package schedule

import (
	"bytes"
	"context"
	"log"
	"path/filepath"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/gpio"
	"github.com/PixnBits/zanjerito/internal/store"
)

func rainCfg() engine.Config {
	al := true
	return engine.Config{
		Chip: "gpiochip0", ActiveLow: &al, Timezone: Phoenix, MaxOnSec: 900,
		Power: engine.StationConfig{ID: "psu", BCM: 21},
		Stations: []engine.StationConfig{
			{ID: "front-west", Title: "Front West", BCM: 5},
			{ID: "drip", Title: "Drip Line", BCM: 13, RainPauseExempt: true},
		},
	}
}

func TestTickAutoRainPauseRunsExemptOnly(t *testing.T) {
	loc, err := time.LoadLocation(Phoenix)
	if err != nil {
		t.Fatal(err)
	}
	cfg := rainCfg()
	drv := gpio.NewFake()
	e, err := engine.New(cfg, drv)
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	until := time.Date(2030, 1, 1, 0, 0, 0, 0, time.UTC)
	e.SetPauseMeta(&until, "rain", engine.PauseMeta{Source: engine.PauseSourceAuto, RainInches: 0.4})
	rec := &capRec{}
	e.SetRecorder(rec)

	path := filepath.Join(t.TempDir(), "cfg.json")
	sch := store.Schedule{
		ID: "probe", Enabled: true, Note: "Beds",
		Weekdays: []string{"mon"}, Start: "06:00",
		Steps: []store.Step{
			{StationID: "front-west", Minutes: 1},
			{StationID: "drip", Minutes: 1},
		},
	}
	if err := store.Save(path, store.File{Config: cfg, Schedules: []store.Schedule{sch}}); err != nil {
		t.Fatal(err)
	}
	var buf bytes.Buffer
	r := &Runner{
		Eng: e, Path: path, Loc: loc,
		Now:       fixedClock{t: time.Date(2026, 9, 14, 6, 0, 0, 0, loc)},
		Log:       log.New(&buf, "", 0),
		lastFired: map[string]string{},
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan struct{})
	go func() {
		_ = r.Tick(ctx)
		close(done)
	}()

	deadline := time.Now().Add(2 * time.Second)
	var sawOn, sawWest bool
	for time.Now().Before(deadline) {
		st := gpio.StateForTest(drv)
		if st["front-west"] == gpio.On {
			sawWest = true
		}
		if st["drip"] == gpio.On {
			sawOn = true
			break
		}
		time.Sleep(5 * time.Millisecond)
	}
	_ = e.Stop()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("tick did not return")
	}
	if sawWest {
		t.Fatal("front-west watered during auto rain pause")
	}
	if !sawOn {
		t.Fatalf("drip did not run, log %q", buf.String())
	}
	got := rec.snapshot()
	var skipped *engine.RunRecord
	for i := range got {
		if got[i].Outcome == engine.OutcomeSkipped {
			skipped = &got[i]
			break
		}
	}
	if skipped == nil {
		t.Fatalf("no skip record %+v log %q", got, buf.String())
	}
	if skipped.Reason != "rain" || skipped.ProgramID != "probe" {
		t.Fatalf("skip %+v", skipped)
	}
	if len(skipped.Stations) != 1 || skipped.Stations[0].StationID != "front-west" || skipped.Stations[0].ActualSec != 0 {
		t.Fatalf("skipped stations %+v", skipped.Stations)
	}
	for _, st := range skipped.Stations {
		if st.StationID == "drip" {
			t.Fatal("drip should not be in the skip record")
		}
	}
}

func TestTickManualPauseSkipsExempt(t *testing.T) {
	loc, err := time.LoadLocation(Phoenix)
	if err != nil {
		t.Fatal(err)
	}
	cfg := rainCfg()
	drv := gpio.NewFake()
	e, err := engine.New(cfg, drv)
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	e.SetPause(nil, "rain")
	rec := &capRec{}
	e.SetRecorder(rec)
	path := filepath.Join(t.TempDir(), "cfg.json")
	sch := store.Schedule{
		ID: "probe", Enabled: true, Note: "Beds",
		Weekdays: []string{"mon"}, Start: "06:00",
		Steps: []store.Step{
			{StationID: "front-west", Minutes: 1},
			{StationID: "drip", Minutes: 1},
		},
	}
	if err := store.Save(path, store.File{Config: cfg, Schedules: []store.Schedule{sch}}); err != nil {
		t.Fatal(err)
	}
	var buf bytes.Buffer
	r := &Runner{
		Eng: e, Path: path, Loc: loc,
		Now:       fixedClock{t: time.Date(2026, 9, 14, 6, 0, 0, 0, loc)},
		Log:       log.New(&buf, "", 0),
		lastFired: map[string]string{},
	}
	if err := r.Tick(context.Background()); err != nil {
		t.Fatal(err)
	}
	if gpio.StateForTest(drv)["drip"] == gpio.On || gpio.StateForTest(drv)["front-west"] == gpio.On {
		t.Fatal("manual pause watered a station")
	}
	got := rec.snapshot()
	if len(got) != 1 || got[0].Outcome != engine.OutcomeSkipped || got[0].Reason != "rain" {
		t.Fatalf("%+v", got)
	}
	ids := map[string]bool{}
	for _, st := range got[0].Stations {
		ids[st.StationID] = true
	}
	if !ids["drip"] || !ids["front-west"] {
		t.Fatalf("stations %+v", got[0].Stations)
	}
}

func TestTickAutoRainPauseSkipsProgramWithNoExempt(t *testing.T) {
	loc, err := time.LoadLocation(Phoenix)
	if err != nil {
		t.Fatal(err)
	}
	cfg := rainCfg()
	e, err := engine.New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	until := time.Date(2030, 1, 1, 0, 0, 0, 0, time.UTC)
	e.SetPauseMeta(&until, "rain", engine.PauseMeta{Source: engine.PauseSourceAuto})
	rec := &capRec{}
	e.SetRecorder(rec)
	path := filepath.Join(t.TempDir(), "cfg.json")
	sch := store.Schedule{
		ID: "probe", Enabled: true,
		Weekdays: []string{"mon"}, Start: "06:00",
		Steps: []store.Step{{StationID: "front-west", Minutes: 1}},
	}
	if err := store.Save(path, store.File{Config: cfg, Schedules: []store.Schedule{sch}}); err != nil {
		t.Fatal(err)
	}
	var buf bytes.Buffer
	r := &Runner{
		Eng: e, Path: path, Loc: loc,
		Now:       fixedClock{t: time.Date(2026, 9, 14, 6, 0, 0, 0, loc)},
		Log:       log.New(&buf, "", 0),
		lastFired: map[string]string{},
	}
	if err := r.Tick(context.Background()); err != nil {
		t.Fatal(err)
	}
	if e.Status().Phase != engine.PhaseIdle {
		t.Fatalf("phase %s", e.Status().Phase)
	}
	got := rec.snapshot()
	if len(got) != 1 || got[0].Reason != "rain" || len(got[0].Stations) != 1 || got[0].Stations[0].StationID != "front-west" {
		t.Fatalf("%+v", got)
	}
}
