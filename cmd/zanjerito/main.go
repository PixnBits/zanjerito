package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/PixnBits/zanjerito/internal/api"
	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/gpio"
	"github.com/PixnBits/zanjerito/internal/history"
	"github.com/PixnBits/zanjerito/internal/rain"
	"github.com/PixnBits/zanjerito/internal/schedule"
	"github.com/PixnBits/zanjerito/internal/store"
)

func main() {
	configPath := flag.String("config", "config/pinmap.example.json", "path to config JSON")
	driverName := flag.String("driver", "fake", "gpio driver: fake|dualrun|lockout|gpiocdev")
	listen := flag.String("listen", "", "LAN bind for REST+SSE (empty = engine only, e.g. 192.168.1.8:8080)")
	dry := flag.Bool("dry", false, "with -driver=gpiocdev: claim lines but refuse energize (pre-cutover smoke)")
	flag.Parse()

	cfg, err := engine.LoadConfig(*configPath)
	if err != nil {
		log.Fatalf("config: %v", err)
	}
	var drv gpio.Driver
	if *dry {
		if *driverName != "gpiocdev" {
			log.Printf("-dry is only meaningful with -driver=gpiocdev (got %s); ignoring -dry", *driverName)
			drv, err = gpio.New(*driverName)
		} else {
			drv, err = gpio.NewGpiocdevDry()
		}
	} else {
		drv, err = gpio.New(*driverName)
	}
	if err != nil {
		log.Fatalf("gpio driver: %v", err)
	}
	eng, err := engine.New(cfg, drv)
	if err != nil {
		_ = drv.Close()
		log.Fatalf("engine: %v", err)
	}
	defer func() {
		if err := eng.Close(); err != nil {
			log.Printf("engine close: %v", err)
		}
	}()

	if ps, err := store.LoadPause(*configPath, time.Now()); err != nil {
		log.Printf("pause load: %v (continuing unpaused)", err)
	} else {
		rain.ApplyStoredPause(eng, ps)
		if ps.Active {
			log.Printf("pause restored (until=%v reason=%q source=%q)", ps.Until, ps.Reason, ps.Source)
		}
	}

	log.Printf("zanjerito engine ready (driver=%s dry=%v phase=%s)", *driverName, *dry && *driverName == "gpiocdev", eng.Status().Phase)

	// Background, not the signal ctx: Close must still be alive after Stop
	// so the final record (emitted by the run goroutine) can flush.
	hist, err := history.Open(store.HistoryPath(*configPath), nil)
	if err != nil {
		log.Printf("history: %v (continuing empty)", err)
	}
	if hist != nil {
		hist.Start(context.Background())
		eng.SetRecorder(hist)
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	runner, err := schedule.NewRunner(eng, *configPath)
	if err != nil {
		log.Fatalf("schedule: %v", err)
	}
	go runner.Loop(ctx, time.Second)
	log.Printf("schedule runner ticking (America/Phoenix, %s)", *configPath)

	rainPoller, err := rain.Start(ctx, eng, *configPath)
	if err != nil {
		log.Printf("rain: disabled: %v", err)
	}

	if *listen != "" {
		srv := api.New(eng, *configPath)
		srv.History = hist
		srv.Rain = rainPoller
		httpSrv := &http.Server{Addr: *listen, Handler: srv, ReadHeaderTimeout: 10 * time.Second}
		go func() {
			log.Printf("api listening on %s (LAN trust, D7)", *listen)
			if err := httpSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
				log.Printf("api: %v", err)
			}
		}()
		defer func() { _ = httpSrv.Close() }()
	}

	<-ctx.Done()
	log.Printf("shutdown: Stop + Close")
	_ = eng.Stop()
	if hist != nil {
		hist.Close()
	}
	fmt.Fprintln(os.Stderr, "zanjerito stopped")
}
