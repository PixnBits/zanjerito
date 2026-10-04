package main

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/api"
	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/gpio"
)

func bindErr(errno syscall.Errno) error {
	return &net.OpError{Op: "listen", Net: "tcp", Err: os.NewSyscallError("bind", errno)}
}

func okHandler() http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusOK)
		_, _ = io.WriteString(w, "ok")
	})
}

func testHTTPServer() *http.Server {
	return &http.Server{
		Addr:              "127.0.0.1:0",
		Handler:           okHandler(),
		ReadHeaderTimeout: 10 * time.Second,
	}
}

type logBuf struct {
	mu sync.Mutex
	s  []string
}

func (l *logBuf) Printf(format string, args ...any) {
	l.mu.Lock()
	defer l.mu.Unlock()
	l.s = append(l.s, fmt.Sprintf(format, args...))
}

func (l *logBuf) String() string {
	l.mu.Lock()
	defer l.mu.Unlock()
	return strings.Join(l.s, "\n")
}

func (l *logBuf) snapshot() []string {
	l.mu.Lock()
	defer l.mu.Unlock()
	out := make([]string, len(l.s))
	copy(out, l.s)
	return out
}

func (l *logBuf) linesContaining(sub string) int {
	n := 0
	for _, line := range l.snapshot() {
		if strings.Contains(line, sub) {
			n++
		}
	}
	return n
}

func (l *logBuf) countExact(line string) int {
	n := 0
	for _, got := range l.snapshot() {
		if got == line {
			n++
		}
	}
	return n
}

func frozenRetry(initial time.Duration) retryOpts {
	t0 := time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC)
	return retryOpts{
		Initial:     initial,
		Max:         8 * time.Millisecond,
		LogInterval: time.Minute,
		Now:         func() time.Time { return t0 },
	}
}

func waitFor(t *testing.T, d time.Duration, msg string, ok func() bool) {
	t.Helper()
	deadline := time.Now().Add(d)
	for time.Now().Before(deadline) {
		if ok() {
			return
		}
		time.Sleep(2 * time.Millisecond)
	}
	t.Fatal(msg)
}

func getOK(url string) bool {
	client := &http.Client{Timeout: 200 * time.Millisecond}
	resp, err := client.Get(url)
	if err != nil {
		return false
	}
	defer resp.Body.Close()
	_, _ = io.Copy(io.Discard, resp.Body)
	return resp.StatusCode == http.StatusOK
}

func TestServeAPIRetriesUntilAddressAppears(t *testing.T) {
	const failN = 3
	var attempts atomic.Int32
	addrCh := make(chan string, 1)
	listen := func(network, addr string) (net.Listener, error) {
		n := attempts.Add(1)
		if n <= failN {
			return nil, bindErr(syscall.EADDRNOTAVAIL)
		}
		ln, err := net.Listen("tcp", "127.0.0.1:0")
		if err != nil {
			return nil, err
		}
		select {
		case addrCh <- ln.Addr().String():
		default:
		}
		return ln, nil
	}

	var logs logBuf
	srv := testHTTPServer()
	ctx, cancel := context.WithCancel(context.Background())
	done := startAPI(ctx, srv, listen, frozenRetry(2*time.Millisecond), logs.Printf)
	t.Cleanup(func() {
		cancel()
		_ = srv.Close()
		select {
		case <-done:
		case <-time.After(2 * time.Second):
		}
	})

	waitFor(t, time.Second, "serveAPI blocked the caller (no in-flight retry observed)", func() bool {
		n := attempts.Load()
		return n >= 1 && n <= failN
	})

	var addr string
	select {
	case addr = <-addrCh:
	case <-time.After(2 * time.Second):
		t.Fatal("did not bind after retries")
	}
	url := "http://" + addr + "/"
	waitFor(t, 2*time.Second, "GET on bound address failed: "+url, func() bool {
		return getOK(url)
	})

	if got := attempts.Load(); got != failN+1 {
		t.Fatalf("listen attempts=%d want %d", got, failN+1)
	}
	if logs.linesContaining("not yet available") != 1 {
		t.Fatalf("want one rate-limited first-failure log, got %q", logs.String())
	}
	if logs.linesContaining("listen failed") != 0 {
		t.Fatalf("EADDRNOTAVAIL must not use listen-failed wording: %q", logs.String())
	}
}

