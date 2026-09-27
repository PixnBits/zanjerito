package schedule

import (
	"context"
	"log"
	"path/filepath"
	"sync"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/gpio"
	"github.com/PixnBits/zanjerito/internal/store"
)

type capRec struct {
	mu  sync.Mutex
	got []engine.RunRecord
}

func (c *capRec) Record(r engine.RunRecord) {
	c.mu.Lock()
	c.got = append(c.got, r)
	c.mu.Unlock()
}

func (c *capRec) snapshot() []engine.RunRecord {
	c.mu.Lock()
	defer c.mu.Unlock()
	return append([]engine.RunRecord(nil), c.got...)
}

func TestTickPausedRecordsSkipOnce(t *testing.T) {
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
	rec := &capRec{}
	e.SetRecorder(rec)

	path := filepath.Join(t.TempDir(), "cfg.json")
	sch := store.Schedule{
		ID: "probe", Enabled: true, Note: "Morning front",
		Weekdays: []string{"mon"}, Start: "06:00",
		Steps: []store.Step{{StationID: "front-west", Minutes: 1}},
	}
	if err := store.Save(path, store.File{Config: cfg, Schedules: []store.Schedule{sch}}); err != nil {
		t.Fatal(err)
	}
	r := &Runner{
		Eng: e, Path: path, Loc: loc,
		Now:       fixedClock{t: time.Date(2026, 9, 14, 6, 0, 0, 0, loc)},
		Log:       log.New(&bytesBuffer{}, "", 0),
		lastFired: map[string]string{},
	}
	if err := r.Tick(context.Background()); err != nil {
		t.Fatal(err)
	}
	if err := r.Tick(context.Background()); err != nil {
		t.Fatal(err)
	}
	got := rec.snapshot()
	if len(got) != 1 {
		t.Fatalf("want exactly one skip, got %d (%+v)", len(got), got)
	}
	g := got[0]
	if g.Outcome != engine.OutcomeSkipped || g.Kind != engine.KindSchedule {
		t.Fatalf("outcome=%s kind=%s", g.Outcome, g.Kind)
	}
	if g.ProgramID != "probe" || g.Program != "Morning front" || g.Reason != "rain" {
		t.Fatalf("program id=%q name=%q reason=%q", g.ProgramID, g.Program, g.Reason)
	}
	if len(g.Stations) != 1 || g.Stations[0].StationID != "front-west" || g.Stations[0].PlannedSec != 60 || g.Stations[0].ActualSec != 0 {
		t.Fatalf("stations %+v", g.Stations)
	}
	if e.Status().Phase != engine.PhaseIdle {
		t.Fatalf("phase %s", e.Status().Phase)
	}
}

func TestTickPausedBadItineraryRecordsNoStations(t *testing.T) {
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
	rec := &capRec{}
	e.SetRecorder(rec)

	path := filepath.Join(t.TempDir(), "cfg.json")
	sch := store.Schedule{
		ID: "probe", Enabled: true, Note: "Morning front",
		Weekdays: []string{"mon"}, Start: "06:00",
	}
	if err := store.Save(path, store.File{Config: cfg, Schedules: []store.Schedule{sch}}); err != nil {
		t.Fatal(err)
	}
	r := &Runner{
		Eng: e, Path: path, Loc: loc,
		Now:       fixedClock{t: time.Date(2026, 9, 14, 6, 0, 0, 0, loc)},
		Log:       log.New(&bytesBuffer{}, "", 0),
		lastFired: map[string]string{},
	}
	if err := r.Tick(context.Background()); err != nil {
		t.Fatal(err)
	}
	got := rec.snapshot()
	if len(got) != 1 {
		t.Fatalf("want one skip, got %+v", got)
	}
	if len(got[0].Stations) != 0 || got[0].Reason != "rain" || got[0].Program != "Morning front" {
		t.Fatalf("%+v", got[0])
	}
}

func TestTickRunProgramRecordsSchedule(t *testing.T) {
	for _, tc := range []struct {
		note string
		want string
	}{
		{note: "Dawn soak", want: "Dawn soak"},
		{note: "", want: "probe"},
	} {
		t.Run(tc.want, func(t *testing.T) {
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
			rec := &capRec{}
			e.SetRecorder(rec)

			path := filepath.Join(t.TempDir(), "cfg.json")
			sch := store.Schedule{
				ID: "probe", Enabled: true, Note: tc.note,
				Weekdays: []string{"mon"}, Start: "06:00",
				Steps: []store.Step{{StationID: "front-west", Minutes: 1}},
			}
			if err := store.Save(path, store.File{Config: cfg, Schedules: []store.Schedule{sch}}); err != nil {
				t.Fatal(err)
			}
			var buf muBuf
			r := &Runner{
				Eng: e, Path: path, Loc: loc,
				Now:       fixedClock{t: time.Date(2026, 9, 14, 6, 0, 0, 0, loc)},
				Log:       log.New(&buf, "", 0),
				lastFired: map[string]string{},
			}
			ctx, cancel := context.WithCancel(context.Background())
			defer cancel()
			go func() { _ = r.Tick(ctx) }()

			deadline := time.Now().Add(2 * time.Second)
			for time.Now().Before(deadline) {
				if e.Status().Phase != engine.PhaseIdle {
					break
				}
				time.Sleep(5 * time.Millisecond)
			}
			if e.Status().Phase == engine.PhaseIdle {
				t.Fatalf("run did not start: %s", buf.String())
			}
			if err := e.Stop(); err != nil {
				t.Fatal(err)
			}
			deadline = time.Now().Add(2 * time.Second)
			var got []engine.RunRecord
			for time.Now().Before(deadline) {
				got = rec.snapshot()
				if len(got) > 0 {
					break
				}
				time.Sleep(5 * time.Millisecond)
			}
			if len(got) != 1 {
				t.Fatalf("want one schedule record, got %+v log=%s", got, buf.String())
			}
			if got[0].Kind != engine.KindSchedule || got[0].Program != tc.want || got[0].ProgramID != "probe" {
				t.Fatalf("kind=%s program=%q id=%q", got[0].Kind, got[0].Program, got[0].ProgramID)
			}
		})
	}
}

// bytesBuffer is a Logger sink that does not need to be read.
type bytesBuffer struct{}

func (bytesBuffer) Write(p []byte) (int, error) { return len(p), nil }
