package engine

import (
	"context"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/gpio"
)

func testConfig(t *testing.T) Config {
	t.Helper()
	active := true
	return Config{
		Chip:      "gpiochip0",
		ActiveLow: &active,
		Timezone:  "America/Phoenix",
		MaxOnSec:  900,
		Sequencing: SequencingConfig{Mode: SequenceOverlap, OverlapMS: 50},
		Power:      StationConfig{ID: "psu", Title: "PSU", BCM: 21},
		Stations: []StationConfig{
			{ID: "front-west", Title: "Front West", BCM: 5},
			{ID: "front-north", Title: "Front North", BCM: 6},
			{ID: "front-south", Title: "Front South", BCM: 19},
		},
	}
}

func TestLoadConfigRequiresActiveLow(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "bad.json")
	if err := os.WriteFile(p, []byte(`{"chip":"gpiochip0","power":{"id":"psu","bcm":21},"stations":[{"id":"a","bcm":5}]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := LoadConfig(p); err == nil {
		t.Fatal("expected error for missing active_low")
	}
	p2 := filepath.Join(dir, "false.json")
	if err := os.WriteFile(p2, []byte(`{"chip":"gpiochip0","active_low":false,"power":{"id":"psu","bcm":21},"stations":[{"id":"a","bcm":5}]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := LoadConfig(p2); err == nil {
		t.Fatal("expected refuse active_low=false")
	}
}

func TestLoadExampleConfig(t *testing.T) {
	cfg, err := LoadConfig(filepath.Join("..", "..", "config", "pinmap.example.json"))
	if err != nil {
		t.Fatal(err)
	}
	if len(cfg.Stations) != 4 {
		t.Fatalf("stations=%d", len(cfg.Stations))
	}
}

func TestOverlapItineraryAndAllOff(t *testing.T) {
	cfg := testConfig(t)
	drv := gpio.NewFake()
	e, err := New(cfg, drv)
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()

	steps := []Step{
		{StationID: "front-west", Duration: 100 * time.Millisecond},
		{StationID: "front-north", Duration: 100 * time.Millisecond},
	}
	if err := e.RunItinerary(context.Background(), steps); err != nil {
		t.Fatal(err)
	}
	st := gpio.StateForTest(drv)
	if st["front-west"] != gpio.Off || st["front-north"] != gpio.Off || st["psu"] != gpio.Off {
		t.Fatalf("expected all off after run, got %#v", st)
	}
	if e.Status().Phase != PhaseIdle {
		t.Fatalf("phase=%s", e.Status().Phase)
	}
}

func TestRejectConfigWhileBusy(t *testing.T) {
	cfg := testConfig(t)
	e, err := New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() {
		done <- e.RunItinerary(ctx, []Step{{StationID: "front-west", Duration: 500 * time.Millisecond}})
	}()
	time.Sleep(20 * time.Millisecond)
	if err := e.ApplyConfig(cfg); err == nil {
		t.Fatal("expected busy on ApplyConfig")
	}
	cancel()
	<-done
}

func TestStopForcesOff(t *testing.T) {
	cfg := testConfig(t)
	drv := gpio.NewFake()
	e, err := New(cfg, drv)
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()

	go func() {
		_ = e.RunItinerary(context.Background(), []Step{{StationID: "front-west", Duration: 2 * time.Second}})
	}()
	time.Sleep(30 * time.Millisecond)
	if err := e.Stop(); err != nil {
		t.Fatal(err)
	}
	time.Sleep(50 * time.Millisecond)
	st := gpio.StateForTest(drv)
	if st["front-west"] != gpio.Off || st["psu"] != gpio.Off {
		t.Fatalf("expected off after Stop, got %#v", st)
	}
}

func TestBusySecondRun(t *testing.T) {
	cfg := testConfig(t)
	e, err := New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go func() { _ = e.RunItinerary(ctx, []Step{{StationID: "front-west", Duration: time.Second}}) }()
	time.Sleep(20 * time.Millisecond)
	if err := e.RunItinerary(context.Background(), []Step{{StationID: "front-north", Duration: time.Millisecond}}); err != ErrBusy {
		t.Fatalf("got %v want ErrBusy", err)
	}
	cancel()
}
