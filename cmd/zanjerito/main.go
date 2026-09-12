package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"

	"github.com/PixnBits/zanjerito/internal/gpio"
)

func main() {
	configPath := flag.String("config", "config/pinmap.example.json", "path to config JSON")
	driverName := flag.String("driver", "fake", "gpio driver: fake|lockout|gpiocdev")
	flag.Parse()

	log.Printf("zanjerito starting (driver=%s config=%s)", *driverName, *configPath)

	drv, err := gpio.New(*driverName)
	if err != nil {
		log.Fatalf("gpio driver: %v", err)
	}
	defer func() {
		if err := drv.Close(); err != nil {
			log.Printf("gpio close: %v", err)
		}
	}()

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	// Skeleton: prove process lifecycle + fail-safe close. Engine/API land in follow-up PRs.
	<-ctx.Done()
	log.Printf("shutdown signal: ensuring driver close (fail-safe)")
	fmt.Fprintln(os.Stderr, "zanjerito stopped")
}
