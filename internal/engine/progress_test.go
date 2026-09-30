package engine

import (
	"context"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/gpio"
)

func progressCfg() Config {
	active := true
	return Config{
		Chip:       "gpiochip0",
		ActiveLow:  &active,
		Timezone:   "America/Phoenix",
		MaxOnSec:   900,
		Sequencing: SequencingConfig{Mode: SequenceOverlap, OverlapMS: 30},
		Power:      StationConfig{ID: "psu", Title: "Supply", BCM: 21},
		Stations: []StationConfig{
			{ID: "s1", Title: "Test Station 1", BCM: 5},
			{ID: "s2", Title: "Test Station 2", BCM: 6},
		},
	}
}

func TestProgressSnapRemainder(t *testing.T) {
	start := time.Date(2026, 9, 14, 6, 0, 0, 0, time.UTC)
	p := runProgress{
		active:    true,
		kind:      KindSchedule,
		programID: "a",
		program:   "Program A",
		mode:      string(SequenceOverlap),
		started:   start,
		stepIndex: 1,
		stepStart: start.Add(20 * time.Second),
		steps: []runStep{
			{StationID: "s1", Planned: 60 * time.Second},
			{StationID: "s2", Planned: 90 * time.Second},
			{StationID: "s1", Planned: 30 * time.Second},
		},
	}
	// 15s into step 1. Overlap does not add the previous station's leftover on-time.
	snap := progressSnap(p, start.Add(35*time.Second))
	if snap.StepIndex != 1 || snap.Steps[0].State != StepDone || snap.Steps[1].State != StepActive || snap.Steps[2].State != StepPending {
		t.Fatalf("states %+v", snap.Steps)
	}
	if snap.Steps[0].Remaining != 0 || snap.Steps[0].Elapsed != 60*time.Second {
		t.Fatalf("done step %+v", snap.Steps[0])
	}
	if snap.Steps[1].Elapsed != 15*time.Second || snap.Steps[1].Remaining != 75*time.Second {
		t.Fatalf("active %+v", snap.Steps[1])
	}
	if snap.Steps[2].Elapsed != 0 || snap.Steps[2].Remaining != 30*time.Second {
		t.Fatalf("pending %+v", snap.Steps[2])
	}
}

func TestRunProgressAdvancesAndClears(t *testing.T) {
	e, err := New(progressCfg(), gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()

	if _, ok := e.RunProgress(time.Now()); ok {
		t.Fatal("idle progress")
	}

	stopHammer := make(chan struct{})
	go func() {
		for {
			select {
			case <-stopHammer:
				return
			default:
				_, _ = e.RunProgress(time.Now())
				_ = e.Status()
			}
		}
	}()
	defer close(stopHammer)

	done := make(chan error, 1)
	go func() {
		done <- e.RunItinerary(context.Background(), []Step{
			{StationID: "s1", Duration: 180 * time.Millisecond},
			{StationID: "s2", Duration: 180 * time.Millisecond},
		})
	}()

	seen0, seen1 := false, false
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) && (!seen0 || !seen1) {
		snap, ok := e.RunProgress(time.Now())
		if ok && snap.Kind == KindManual && snap.Program == "manual" && snap.Mode == string(SequenceOverlap) {
			if snap.StepIndex == 0 && len(snap.Steps) == 2 && snap.Steps[0].State == StepActive && snap.Steps[1].State == StepPending {
				seen0 = true
			}
			if snap.StepIndex == 1 && snap.Steps[0].State == StepDone && snap.Steps[1].State == StepActive && snap.Steps[1].StationID == "s2" {
				seen1 = true
			}
		}
		time.Sleep(10 * time.Millisecond)
	}
	if !seen0 || !seen1 {
		t.Fatalf("seen step0=%v step1=%v", seen0, seen1)
	}

	select {
	case err := <-done:
		if err != nil {
			t.Fatal(err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("run did not finish")
	}
	deadline = time.Now().Add(time.Second)
	for {
		if _, ok := e.RunProgress(time.Now()); !ok && e.Status().Phase == PhaseIdle {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("progress still set after the run")
		}
		time.Sleep(10 * time.Millisecond)
	}
}
