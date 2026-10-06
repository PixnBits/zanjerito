package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"sync"
	"sync/atomic"
	"syscall"
	"time"
)

const (
	defaultBackoffInitial = time.Second
	defaultBackoffMax     = 30 * time.Second
	defaultLogInterval    = time.Minute
	defaultHealthyAfter   = 30 * time.Second
	apiShutdownTimeout    = 5 * time.Second
	// shutdownBodyLimit caps a mutating request buffered before the gate check.
	shutdownBodyLimit = 4 << 20
	shutdownGateWait  = 250 * time.Millisecond
)

// retryOpts controls listen backoff and failure-log rate. Zero value uses
// 1s initial, 30s max, one identical log per minute, and a 30s healthy Serve.
// sleep, when set, replaces the real timer. Tests use it to record delays.
// healthyAfter is how long Serve must have run before a Serve error resets
// backoff to Initial. Zero uses defaultHealthyAfter.
type retryOpts struct {
	Initial      time.Duration
	Max          time.Duration
	LogInterval  time.Duration
	Now          func() time.Time
	sleep        func(ctx context.Context, d time.Duration) bool
	healthyAfter time.Duration
}

func (o retryOpts) withDefaults() retryOpts {
	if o.Initial <= 0 {
		o.Initial = defaultBackoffInitial
	}
	if o.Max <= 0 {
		o.Max = defaultBackoffMax
	}
	if o.LogInterval <= 0 {
		o.LogInterval = defaultLogInterval
	}
	if o.healthyAfter <= 0 {
		o.healthyAfter = defaultHealthyAfter
	}
	if o.Now == nil {
		o.Now = time.Now
	}
	if o.sleep == nil {
		o.sleep = sleepCtx
	}
	if o.Initial > o.Max {
		o.Initial = o.Max
	}
	return o
}

func listenTCP(network, addr string) (net.Listener, error) {
	var lc net.ListenConfig
	return lc.Listen(context.Background(), network, addr)
}

// serveAPI binds srv.Addr, retrying every listen error with capped exponential
// backoff. A non-ErrServerClosed Serve error while ctx is still active is
// logged and the loop rebinds. Backoff resets to Initial only when that Serve
// stayed up for at least healthyAfter (default 30s on the opt.Now clock);
// otherwise the current backoff is slept and doubled, capped at Max.
// It returns when ctx is cancelled, Serve returns ErrServerClosed, or Serve
// returns nil. A successful listen after ctx is done closes the listener and
// does not Serve.
func serveAPI(ctx context.Context, srv *http.Server, listen func(network, addr string) (net.Listener, error), opt retryOpts, logf func(string, ...any)) {
	opt = opt.withDefaults()
	if listen == nil {
		listen = listenTCP
	}
	if logf == nil {
		logf = log.Printf
	}

	backoff := opt.Initial
	var (
		attempts int
		lastKey  string
		lastLog  time.Time
		hadFail  bool
	)

	for {
		if ctx.Err() != nil {
			return
		}
		ln, err := listen("tcp", srv.Addr)
		attempts++
		if err != nil {
			hadFail = true
			key := listenErrKey(err)
			now := opt.Now()
			if lastKey == "" || key != lastKey || now.Sub(lastLog) >= opt.LogInterval {
				logf("api: %s", listenFailMsg(err))
				lastLog = now
				lastKey = key
			}
			if !opt.sleep(ctx, backoff) {
				return
			}
			if backoff < opt.Max {
				backoff *= 2
				if backoff > opt.Max {
					backoff = opt.Max
				}
			}
			continue
		}

		if ctx.Err() != nil {
			_ = ln.Close()
			return
		}
		if hadFail {
			logf("api: bound %s after %d attempts", ln.Addr().String(), attempts)
		}
		logf("api listening on %s (LAN trust, D7)", ln.Addr())
		servedAt := opt.Now()
		err = srv.Serve(ln)
		servedFor := opt.Now().Sub(servedAt)
		if err == nil || errors.Is(err, http.ErrServerClosed) || ctx.Err() != nil {
			return
		}
		logf("api: %v", err)
		hadFail = true
		// A bind that dies immediately must not snap back to Initial; that
		// hot-loops accept failures. A Serve that stayed up is a fresh fault.
		healthy := servedFor >= opt.healthyAfter
		if healthy {
			backoff = opt.Initial
		}
		if !opt.sleep(ctx, backoff) {
			return
		}
		if !healthy && backoff < opt.Max {
			backoff *= 2
			if backoff > opt.Max {
				backoff = opt.Max
			}
		}
	}
}

// startAPI runs serveAPI in the background and returns its done signal
// immediately. The caller must not block on listen before this returns.
func startAPI(ctx context.Context, srv *http.Server, listen func(network, addr string) (net.Listener, error), opt retryOpts, logf func(string, ...any)) <-chan struct{} {
	done := make(chan struct{})
	go func() {
		defer close(done)
		serveAPI(ctx, srv, listen, opt, logf)
	}()
	return done
}

// newAPIServer is the LAN HTTP server. Shutdown cancels the base context so
// handlers that select on r.Context() (SSE) return without waiting out the
// shutdown deadline. ReadHeaderTimeout stays 10s.
func newAPIServer(addr string, handler http.Handler) *http.Server {
	ctx, cancel := context.WithCancel(context.Background())
	srv := &http.Server{
		Addr:              addr,
		Handler:           handler,
		ReadHeaderTimeout: 10 * time.Second,
		BaseContext:       func(net.Listener) context.Context { return ctx },
	}
	srv.RegisterOnShutdown(cancel)
	return srv
}