func TestServeAPIShutdownDuringRetry(t *testing.T) {
	var attempts atomic.Int32
	listen := func(network, addr string) (net.Listener, error) {
		attempts.Add(1)
		return nil, bindErr(syscall.EADDRNOTAVAIL)
	}
	srv := testHTTPServer()
	ctx, cancel := context.WithCancel(context.Background())
	done := startAPI(ctx, srv, listen, retryOpts{Initial: time.Second, Max: time.Second}, func(string, ...any) {})
	t.Cleanup(func() {
		cancel()
		select {
		case <-done:
		case <-time.After(2 * time.Second):
		}
	})

	waitFor(t, time.Second, "listen was never attempted", func() bool {
		return attempts.Load() >= 1
	})
	cancel()
	t0 := time.Now()
	select {
	case <-done:
		if d := time.Since(t0); d > 500*time.Millisecond {
			t.Fatalf("serveAPI returned in %v, want <=500ms", d)
		}
	case <-time.After(500 * time.Millisecond):
		t.Fatal("serveAPI did not return after ctx cancel during retry")
	}
}

func TestServeAPIOtherErrorsLoggedDistinctly(t *testing.T) {
	const inuseN = 2
	var attempts atomic.Int32
	addrCh := make(chan string, 1)
	listen := func(network, addr string) (net.Listener, error) {
		n := attempts.Add(1)
		switch {
		case n <= inuseN:
			return nil, bindErr(syscall.EADDRINUSE)
		case n == inuseN+1:
			return nil, fmt.Errorf("bind: permission denied")
		default:
			ln, err := net.Listen("tcp", "127.0.0.1:0")
			if err != nil {
				return nil, err
			}
			select {
			case addrCh <- ln.Addr().String():
			default:
			}
			return ln, nil
		}
	}

	var logs logBuf
	srv := testHTTPServer()
	ctx, cancel := context.WithCancel(context.Background())
	done := startAPI(ctx, srv, listen, frozenRetry(2*time.Millisecond), logs.Printf)
	t.Cleanup(func() {
		cancel()
		_ = srv.Close()
		select {
		case <-done:
		case <-time.After(2 * time.Second):
		}
	})

	var addr string
	select {
	case addr = <-addrCh:
	case <-time.After(2 * time.Second):
		t.Fatal("did not bind after other listen errors")
	}
	waitFor(t, 2*time.Second, "GET after other errors failed", func() bool {
		return getOK("http://" + addr + "/")
	})

	got := logs.String()
	if strings.Contains(got, "not yet available") {
		t.Fatalf("EADDRINUSE must not use not-yet-available wording: %q", got)
	}
	if logs.linesContaining("listen failed") != 2 {
		t.Fatalf("want first failure + immediate log on error text change (2), got %d in %q", logs.linesContaining("listen failed"), got)
	}
}

