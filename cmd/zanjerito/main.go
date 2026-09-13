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
)

func main() {
	configPath := flag.String("config", "config/pinmap.example.json", "path to config JSON")
	driverName := flag.String("driver", "fake", "gpio driver: fake|lockout|gpiocdev")
	listen := flag.String("listen", "", "LAN bind for REST+SSE (empty = engine only, e.g. 192.168.1.8:8080)")
	flag.Parse()

	cfg, err := engine.LoadConfig(*configPath)
	if err != nil {
		log.Fatalf("config: %v", err)
	}
	drv, err := gpio.New(*driverName)
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

	log.Printf("zanjerito engine ready (driver=%s phase=%s)", *driverName, eng.Status().Phase)

	if *listen != "" {
		srv := api.New(eng, *configPath)
		httpSrv := &http.Server{Addr: *listen, Handler: srv, ReadHeaderTimeout: 10 * time.Second}
		go func() {
			log.Printf("api listening on %s (LAN trust, D7)", *listen)
			if err := httpSrv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
				log.Printf("api: %v", err)
			}
		}()
		defer func() { _ = httpSrv.Close() }()
	}

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	<-ctx.Done()
	log.Printf("shutdown: Stop + Close")
	_ = eng.Stop()
	fmt.Fprintln(os.Stderr, "zanjerito stopped")
}
