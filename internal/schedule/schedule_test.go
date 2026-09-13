package schedule

import (
	"bytes"
	"context"
	"log"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/gpio"
	"github.com/PixnBits/zanjerito/internal/store"
)

type fixedClock struct{ t time.Time }

func (f fixedClock) Now() time.Time { return f.t }

func frontCfg() engine.Config {
	al := true
	return engine.Config{
		Chip:      "gpiochip0",
		ActiveLow: &al,
		Timezone:  Phoenix,
		MaxOnSec:  900,
		Power:     engine.StationConfig{ID: "psu", BCM: 21},
		Stations: []engine.StationConfig{
			{ID: "front-west", Title: "Front West", BCM: 5},
			{ID: "front-north", Title: "Front North", BCM: 6},
			{ID: "front-south", Title: "Front South", BCM: 19},
		},
	}
}

func TestFrontParityItinerary(t *testing.T) {
	steps, err := Itinerary(FrontParity())
	if err != nil {
		t.Fatal(err)
	}
	if len(steps) != 3 {
		t.Fatalf("len=%d", len(steps))
	}
	want := []struct {
		id  string
		min int
	}{
		{"front-west", 4},
		{"front-north", 8},
		{"front-south", 8},
	}
	for i, w := range want {
		if steps[i].StationID != w.id || steps[i].Duration != time.Duration(w.min)*time.Minute {
			t.Fatalf("%d: %+v want %s %dm", i, steps[i], w.id, w.min)
		}
	}
}

func TestDueUsesPhoenixWeekdayAndClock(t *testing.T) {
	loc, err := time.LoadLocation(Phoenix)
	if err != nil {
		t.Fatal(err)
	}
	// 2026-09-14 is a Monday 06:00 in Phoenix
	mon := time.Date(2026, 9, 14, 6, 0, 0, 0, loc)
	r := &Runner{Loc: loc, lastFired: map[string]string{}}
	sch := FrontParity()
	if !r.due(sch, mon) {
		t.Fatal("monday 06:00 should fire")
	}
	sch.Weekdays = []string{"wed"}
	if r.due(sch, mon) {
		t.Fatal("wednesday-only must not fire monday")
	}
	sch = FrontParity()
	sch.Start = "07:00"
	if r.due(sch, mon) {
		t.Fatal("07:00 must not fire at 06:00")
	}
	sch = FrontParity()
	sch.StartsOn = "2026-10-01"
	if r.due(sch, mon) {
		t.Fatal("before starts_on")
	}
}

func TestTickSkipsWhenBusy(t *testing.T) {
	loc, err := time.LoadLocation(Phoenix)
	if err != nil {
		t.Fatal(err)
	}
	cfg := frontCfg()
	e, err := engine.New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()

	path := filepath.Join(t.TempDir(), "cfg.json")
	sch := store.Schedule{
		ID:       "probe",
		Enabled:  true,
		Weekdays: []string{"mon"},
		Start:    "06:00",
		Steps:    []store.Step{{StationID: "front-west", Minutes: 1}},
	}
	if err := store.Save(path, store.File{Config: cfg, Schedules: []store.Schedule{sch}}); err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	errCh := make(chan error, 1)
	go func() {
		errCh <- e.RunItinerary(ctx, []engine.Step{{StationID: "front-north", Duration: 2 * time.Second}})
	}()
	time.Sleep(50 * time.Millisecond)

	var buf bytes.Buffer
	r := &Runner{
		Eng:       e,
		Path:      path,
		Loc:       loc,
		Now:       fixedClock{t: time.Date(2026, 9, 14, 6, 0, 0, 0, loc)},
		Log:       log.New(&buf, "", 0),
		lastFired: map[string]string{},
	}
	if err := r.Tick(context.Background()); err != nil {
		t.Fatal(err)
	}
	cancel()
	<-errCh
	if !strings.Contains(buf.String(), "busy") {
		t.Fatalf("want D13 busy skip, log=%q", buf.String())
	}
}