func TestServeAPIShutdownBounded(t *testing.T) {
	addrCh := make(chan string, 1)
	listen := func(network, addr string) (net.Listener, error) {
		ln, err := net.Listen("tcp", "127.0.0.1:0")
		if err != nil {
			return nil, err
		}
		select {
		case addrCh <- ln.Addr().String():
		default:
		}
		return ln, nil
	}

	srv := testHTTPServer()
	ctx, cancel := context.WithCancel(context.Background())
	done := startAPI(ctx, srv, listen, retryOpts{}, func(string, ...any) {})
	t.Cleanup(func() {
		cancel()
		_ = srv.Close()
		select {
		case <-done:
		case <-time.After(2 * time.Second):
		}
	})

	var addr string
	select {
	case addr = <-addrCh:
	case <-time.After(2 * time.Second):
		t.Fatal("listen did not succeed")
	}
	url := "http://" + addr + "/"
	waitFor(t, 2*time.Second, "server never answered GET", func() bool {
		return getOK(url)
	})

	tr := &http.Transport{DisableKeepAlives: false, IdleConnTimeout: 90 * time.Second}
	client := &http.Client{Transport: tr, Timeout: 2 * time.Second}
	resp, err := client.Get(url)
	if err != nil {
		t.Fatal(err)
	}
	_, _ = io.Copy(io.Discard, resp.Body)
	resp.Body.Close()

	t0 := time.Now()
	cancel()
	shutdownHTTP(srv, done, 5*time.Second)
	if d := time.Since(t0); d > time.Second {
		t.Fatalf("shutdown took %v with idle keep-alive conn", d)
	}
	select {
	case <-done:
	default:
		t.Fatal("serveAPI done signal not closed after shutdown")
	}
	tr.CloseIdleConnections()
}

func gpioAllOff(drv gpio.Driver) (map[string]gpio.Level, bool) {
	st := gpio.StateForTest(drv)
	if len(st) == 0 {
		return st, false
	}
	for _, lvl := range st {
		if lvl != gpio.Off {
			return st, false
		}
	}
	return st, true
}

// acceptFailListener's first Accept (and every Accept) returns a non-temporary
// error so http.Server.Serve returns instead of retrying.
type acceptFailListener struct{ net.Listener }

func (l *acceptFailListener) Accept() (net.Conn, error) {
	return nil, errAcceptFailed
}

var errAcceptFailed = errors.New("accept failed")

