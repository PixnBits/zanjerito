package main

import (
	"context"
	"errors"
	"fmt"
	"log"
	"net"
	"net/http"
	"syscall"
	"time"
)

const (
	defaultBackoffInitial = time.Second
	defaultBackoffMax     = 30 * time.Second
	defaultLogInterval    = time.Minute
	apiShutdownTimeout    = 5 * time.Second
)

// retryOpts controls listen backoff and failure-log rate. Zero value uses
// 1s initial, 30s max, one identical log per minute.
// sleep, when set, replaces the real timer. Tests use it to record delays.
type retryOpts struct {
	Initial     time.Duration
	Max         time.Duration
	LogInterval time.Duration
	Now         func() time.Time
	sleep       func(ctx context.Context, d time.Duration) bool
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
// logged, backoff resets to Initial, and the loop rebinds. It returns when
// ctx is cancelled, Serve returns ErrServerClosed, or Serve returns nil.
// A successful listen after ctx is done closes the listener and does not Serve.
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
		err = srv.Serve(ln)
		if err == nil || errors.Is(err, http.ErrServerClosed) || ctx.Err() != nil {
			return
		}
		logf("api: %v", err)
		hadFail = true
		backoff = opt.Initial
		if !opt.sleep(ctx, backoff) {
			return
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

// shutdownSequence turns relays off before any HTTP wait, then runs afterStop
// (history flush), then a bounded HTTP shutdown. A client on /api/events or a
// half-read header must not delay all-off.
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
