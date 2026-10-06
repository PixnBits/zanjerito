package engine

import "time"

const (
	// StepDone is a step already passed in the itinerary. In overlap mode the
	// previous station can still be energized for the overlap window after its
	// step is done.
	StepDone = "done"
	// StepActive is the step whose station was most recently turned on.
	StepActive = "active"
	// StepPending is a step that has not turned on yet.
	StepPending = "pending"
)

// RunStepSnap is one itinerary step at a moment in time.
// Planned, Elapsed, and Remaining are station-on durations, not wall gaps.
type RunStepSnap struct {
	StationID string
	Planned   time.Duration
	Elapsed   time.Duration
	Remaining time.Duration
	State     string
}

// RunProgressSnap is a copy of the in-flight itinerary.
// Mode is the sequencing mode ("overlap" or "isolate").
// StepIndex is -1 until the first station turns on.
type RunProgressSnap struct {
	Kind      string
	ProgramID string
	Program   string
	Mode      string
	Started   time.Time
	StepIndex int
	Steps     []RunStepSnap
}

// runStep is the planned itinerary kept for the life of one run.
type runStep struct {
	StationID string
	Planned   time.Duration
}

// runProgress lives under Engine.mu. It is not a second state machine.
type runProgress struct {
	active    bool
	kind      string
	programID string
	program   string
	mode      string
	steps     []runStep
	started   time.Time
	stepIndex int
	stepStart time.Time
}

// RunProgress copies the in-flight itinerary. The bool is false when no run
// is active, including after the run's cleanup.
//
// Overlap timing is approximate. The step index advances when the next
// station turns on, even while the previous station stays on for the overlap
// window. Remaining time is the current step's remaining planned duration
// plus the planned durations of later steps. It does not add the overlap
// window, and it does not subtract time the previous station is still
// energized. Isolate mode uses the same remaining formula (station-on time
// only); the power-down gap between stations is not included.
func (e *Engine) RunProgress(now time.Time) (RunProgressSnap, bool) {
	e.mu.Lock()
	defer e.mu.Unlock()
	if !e.prog.active {
		return RunProgressSnap{}, false
	}
	return progressSnap(e.prog, now), true
}

func (e *Engine) armProgress(programID, program, kind string, steps []Step, started time.Time) {
	plans := make([]runStep, len(steps))
	for i, s := range steps {
		plans[i] = runStep{StationID: s.StationID, Planned: s.Duration}
	}
	e.mu.Lock()
	e.prog = runProgress{
		active:    true,
		kind:      kind,
		programID: programID,
		program:   program,
		mode:      string(e.cfg.Sequencing.Mode),
		steps:     plans,
		started:   started,
		stepIndex: -1,
	}
	e.mu.Unlock()
}

// markStep records that step idx turned on. Called only after the relay write
// returns, and it takes e.mu itself so the caller does not hold the lock
// across the driver callback.
func (e *Engine) markStep(idx int) {
	e.mu.Lock()
	defer e.mu.Unlock()
	if !e.prog.active || idx < 0 || idx >= len(e.prog.steps) {
		return
	}
	e.prog.stepIndex = idx
	e.prog.stepStart = time.Now()
}

func progressSnap(p runProgress, now time.Time) RunProgressSnap {
	out := RunProgressSnap{
		Kind:      p.kind,
		ProgramID: p.programID,
		Program:   p.program,
		Mode:      p.mode,
		Started:   p.started,
		StepIndex: p.stepIndex,
		Steps:     make([]RunStepSnap, len(p.steps)),
	}
	for i, st := range p.steps {
		elapsed := time.Duration(0)
		remain := st.Planned
		state := StepPending
		switch {
		case p.stepIndex >= 0 && i < p.stepIndex:
			elapsed = st.Planned
			if elapsed < 0 {
				elapsed = 0
			}
			remain = 0
			state = StepDone
		case i == p.stepIndex:
			elapsed = now.Sub(p.stepStart)
			if elapsed < 0 {
				elapsed = 0
			}
			if elapsed > st.Planned {
				elapsed = st.Planned
			}
			remain = st.Planned - elapsed
			if remain < 0 {
				remain = 0
			}
			state = StepActive
		}
		out.Steps[i] = RunStepSnap{
			StationID: st.StationID,
			Planned:   st.Planned,
			Elapsed:   elapsed,
			Remaining: remain,
			State:     state,
		}
	}
	return out
}