func listenFailMsg(err error) string {
	if addrNotYetAvailable(err) {
		return "address not yet available, will keep retrying"
	}
	return fmt.Sprintf("listen failed: %v, will keep retrying", err)
}

func listenErrKey(err error) string {
	if addrNotYetAvailable(err) {
		return "eaddrnotavail"
	}
	if err == nil {
		return ""
	}
	return err.Error()
}

func addrNotYetAvailable(err error) bool {
	return errors.Is(err, syscall.EADDRNOTAVAIL)
}

func sleepCtx(ctx context.Context, d time.Duration) bool {
	if d <= 0 {
		return ctx.Err() == nil
	}
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-ctx.Done():
		if !t.Stop() {
			select {
			case <-t.C:
			default:
			}
		}
		return false
	case <-t.C:
		return true
	}
}

// shutdownGate rejects mutating requests once shutdown has started.
// GET, HEAD, and OPTIONS skip it so SSE and reads continue during the drain.
// The body is buffered before the down check: a request whose body finishes
// after Begin must not reach the handler. An in-flight handler holds mu for
// the whole call, so Begin can wait for it before relays drop.
type shutdownGate struct {
	mu   sync.RWMutex
	down atomic.Bool
}

// Wrap returns a handler that gates every method except GET, HEAD, and OPTIONS.
func (g *shutdownGate) Wrap(h http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		switch r.Method {
		case http.MethodGet, http.MethodHead, http.MethodOptions:
			h.ServeHTTP(w, r)
			return
		}
		body, err := io.ReadAll(http.MaxBytesReader(w, r.Body, shutdownBodyLimit))
		if err != nil {
			code, msg := gateReadErr(err)
			writeGateErr(w, code, msg)
			return
		}
		g.mu.RLock()
		defer g.mu.RUnlock()
		if g.down.Load() {
			w.Header().Set("Retry-After", "5")
			writeGateErr(w, http.StatusServiceUnavailable, "shutting down")
			return
		}
		r.Body = io.NopCloser(bytes.NewReader(body))
		h.ServeHTTP(w, r)
	})
}

// Begin refuses later mutating requests, then waits up to 250ms for a handler
// already inside Wrap to return. down is stored before the wait so a body that
// completes during the wait is rejected. With nothing in flight, TryLock
// succeeds immediately and relay-off is not delayed.
func (g *shutdownGate) Begin() {
	if g == nil {
		return
	}
	g.down.Store(true)
	deadline := time.Now().Add(shutdownGateWait)
	for {
		if g.mu.TryLock() {
			g.mu.Unlock()
			return
		}
		remain := time.Until(deadline)
		if remain <= 0 {
			return
		}
		if remain > time.Millisecond {
			remain = time.Millisecond
		}
		time.Sleep(remain)
	}
}

func gateReadErr(err error) (int, string) {
	var max *http.MaxBytesError
	if errors.As(err, &max) {
		return http.StatusRequestEntityTooLarge, "request body too large"
	}
	return http.StatusBadRequest, "bad request body"
}

func writeGateErr(w http.ResponseWriter, code int, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(map[string]string{"error": msg})
}

// gracefulShutdown closes the gate, stops relays, flushes history, then bounds
// the HTTP drain. nil gate, stopRelays, and closeHist are skipped. Begin runs
// before stopRelays so a request still reading its body cannot start a run
// after the relays drop.
func gracefulShutdown(gate *shutdownGate, stopRelays func() error, closeHist func(), srv *http.Server, apiDone <-chan struct{}, httpTimeout time.Duration) {
	if gate != nil {
		gate.Begin()
	}
	shutdownSequence(stopRelays, closeHist, srv, apiDone, httpTimeout)
}

// shutdownSequence turns relays off before any HTTP wait, then runs afterStop
// (history flush), then a bounded HTTP shutdown. A client on /api/events or a
// half-read header must not delay all-off. Callers that serve the API should
// use gracefulShutdown so the gate closes first.
func shutdownSequence(stopRelays func() error, afterStop func(), srv *http.Server, apiDone <-chan struct{}, httpTimeout time.Duration) {
	if stopRelays != nil {
		_ = stopRelays()
	}
	if afterStop != nil {
		afterStop()
	}
	shutdownHTTP(srv, apiDone, httpTimeout)
}

// shutdownHTTP stops srv with a bounded Shutdown, Close fallback, then waits
// for serveAPI's done signal (total wait around timeout, plus 1s after Close).
func shutdownHTTP(srv *http.Server, done <-chan struct{}, timeout time.Duration) {
	if timeout <= 0 {
		timeout = apiShutdownTimeout
	}
	deadline := time.Now().Add(timeout)
	if srv != nil {
		ctx, cancel := context.WithDeadline(context.Background(), deadline)
		err := srv.Shutdown(ctx)
		cancel()
		if err != nil {
			_ = srv.Close()
		}
	}
	if done == nil {
		return
	}
	remain := time.Until(deadline)
	if remain < 0 {
		remain = 0
	}
	timer := time.NewTimer(remain)
	defer timer.Stop()
	select {
	case <-done:
		return
	case <-timer.C:
		if srv != nil {
			_ = srv.Close()
		}
	}
	extra := time.NewTimer(time.Second)
	defer extra.Stop()
	select {
	case <-done:
	case <-extra.C:
	}
}
