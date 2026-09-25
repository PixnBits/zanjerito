package engine

import (
	"context"
	"errors"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/gpio"
)

func pauseCfg() Config {
	al := true
	return Config{
		Chip: "gpiochip0", ActiveLow: &al, Timezone: "America/Phoenix", MaxOnSec: 900,
		Power: StationConfig{ID: "psu", BCM: 21},
		Stations: []StationConfig{
			{ID: "front-north", Title: "Front North", BCM: 6},
		},
	}
}

func TestPauseBlocksItinerary(t *testing.T) {
	e, err := New(pauseCfg(), gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	e.SetPause(nil, "rain")
	err = e.RunItinerary(context.Background(), []Step{{StationID: "front-north", Duration: time.Second}})
	if !errors.Is(err, ErrPaused) {
		t.Fatalf("want ErrPaused, got %v", err)
	}
	e.ClearPause()
	// short run should succeed after resume
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go func() {
		time.Sleep(20 * time.Millisecond)
		cancel()
	}()
	_ = e.RunItinerary(ctx, []Step{{StationID: "front-north", Duration: time.Second}})
}

func TestPauseAutoExpireAllowsRun(t *testing.T) {
	e, err := New(pauseCfg(), gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	until := time.Now().Add(-time.Second)
	e.SetPause(&until, "expired")
	if e.IsPaused(time.Now()) {
		t.Fatal("should auto-expire")
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() {
		done <- e.RunItinerary(ctx, []Step{{StationID: "front-north", Duration: 50 * time.Millisecond}})
	}()
	select {
	case err := <-done:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("timeout")
	}
}

func TestStopDoesNotEnterPause(t *testing.T) {
	e, err := New(pauseCfg(), gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go func() {
		_ = e.RunItinerary(ctx, []Step{{StationID: "front-north", Duration: 2 * time.Second}})
	}()
	time.Sleep(30 * time.Millisecond)
	if err := e.Stop(); err != nil {
		t.Fatal(err)
	}
	if e.IsPaused(time.Now()) {
		t.Fatal("STOP must not arm pause")
	}
}
