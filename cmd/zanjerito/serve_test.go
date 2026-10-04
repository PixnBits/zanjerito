package main

import (
	"context"
	"fmt"
	"io"
	"net"
	"net/http"
	"os"
	"strings"
	"sync"
	"sync/atomic"
	"syscall"
	"testing"
	"time"
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

func (l *logBuf) linesContaining(sub string) int {
	l.mu.Lock()
	defer l.mu.Unlock()
	n := 0
	for _, line := range l.s {
		if strings.Contains(line, sub) {
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
	done := make(chan struct{})
	go func() {
		defer close(done)
		serveAPI(ctx, srv, listen, frozenRetry(2*time.Millisecond), logs.Printf)
	}()
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
	done := make(chan struct{})
	go func() {
		defer close(done)
		serveAPI(ctx, srv, listen, retryOpts{Initial: time.Second, Max: time.Second}, func(string, ...any) {})
	}()
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
	done := make(chan struct{})
	go func() {
		defer close(done)
		serveAPI(ctx, srv, listen, frozenRetry(2*time.Millisecond), logs.Printf)
	}()
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
	done := make(chan struct{})
	go func() {
		defer close(done)
		serveAPI(ctx, srv, listen, retryOpts{}, func(string, ...any) {})
	}()
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
