package schedule

import (
	"bytes"
	"context"
	"log"
	"path/filepath"
	"strings"
	"sync"
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

type muBuf struct {
	mu sync.Mutex
	b  bytes.Buffer
}

func (m *muBuf) Write(p []byte) (int, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.b.Write(p)
}

func (m *muBuf) String() string {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.b.String()
}

func (m *muBuf) Reset() {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.b.Reset()
}

type muClock struct {
	mu sync.Mutex
	t  time.Time
}

func (c *muClock) Now() time.Time {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.t
}

func (c *muClock) Set(t time.Time) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.t = t
}

func TestTickLogsStart(t *testing.T) {
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
		ID: "probe", Enabled: true, Weekdays: []string{"mon"}, Start: "06:00",
		Steps: []store.Step{{StationID: "front-west", Minutes: 1}},
	}
	if err := store.Save(path, store.File{Config: cfg, Schedules: []store.Schedule{sch}}); err != nil {
		t.Fatal(err)
	}
	var buf muBuf
	r := &Runner{
		Eng: e, Path: path, Loc: loc,
		Now: fixedClock{t: time.Date(2026, 9, 14, 6, 0, 0, 0, loc)},
		Log: log.New(&buf, "", 0), lastFired: map[string]string{},
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go func() { _ = r.Tick(ctx) }()
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if strings.Contains(buf.String(), "start probe at 06:00 Phoenix") {
			_ = e.Stop()
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	_ = e.Stop()
	t.Fatalf("missing start log: %s", buf.String())
}

func TestLoopStopsOnCancel(t *testing.T) {
	cfg := frontCfg()
	path := filepath.Join(t.TempDir(), "cfg.json")
	if err := store.Save(path, store.File{Config: cfg}); err != nil {
		t.Fatal(err)
	}
	e, err := engine.New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	r, err := NewRunner(e, path)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() {
		r.Loop(ctx, 20*time.Millisecond)
		close(done)
	}()
	time.Sleep(50 * time.Millisecond)
	cancel()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("Loop did not return after cancel")
	}
}

func TestTickSkipsWhenPaused(t *testing.T) {
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
	e.SetPause(nil, "rain")

	path := filepath.Join(t.TempDir(), "cfg.json")
	sch := store.Schedule{
		ID: "probe", Enabled: true, Weekdays: []string{"mon"}, Start: "06:00",
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
	if !strings.Contains(buf.String(), "paused") {
		t.Fatalf("want skip paused log, got %q", buf.String())
	}
	if e.Status().Phase != engine.PhaseIdle {
		t.Fatalf("must not water while paused: %s", e.Status().Phase)
	}
	if r.lastFired["probe"] == "" {
		t.Fatal("paused skip should consume today's slot")
	}
}

func TestTickSkipsEachOccurrenceDuringMultiDayPauseThenRuns(t *testing.T) {
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
		ID: "probe", Enabled: true, Start: "06:00",
		Steps: []store.Step{{StationID: "front-west", Minutes: 1}},
	}
	schedules := []store.Schedule{sch}
	if err := store.Save(path, store.File{Config: cfg, Schedules: schedules}); err != nil {
		t.Fatal(err)
	}

	// Paused Wed 2026-09-23 15:00 for 4 days → morning-anchored to Sun 2026-09-27 06:00.
	wed := time.Date(2026, 9, 23, 15, 0, 0, 0, loc)
	until := PauseUntilDays(schedules, wed, loc, 4)
	wantUntil := time.Date(2026, 9, 27, 6, 0, 0, 0, loc)
	if !until.Equal(wantUntil) {
		t.Fatalf("until %s want %s", until, wantUntil)
	}
	e.SetPause(&until, "rain")

	clock := &muClock{}
	var buf muBuf
	r := &Runner{
		Eng: e, Path: path, Loc: loc, Now: clock,
		Log: log.New(&buf, "", 0), lastFired: map[string]string{},
	}

	// Every 06:00 occurrence inside the pause (Thu, Fri, Sat) is skipped.
	for _, day := range []int{24, 25, 26} {
		buf.Reset()
		clock.Set(time.Date(2026, 9, day, 6, 0, 0, 0, loc))
		if err := r.Tick(context.Background()); err != nil {
			t.Fatal(err)
		}
		if !strings.Contains(buf.String(), "skip probe: paused") {
			t.Fatalf("Sep %d 06:00 want skip paused, log=%q", day, buf.String())
		}
		if strings.Contains(buf.String(), "start probe") {
			t.Fatalf("Sep %d must not start while paused, log=%q", day, buf.String())
		}
		if e.Status().Phase != engine.PhaseIdle {
			t.Fatalf("Sep %d must stay Idle, phase=%s", day, e.Status().Phase)
		}
	}

	buf.Reset()
	clock.Set(time.Date(2026, 9, 27, 5, 59, 0, 0, loc))
	if err := r.Tick(context.Background()); err != nil {
		t.Fatal(err)
	}
	if buf.String() != "" {
		t.Fatalf("Sun 05:59 want nothing, log=%q", buf.String())
	}

	buf.Reset()
	clock.Set(time.Date(2026, 9, 27, 6, 0, 0, 0, loc))
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go func() { _ = r.Tick(ctx) }()
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if strings.Contains(buf.String(), "start probe") {
			_ = e.Stop()
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	_ = e.Stop()
	t.Fatalf("Sun 06:00 missing start log: %s", buf.String())
}
