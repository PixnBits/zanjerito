package store

import (
	"context"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/gpio"
)

func validCfg() engine.Config {
	al := true
	return engine.Config{
		Chip:      "gpiochip0",
		ActiveLow: &al,
		Timezone:  "America/Phoenix",
		MaxOnSec:  900,
		Power:     engine.StationConfig{ID: "psu", BCM: 21},
		Stations: []engine.StationConfig{
			{ID: "front-north", Title: "Front North", BCM: 6},
		},
	}
}

func TestSaveLoadRoundTrip(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	in := File{
		Config:    validCfg(),
		Schedules: []Schedule{{ID: "dawn", Enabled: true, Note: "placeholder"}},
	}
	if err := Save(path, in); err != nil {
		t.Fatal(err)
	}
	out, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if out.Chip != in.Chip || out.Power.ID != "psu" || len(out.Stations) != 1 {
		t.Fatalf("pin map mismatch: %+v", out)
	}
	if out.Stations[0].ID != "front-north" || out.Stations[0].BCM != 6 {
		t.Fatalf("station: %+v", out.Stations[0])
	}
	if len(out.Schedules) != 1 || out.Schedules[0].ID != "dawn" || !out.Schedules[0].Enabled {
		t.Fatalf("schedules: %+v", out.Schedules)
	}
	if out.ActiveLow == nil || !*out.ActiveLow {
		t.Fatal("active_low lost")
	}
}

func TestLoadExamplePinmap(t *testing.T) {
	f, err := Load(filepath.Join("..", "..", "config", "pinmap.example.json"))
	if err != nil {
		t.Fatal(err)
	}
	if len(f.Stations) != 4 || f.Power.ID != "psu" {
		t.Fatalf("example: %+v", f)
	}
	path := filepath.Join(t.TempDir(), "round.json")
	if err := Save(path, f); err != nil {
		t.Fatal(err)
	}
	out, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if out.Stations[0].Color == "" || out.Power.Physical == 0 {
		t.Fatalf("pin-map extras dropped: %+v %+v", out.Stations[0], out.Power)
	}
}

func TestLoadRefusesActiveLow(t *testing.T) {
	dir := t.TempDir()
	p := filepath.Join(dir, "c.json")
	if err := os.WriteFile(p, []byte(`{"chip":"gpiochip0","power":{"id":"psu","bcm":21},"stations":[{"id":"a","bcm":5}]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := Load(p); err == nil {
		t.Fatal("expected missing active_low")
	}
	if err := os.WriteFile(p, []byte(`{"chip":"gpiochip0","active_low":false,"power":{"id":"psu","bcm":21},"stations":[{"id":"a","bcm":5}]}`), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := Load(p); err == nil {
		t.Fatal("expected refuse active_low=false")
	}
}

func TestSaveRefusesInvalid(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "c.json")
	if err := Save(path, File{Config: engine.Config{Chip: "gpiochip0"}}); err == nil {
		t.Fatal("expected validate fail")
	}
	if _, err := os.Stat(path); !os.IsNotExist(err) {
		t.Fatalf("should not create file on invalid save: %v", err)
	}
}

func TestAtomicRenameNoTempLeft(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	if err := Save(path, File{Config: validCfg()}); err != nil {
		t.Fatal(err)
	}
	ents, err := os.ReadDir(dir)
	if err != nil {
		t.Fatal(err)
	}
	if len(ents) != 1 || ents[0].Name() != "config.json" {
		t.Fatalf("leftover temps: %v", ents)
	}
	var raw map[string]any
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if err := json.Unmarshal(b, &raw); err != nil {
		t.Fatal(err)
	}
}

func TestApplyAndSaveRejectsBusy(t *testing.T) {
	cfg := validCfg()
	e, err := engine.New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() {
		done <- e.RunItinerary(ctx, []engine.Step{{StationID: "front-north", Duration: 500 * time.Millisecond}})
	}()
	time.Sleep(20 * time.Millisecond)

	path := filepath.Join(t.TempDir(), "cfg.json")
	if err := SaveConfig(path, cfg); err != nil {
		t.Fatal(err)
	}
	before, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}

	cfg2 := cfg
	cfg2.Timezone = "UTC"
	if err := ApplyAndSave(e, path, cfg2); err == nil {
		t.Fatal("expected busy on ApplyAndSave")
	} else if !errors.Is(err, engine.ErrBusy) {
		t.Fatalf("got %v want ErrBusy", err)
	}
	after, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if string(after) != string(before) {
		t.Fatal("persist must not write while busy")
	}
	cancel()
	<-done
}

func TestApplyAndSaveIdle(t *testing.T) {
	cfg := validCfg()
	e, err := engine.New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	path := filepath.Join(t.TempDir(), "cfg.json")
	cfg.Timezone = "UTC"
	if err := ApplyAndSave(e, path, cfg); err != nil {
		t.Fatal(err)
	}
	got, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if got.Timezone != "UTC" {
		t.Fatalf("tz %q", got.Timezone)
	}
}

func TestApplyAndSaveKeepsSchedules(t *testing.T) {
	cfg := validCfg()
	path := filepath.Join(t.TempDir(), "cfg.json")
	f := File{
		Config:    cfg,
		Schedules: []Schedule{{ID: "dawn", Enabled: true, Note: "keep me"}},
	}
	if err := Save(path, f); err != nil {
		t.Fatal(err)
	}
	e, err := engine.New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	cfg2 := validCfg()
	cfg2.Timezone = "UTC"
	cfg2.MaxOnSec = 0 // should persist engine default, not wipe schedules
	if err := ApplyAndSave(e, path, cfg2); err != nil {
		t.Fatal(err)
	}
	got, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if got.Timezone != "UTC" {
		t.Fatalf("tz %q", got.Timezone)
	}
	if got.MaxOnSec != 900 {
		t.Fatalf("max_on_sec want 900 (normalized), got %d", got.MaxOnSec)
	}
	if len(got.Schedules) != 1 || got.Schedules[0].ID != "dawn" || !got.Schedules[0].Enabled {
		t.Fatalf("schedules wiped: %+v", got.Schedules)
	}
}
