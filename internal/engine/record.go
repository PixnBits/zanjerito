package engine

import (
	"math"
	"time"
)

const (
	KindSchedule = "schedule"
	KindManual   = "manual"

	OutcomeCompleted = "completed"
	OutcomeStopped   = "stopped"
	OutcomeSkipped   = "skipped"
	OutcomeError     = "error"
)

// StationRun is one station's planned and actual on-time for a single step.
// PlannedSec is step duration truncated to whole seconds.
// ActualSec is measured on-time rounded to the nearest second.
type StationRun struct {
	StationID  string
	PlannedSec int
	ActualSec  int
}

// RunRecord is one household-visible run. It is not the on-disk shape;
// internal/history maps it to JSON.
type RunRecord struct {
	ProgramID string
	Program   string
	Kind      string // KindSchedule or KindManual
	Stations  []StationRun
	Start     time.Time
	End       time.Time
	Outcome   string // OutcomeCompleted, OutcomeStopped, OutcomeSkipped, or OutcomeError
	Error     string
	Reason    string
}

// RunRecorder accepts a finished or skipped run.
// Record must not block. The engine calls it without holding its lock,
// after relays are off and the run is no longer marked busy.
// Stop does not call Record.
type RunRecorder interface {
	Record(RunRecord)
}

type recorderSlot struct {
	r RunRecorder
}

// SetRecorder installs r. Nil clears it. Safe to call while idle or running;
// the value is loaded without the engine lock when a record is emitted.
func (e *Engine) SetRecorder(r RunRecorder) {
	if r == nil {
		e.rec.Store(nil)
		return
	}
	e.rec.Store(&recorderSlot{r: r})
}

func (e *Engine) emit(rec RunRecord) {
	slot := e.rec.Load()
	if slot == nil || slot.r == nil {
		return
	}
	slot.r.Record(rec)
}

// RecordSkipped emits a schedule skip (paused) without starting a run.
// Nil recorder is a no-op. Stations keep PlannedSec and ActualSec 0.
// A nil steps slice records no stations.
func (e *Engine) RecordSkipped(programID, program string, steps []Step, reason string, at time.Time) {
	stations := make([]StationRun, 0, len(steps))
	for _, s := range steps {
		stations = append(stations, StationRun{
			StationID:  s.StationID,
			PlannedSec: truncSec(s.Duration),
			ActualSec:  0,
		})
	}
	e.emit(RunRecord{
		ProgramID: programID,
		Program:   program,
		Kind:      KindSchedule,
		Stations:  stations,
		Start:     at,
		End:       at,
		Outcome:   OutcomeSkipped,
		Reason:    reason,
	})
}

func truncSec(d time.Duration) int {
	if d <= 0 {
		return 0
	}
	return int(d / time.Second)
}

func roundSec(d time.Duration) int {
	if d <= 0 {
		return 0
	}
	return int(math.Round(d.Seconds()))
}

// stationTimer is touched only by the run goroutine.
type stationTimer struct {
	rows []StationRun
	acc  []time.Duration
	open map[string]openSpan
}

type openSpan struct {
	idx int
	at  time.Time
}

func newStationTimer(steps []Step) *stationTimer {
	t := &stationTimer{
		rows: make([]StationRun, len(steps)),
		acc:  make([]time.Duration, len(steps)),
		open: make(map[string]openSpan),
	}
	for i, s := range steps {
		t.rows[i] = StationRun{StationID: s.StationID, PlannedSec: truncSec(s.Duration)}
	}
	return t
}

func (t *stationTimer) on(id string, idx int, at time.Time) {
	if t == nil {
		return
	}
	if sp, ok := t.open[id]; ok {
		t.add(sp, at)
		delete(t.open, id)
	}
	t.open[id] = openSpan{idx: idx, at: at}
}

func (t *stationTimer) off(id string, at time.Time) {
	if t == nil {
		return
	}
	sp, ok := t.open[id]
	if !ok {
		return
	}
	t.add(sp, at)
	delete(t.open, id)
}

func (t *stationTimer) closeOpen(at time.Time) {
	if t == nil {
		return
	}
	for id := range t.open {
		t.off(id, at)
	}
}

func (t *stationTimer) add(sp openSpan, at time.Time) {
	if sp.idx < 0 || sp.idx >= len(t.acc) {
		return
	}
	d := at.Sub(sp.at)
	if d > 0 {
		t.acc[sp.idx] += d
	}
}

func (t *stationTimer) stations() []StationRun {
	if t == nil {
		return []StationRun{}
	}
	out := make([]StationRun, len(t.rows))
	copy(out, t.rows)
	for i := range out {
		out[i].ActualSec = roundSec(t.acc[i])
	}
	return out
}

func buildRecord(programID, program, kind string, tim *stationTimer, start, end time.Time, outcome, errStr string) RunRecord {
	return RunRecord{
		ProgramID: programID,
		Program:   program,
		Kind:      kind,
		Stations:  tim.stations(),
		Start:     start,
		End:       end,
		Outcome:   outcome,
		Error:     errStr,
	}
}
