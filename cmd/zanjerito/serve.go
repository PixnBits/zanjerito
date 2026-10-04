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
type retryOpts struct {
	Initial     time.Duration
	Max         time.Duration
	LogInterval time.Duration
	Now         func() time.Time
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
// backoff. It returns when ctx is cancelled during retry, or after Serve ends.
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
			if !sleepCtx(ctx, backoff) {
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
		if err := srv.Serve(ln); err != nil && !errors.Is(err, http.ErrServerClosed) {
			logf("api: %v", err)
		}
		return
	}
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