func TestShutdownSequenceRelaysOffBeforeHTTP(t *testing.T) {
	const httpTimeout = 2 * time.Second

	cfg := frontCfg()
	drv := gpio.NewFake()
	eng, err := engine.New(cfg, drv)
	if err != nil {
		t.Fatal(err)
	}
	defer eng.Close()

	runDone := make(chan struct{})
	go func() {
		defer close(runDone)
		_ = eng.RunItinerary(context.Background(), []engine.Step{{
			StationID: "front-west",
			Duration:  45 * time.Second,
		}})
	}()
	waitFor(t, 2*time.Second, "station did not energize before shutdown", func() bool {
		st := gpio.StateForTest(drv)
		return st["front-west"] == gpio.On && st["psu"] == gpio.On
	})

	var conns atomic.Int32
	handler := api.New(eng, filepath.Join(t.TempDir(), "cfg.json"))
	srv := newAPIServer("127.0.0.1:0", handler)
	srv.ConnState = func(_ net.Conn, cs http.ConnState) {
		if cs == http.StateNew {
			conns.Add(1)
		}
	}

	addrCh := make(chan string, 1)
	listen := func(network, addr string) (net.Listener, error) {
		ln, err := net.Listen("tcp", "127.0.0.1:0")
		if err != nil {
			return nil, err
		}
		select {
		case addrCh <- ln.Addr().String():
		default:
		}
		return ln, nil
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	apiDone := startAPI(ctx, srv, listen, retryOpts{}, func(string, ...any) {})
	t.Cleanup(func() {
		cancel()
		_ = srv.Close()
		select {
		case <-apiDone:
		case <-time.After(2 * time.Second):
		}
	})

	var addr string
	select {
	case addr = <-addrCh:
	case <-time.After(2 * time.Second):
		t.Fatal("listen did not succeed")
	}

	client := &http.Client{}
	req, err := http.NewRequest(http.MethodGet, "http://"+addr+"/api/events", nil)
	if err != nil {
		t.Fatal(err)
	}
	resp, err := client.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	first := make(chan struct {
		n   int
		err error
	}, 1)
	go func() {
		buf := make([]byte, 32)
		n, err := resp.Body.Read(buf)
		first <- struct {
			n   int
			err error
		}{n, err}
	}()
	select {
	case rr := <-first:
		if rr.n == 0 && rr.err != nil {
			t.Fatalf("sse prelude: %v", rr.err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for SSE bytes")
	}

	raw, err := net.Dial("tcp", addr)
	if err != nil {
		t.Fatal(err)
	}
	defer raw.Close()
	if _, err := raw.Write([]byte("GET /api/events HTTP/1.1\r\nHost: 127.0.0.1\r\n")); err != nil {
		t.Fatal(err)
	}
	waitFor(t, 2*time.Second, "SSE and partial-header conns were not both accepted", func() bool {
		return conns.Load() >= 2
	})

	sseErr := make(chan error, 1)
	go func() {
		_, err := io.Copy(io.Discard, resp.Body)
		sseErr <- err
	}()
	partialErr := make(chan error, 1)
	go func() {
		_ = raw.SetReadDeadline(time.Now().Add(5 * time.Second))
		_, err := raw.Read(make([]byte, 1))
		partialErr <- err
	}()

	var offAt, histAt time.Time
	start := time.Now()
	shutdownSequence(func() error {
		err := eng.Stop()
		offAt = time.Now()
		return err
	}, func() {
		histAt = time.Now()
		if st, ok := gpioAllOff(drv); !ok {
			t.Errorf("gpio not all off before HTTP shutdown: %v", st)
		}
	}, srv, apiDone, httpTimeout)
	elapsed := time.Since(start)

	if offAt.IsZero() || offAt.Before(start) || offAt.Sub(start) >= 300*time.Millisecond {
		t.Fatalf("relays off at %v (delta %v), want <300ms after shutdownSequence starts", offAt, offAt.Sub(start))
	}
	if histAt.IsZero() || histAt.Before(offAt) || histAt.Sub(start) >= 300*time.Millisecond {
		t.Fatalf("afterStop at %v (delta %v), want after relays-off and <300ms", histAt, histAt.Sub(start))
	}
	if elapsed < httpTimeout*8/10 {
		t.Fatalf("shutdownSequence returned in %v; HTTP shutdown was not held (timeout %v)", elapsed, httpTimeout)
	}
	if elapsed > httpTimeout+1500*time.Millisecond {
		t.Fatalf("shutdownSequence took %v, want <= %v", elapsed, httpTimeout+1500*time.Millisecond)
	}
	if st, ok := gpioAllOff(drv); !ok {
		t.Fatalf("gpio after shutdownSequence: %v", st)
	}

	dialer := net.Dialer{Timeout: 200 * time.Millisecond}
	if c, err := dialer.Dial("tcp", addr); err == nil {
		c.Close()
		t.Fatal("server still accepts connections after shutdownSequence")
	}
	select {
	case err := <-partialErr:
		if err == nil {
			t.Fatal("partial-header conn still readable")
		}
	case <-time.After(500 * time.Millisecond):
		t.Fatal("partial-header conn was not closed")
	}
	select {
	case <-sseErr:
	case <-time.After(500 * time.Millisecond):
		t.Fatal("SSE conn was not closed")
	}

	select {
	case <-runDone:
	case <-time.After(time.Second):
		t.Fatal("run did not finish after Stop")
	}
}

func TestShutdownHTTPDeadlineForcesClose(t *testing.T) {
	const timeout = 150 * time.Millisecond

	entered := make(chan struct{})
	release := make(chan struct{})
	t.Cleanup(func() {
		select {
		case <-release:
		default:
			close(release)
		}
	})
	srv := &http.Server{
		Addr: "127.0.0.1:0",
		Handler: http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			close(entered)
			<-release
		}),
		ReadHeaderTimeout: 10 * time.Second,
	}

	addrCh := make(chan string, 1)
	listen := func(network, addr string) (net.Listener, error) {
		ln, err := net.Listen("tcp", "127.0.0.1:0")
		if err != nil {
			return nil, err
		}
		select {
		case addrCh <- ln.Addr().String():
		default:
		}
		return ln, nil
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	apiDone := startAPI(ctx, srv, listen, retryOpts{}, func(string, ...any) {})
	t.Cleanup(func() {
		cancel()
		_ = srv.Close()
	})

	var addr string
	select {
	case addr = <-addrCh:
	case <-time.After(2 * time.Second):
		t.Fatal("listen did not succeed")
	}

	clientErr := make(chan error, 1)
	go func() {
		resp, err := (&http.Client{}).Get("http://" + addr + "/")
		if err != nil {
			clientErr <- err
			return
		}
		defer resp.Body.Close()
		_, err = io.Copy(io.Discard, resp.Body)
		clientErr <- err
	}()
	select {
	case <-entered:
	case <-time.After(2 * time.Second):
		t.Fatal("handler did not start")
	}

	finished := make(chan time.Duration, 1)
	go func() {
		t0 := time.Now()
		shutdownHTTP(srv, apiDone, timeout)
		finished <- time.Since(t0)
	}()
	select {
	case elapsed := <-finished:
		if elapsed < timeout*8/10 {
			t.Fatalf("shutdownHTTP returned in %v, want >= %v while a handler is held", elapsed, timeout*8/10)
		}
		if elapsed > timeout+1200*time.Millisecond {
			t.Fatalf("shutdownHTTP returned in %v, want <= %v", elapsed, timeout+1200*time.Millisecond)
		}
	case <-time.After(timeout + 1200*time.Millisecond):
		t.Fatal("shutdownHTTP hung past timeout+1.2s")
	}
	select {
	case <-apiDone:
	default:
		t.Fatal("apiDone not closed after shutdownHTTP")
	}
	select {
	case err := <-clientErr:
		if err == nil {
			t.Fatal("client got a full response; Close fallback did not drop the conn")
		}
	case <-time.After(500 * time.Millisecond):
		t.Fatal("held client still blocked; Close fallback did not run")
	}
}

func TestSSEEndsOnShutdownViaBaseContext(t *testing.T) {
	cfg := frontCfg()
	drv := gpio.NewFake()
	eng, err := engine.New(cfg, drv)
	if err != nil {
		t.Fatal(err)
	}
	defer eng.Close()

	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	srv := newAPIServer(ln.Addr().String(), api.New(eng, filepath.Join(t.TempDir(), "cfg.json")))
	if srv.ReadHeaderTimeout != 10*time.Second {
		t.Fatalf("ReadHeaderTimeout=%v, want 10s", srv.ReadHeaderTimeout)
	}
	serveDone := make(chan struct{})
	go func() {
		defer close(serveDone)
		_ = srv.Serve(ln)
	}()
	t.Cleanup(func() {
		_ = srv.Close()
		select {
		case <-serveDone:
		case <-time.After(2 * time.Second):
		}
	})

	addr := ln.Addr().String()
	waitFor(t, 2*time.Second, "listener did not accept", func() bool {
		c, err := net.DialTimeout("tcp", addr, 50*time.Millisecond)
		if err != nil {
			return false
		}
		c.Close()
		return true
	})

	req, err := http.NewRequest(http.MethodGet, "http://"+addr+"/api/events", nil)
	if err != nil {
		t.Fatal(err)
	}
	resp, err := (&http.Client{}).Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	buf := make([]byte, 32)
	n, rerr := resp.Body.Read(buf)
	if n == 0 && rerr != nil {
		t.Fatalf("sse prelude: %v", rerr)
	}

	clientErr := make(chan error, 1)
	go func() {
		_, err := io.Copy(io.Discard, resp.Body)
		clientErr <- err
	}()

	shutErr := make(chan error, 1)
	t0 := time.Now()
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		shutErr <- srv.Shutdown(ctx)
	}()
	select {
	case err := <-clientErr:
		if d := time.Since(t0); d >= 500*time.Millisecond {
			t.Fatalf("SSE client disconnected in %v, want <500ms (err %v)", d, err)
		}
	case <-time.After(500 * time.Millisecond):
		t.Fatal("SSE client still connected after 500ms; base context was not cancelled")
	}
	select {
	case err := <-shutErr:
		if d := time.Since(t0); d >= 500*time.Millisecond {
			t.Fatalf("Shutdown returned in %v (err %v), want <500ms", d, err)
		}
	case <-time.After(500 * time.Millisecond):
		t.Fatal("Shutdown blocked; SSE handler did not leave on base-context cancel")
	}
}

func TestServeAPIBackoffDoublesAndCaps(t *testing.T) {
	want := []time.Duration{
		time.Second,
		2 * time.Second,
		4 * time.Second,
		8 * time.Second,
		16 * time.Second,
		30 * time.Second,
		30 * time.Second,
		30 * time.Second,
	}
	var mu sync.Mutex
	var sleeps []time.Duration
	var attempts atomic.Int32
	addrCh := make(chan string, 1)
	listen := func(network, addr string) (net.Listener, error) {
		if attempts.Add(1) <= 8 {
			return nil, bindErr(syscall.EADDRNOTAVAIL)
		}
		ln, err := net.Listen("tcp", "127.0.0.1:0")
		if err != nil {
			return nil, err
		}
		select {
		case addrCh <- ln.Addr().String():
		default:
		}
		return ln, nil
	}
	opt := retryOpts{
		Initial: time.Second,
		Max:     30 * time.Second,
		sleep: func(ctx context.Context, d time.Duration) bool {
			mu.Lock()
			sleeps = append(sleeps, d)
			mu.Unlock()
			return ctx.Err() == nil
		},
	}
	srv := testHTTPServer()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := startAPI(ctx, srv, listen, opt, func(string, ...any) {})
	t.Cleanup(func() {
		cancel()
		_ = srv.Close()
		select {
		case <-done:
		case <-time.After(2 * time.Second):
		}
	})

	var addr string
	select {
	case addr = <-addrCh:
	case <-time.After(2 * time.Second):
		t.Fatal("did not bind after 8 backoff sleeps")
	}
	waitFor(t, 2*time.Second, "GET after capped backoff failed", func() bool {
		return getOK("http://" + addr + "/")
	})

	mu.Lock()
	got := append([]time.Duration(nil), sleeps...)
	mu.Unlock()
	if len(got) != len(want) {
		t.Fatalf("sleeps=%v want %v", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("sleeps=%v want %v", got, want)
		}
	}
}

func TestServeAPIBoundAfterAttemptCount(t *testing.T) {
	t.Run("after failures", func(t *testing.T) {
		const failN = 3
		var attempts atomic.Int32
		addrCh := make(chan string, 1)
		listen := func(network, addr string) (net.Listener, error) {
			n := attempts.Add(1)
			if n <= failN {
				return nil, bindErr(syscall.EADDRNOTAVAIL)
			}
			ln, err := net.Listen("tcp", "127.0.0.1:0")
			if err != nil {
				return nil, err
			}
			select {
			case addrCh <- ln.Addr().String():
			default:
			}
			return ln, nil
		}
		var logs logBuf
		srv := testHTTPServer()
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		done := startAPI(ctx, srv, listen, frozenRetry(time.Millisecond), logs.Printf)
		t.Cleanup(func() {
			cancel()
			_ = srv.Close()
			select {
			case <-done:
			case <-time.After(2 * time.Second):
			}
		})
		var addr string
		select {
		case addr = <-addrCh:
		case <-time.After(2 * time.Second):
			t.Fatal("did not bind")
		}
		waitFor(t, 2*time.Second, "GET failed", func() bool {
			return getOK("http://" + addr + "/")
		})
		want := fmt.Sprintf("api: bound %s after %d attempts", addr, failN+1)
		if logs.countExact(want) != 1 || logs.linesContaining("after") != 1 {
			t.Fatalf("logs=%q want exactly one %q", logs.String(), want)
		}
	})

	t.Run("first attempt", func(t *testing.T) {
		addrCh := make(chan string, 1)
		listen := func(network, addr string) (net.Listener, error) {
			ln, err := net.Listen("tcp", "127.0.0.1:0")
			if err != nil {
				return nil, err
			}
			select {
			case addrCh <- ln.Addr().String():
			default:
			}
			return ln, nil
		}
		var logs logBuf
		srv := testHTTPServer()
		ctx, cancel := context.WithCancel(context.Background())
		defer cancel()
		done := startAPI(ctx, srv, listen, retryOpts{}, logs.Printf)
		t.Cleanup(func() {
			cancel()
			_ = srv.Close()
			select {
			case <-done:
			case <-time.After(2 * time.Second):
			}
		})
		var addr string
		select {
		case addr = <-addrCh:
		case <-time.After(2 * time.Second):
			t.Fatal("did not bind")
		}
		waitFor(t, 2*time.Second, "GET failed", func() bool {
			return getOK("http://" + addr + "/")
		})
		if logs.linesContaining("api listening") != 1 {
			t.Fatalf("server did not log a successful listen: %q", logs.String())
		}
		if logs.linesContaining("bound") != 0 || logs.linesContaining("after") != 0 {
			t.Fatalf("first success must not log bound-after: %q", logs.String())
		}
	})
}

func TestServeAPIRelogsSameErrorOncePerMinute(t *testing.T) {
	start := time.Date(2026, 6, 1, 0, 0, 0, 0, time.UTC)
	var mu sync.Mutex
	now := start
	useInUse := false
	var counts []int
	var logs logBuf

	listen := func(network, addr string) (net.Listener, error) {
		mu.Lock()
		inUse := useInUse
		mu.Unlock()
		if inUse {
			return nil, bindErr(syscall.EADDRINUSE)
		}
		return nil, bindErr(syscall.EADDRNOTAVAIL)
	}
	opt := retryOpts{
		Initial:     time.Millisecond,
		Max:         time.Second,
		LogInterval: time.Minute,
		Now: func() time.Time {
			mu.Lock()
			defer mu.Unlock()
			return now
		},
		sleep: func(ctx context.Context, d time.Duration) bool {
			mu.Lock()
			defer mu.Unlock()
			counts = append(counts, logs.linesContaining("api:"))
			switch len(counts) {
			case 1:
				// next failure is the same error at the same instant
			case 2:
				now = start.Add(59 * time.Second)
			case 3:
				now = start.Add(61 * time.Second)
			case 4:
				useInUse = true
			default:
				return false
			}
			return true
		},
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := startAPI(ctx, testHTTPServer(), listen, opt, logs.Printf)
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("serveAPI did not stop after the scripted sleeps")
	}

	mu.Lock()
	got := append([]int(nil), counts...)
	mu.Unlock()
	wantCounts := []int{1, 1, 1, 2, 3}
	if len(got) != len(wantCounts) {
		t.Fatalf("log counts after each failure=%v want %v; logs=%q", got, wantCounts, logs.String())
	}
	for i := range wantCounts {
		if got[i] != wantCounts[i] {
			t.Fatalf("log counts after each failure=%v want %v; logs=%q", got, wantCounts, logs.String())
		}
	}
	lines := logs.snapshot()
	if len(lines) != 3 {
		t.Fatalf("logs=%q", logs.String())
	}
	if !strings.Contains(lines[0], "not yet available") || !strings.Contains(lines[1], "not yet available") {
		t.Fatalf("same error should be the first two logs: %q", logs.String())
	}
	if !strings.Contains(lines[2], "listen failed") || !strings.Contains(lines[2], "address already in use") {
		t.Fatalf("changed error should log immediately: %q", logs.String())
	}
}

func TestServeAPIRebindsAfterServeError(t *testing.T) {
	var mu sync.Mutex
	var sleeps []time.Duration
	var attempts atomic.Int32
	addrCh := make(chan string, 1)
	listen := func(network, addr string) (net.Listener, error) {
		switch attempts.Add(1) {
		case 1, 3:
			return nil, bindErr(syscall.EADDRNOTAVAIL)
		case 2:
			ln, err := net.Listen("tcp", "127.0.0.1:0")
			if err != nil {
				return nil, err
			}
			return &acceptFailListener{Listener: ln}, nil
		default:
			ln, err := net.Listen("tcp", "127.0.0.1:0")
			if err != nil {
				return nil, err
			}
			select {
			case addrCh <- ln.Addr().String():
			default:
			}
			return ln, nil
		}
	}
	var logs logBuf
	opt := retryOpts{
		Initial: time.Second,
		Max:     30 * time.Second,
		sleep: func(ctx context.Context, d time.Duration) bool {
			mu.Lock()
			sleeps = append(sleeps, d)
			mu.Unlock()
			return ctx.Err() == nil
		},
	}
	srv := testHTTPServer()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := startAPI(ctx, srv, listen, opt, logs.Printf)
	t.Cleanup(func() {
		cancel()
		_ = srv.Close()
		select {
		case <-done:
		case <-time.After(2 * time.Second):
		}
	})

	var addr string
	select {
	case addr = <-addrCh:
	case <-time.After(2 * time.Second):
		t.Fatal("serveAPI did not rebind after Serve returned an accept error")
	}
	waitFor(t, 2*time.Second, "second listener did not serve", func() bool {
		return getOK("http://" + addr + "/")
	})
	if got := attempts.Load(); got != 4 {
		t.Fatalf("listen calls=%d want 4", got)
	}
	if logs.linesContaining("accept failed") != 1 {
		t.Fatalf("want the Serve error logged once, got %q", logs.String())
	}
	mu.Lock()
	got := append([]time.Duration(nil), sleeps...)
	mu.Unlock()
	want := []time.Duration{time.Second, time.Second, time.Second}
	if len(got) != len(want) {
		t.Fatalf("sleeps=%v want %v (backoff must reset to Initial after Serve error)", got, want)
	}
	for i := range want {
		if got[i] != want[i] {
			t.Fatalf("sleeps=%v want %v (backoff must reset to Initial after Serve error)", got, want)
		}
	}
}

func TestStartAPIReturnsWhileListenBlocked(t *testing.T) {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	entered := make(chan struct{})
	var once sync.Once
	listen := func(network, addr string) (net.Listener, error) {
		once.Do(func() { close(entered) })
		<-ctx.Done()
		return nil, ctx.Err()
	}
	ret := make(chan (<-chan struct{}), 1)
	go func() {
		ret <- startAPI(ctx, testHTTPServer(), listen, retryOpts{Initial: time.Hour, Max: time.Hour}, func(string, ...any) {})
	}()
	var apiDone <-chan struct{}
	select {
	case apiDone = <-ret:
	case <-time.After(200 * time.Millisecond):
		t.Fatal("startAPI blocked >200ms while listen was blocked")
	}
	select {
	case <-entered:
	case <-time.After(time.Second):
		t.Fatal("listen was never called")
	}
	t0 := time.Now()
	cancel()
	select {
	case <-apiDone:
		if d := time.Since(t0); d > 500*time.Millisecond {
			t.Fatalf("cancel stopped startAPI in %v, want <=500ms", d)
		}
	case <-time.After(500 * time.Millisecond):
		t.Fatal("cancel did not stop startAPI promptly")
	}
}
