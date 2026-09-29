// Package engine is the sole GPIO writer for zanjerito (docs/architecture.md).
package engine

import (
	"context"
	"errors"
	"fmt"
	"log"
	"sync"
	"sync/atomic"
	"time"

	"github.com/PixnBits/zanjerito/internal/gpio"
)

var (
	ErrBusy            = errors.New("engine: busy watering")
	ErrUnknownStation  = errors.New("engine: unknown station")
	ErrDurationTooLong = errors.New("engine: duration exceeds max_on_sec")
	ErrFaulted         = errors.New("engine: faulted")
	ErrPaused          = errors.New("engine: paused — watering held")
)

const (
	// PauseSourceManual is a user pause. Empty on-disk source loads as manual.
	PauseSourceManual = "manual"
	// PauseSourceAuto is an automatic rain pause.
	PauseSourceAuto = "auto"
)

// PauseMeta is optional rain context stored with a pause.
// Source empty is treated as manual.
type PauseMeta struct {
	Source      string
	RainInches  float64
	RainEventAt *time.Time
	LastRainAt  *time.Time
}

// PauseSnap is the pause after an optional expiry check.
// EventLastRain and RainClearedAt survive ClearPause and timed expiry.
type PauseSnap struct {
	Paused        bool
	Until         *time.Time
	Reason        string
	Source        string
	RainInches    float64
	RainEventAt   *time.Time
	LastRainAt    *time.Time
	RainClearedAt *time.Time
	EventLastRain *time.Time
}

// AutoRain reports an active automatic pause whose reason is rain.
func (p PauseSnap) AutoRain() bool {
	return p.Paused && p.Source == PauseSourceAuto && p.Reason == "rain"
}

// Phase is the high-level state machine.
type Phase string

const (
	PhaseIdle      Phase = "Idle"
	PhasePowerUp   Phase = "PowerUp"
	PhaseStationOn Phase = "StationOn"
	PhaseOverlap   Phase = "Overlap"
	PhasePowerDown Phase = "PowerDown"
	PhaseFault     Phase = "Fault"
)

// Step is one station in an itinerary.
type Step struct {
	StationID string
	Duration  time.Duration
}

// Status is a snapshot for API/UI later.
type Status struct {
	Phase          Phase
	CurrentStation string
	StationsOn     []string
	LastError      string
}

// Engine owns the driver and enforces safety invariants.
type Engine struct {
	mu      sync.Mutex
	cfg     Config
	drv     gpio.Driver
	phase   Phase
	on      map[string]bool // stations currently logically On (excl psu tracked separately)
	psuOn   bool
	lastErr error
	running bool
	cancel  context.CancelFunc

	// Webpage pause ("don't water for a while") — orthogonal to Phase; Idle while paused.
	paused      bool
	pausedUntil *time.Time // nil = until further notice when paused
	pauseReason string
	pauseSource string
	rainInches  float64
	rainEventAt *time.Time
	lastRainAt  *time.Time
	// rainClearedAt and eventLastRain outlive the active pause.
	rainClearedAt *time.Time
	eventLastRain *time.Time

	// rec is consulted without mu. Record runs only after running is cleared.
	rec atomic.Pointer[recorderSlot]
}

// New sets up GPIO lines from config. Driver must not be used elsewhere.
func New(cfg Config, drv gpio.Driver) (*Engine, error) {
	if err := cfg.NormalizeAndValidate(); err != nil {
		return nil, err
	}
	lines := make([]gpio.Line, 0, len(cfg.Stations)+1)
	lines = append(lines, gpio.Line{ID: cfg.Power.ID, BCM: cfg.Power.BCM})
	for _, s := range cfg.Stations {
		lines = append(lines, gpio.Line{ID: s.ID, BCM: s.BCM})
	}
	if err := drv.Setup(cfg.Chip, lines, true); err != nil {
		return nil, err
	}
	e := &Engine{
		cfg:   cfg,
		drv:   drv,
		phase: PhaseIdle,
		on:    make(map[string]bool),
	}
	if err := e.allOffLocked(); err != nil {
		_ = drv.Close()
		return nil, err
	}
	return e, nil
}

func (e *Engine) Status() Status {
	e.mu.Lock()
	defer e.mu.Unlock()
	st := Status{Phase: e.phase}
	if e.lastErr != nil {
		st.LastError = e.lastErr.Error()
	}
	for id, on := range e.on {
		if on {
			st.StationsOn = append(st.StationsOn, id)
			st.CurrentStation = id
		}
	}
	return st
}

// SetPause arms a manual watering hold. until==nil means until further notice.
// Does not Stop(); caller cancels any active run separately.
func (e *Engine) SetPause(until *time.Time, reason string) {
	e.SetPauseMeta(until, reason, PauseMeta{Source: PauseSourceManual})
}

// SetPauseMeta arms a hold with source and rain fields.
// Source empty is stored as manual. Does not Stop().
func (e *Engine) SetPauseMeta(until *time.Time, reason string, meta PauseMeta) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.setPauseLocked(until, reason, meta)
}

func (e *Engine) setPauseLocked(until *time.Time, reason string, meta PauseMeta) {
	e.paused = true
	if until != nil {
		u := until.UTC()
		e.pausedUntil = &u
	} else {
		e.pausedUntil = nil
	}
	e.pauseReason = reason
	src := meta.Source
	if src == "" {
		src = PauseSourceManual
	}
	e.pauseSource = src
	e.rainInches = meta.RainInches
	e.rainEventAt = cloneTime(meta.RainEventAt)
	e.lastRainAt = cloneTime(meta.LastRainAt)
	if src == PauseSourceAuto && meta.LastRainAt != nil {
		if e.eventLastRain == nil || meta.LastRainAt.After(*e.eventLastRain) {
			e.eventLastRain = cloneTime(meta.LastRainAt)
		}
	}
}

// ClearPause resumes watering (schedules + manual). Idempotent.
// Rain clear memory (rainClearedAt, eventLastRain) is kept; call NoteRainCleared
// to record a manual resume.
func (e *Engine) ClearPause() {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.clearActiveLocked()
}

// NoteRainCleared records a manual resume instant. It does not clear by itself.
func (e *Engine) NoteRainCleared(at time.Time) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.rainClearedAt = cloneTime(&at)
}

// NoteEventRain remembers the newest positive increment of an automatic event
// without changing the active pause. Used when an auto pause ends.
func (e *Engine) NoteEventRain(at time.Time) {
	e.mu.Lock()
	defer e.mu.Unlock()
	if at.IsZero() {
		return
	}
	if e.eventLastRain == nil || at.After(*e.eventLastRain) {
		e.eventLastRain = cloneTime(&at)
	}
}

// RestoreRainMemory installs clear/event memory from pause.json. Nil fields
// are left unchanged.
func (e *Engine) RestoreRainMemory(clearedAt, eventLast *time.Time) {
	e.mu.Lock()
	defer e.mu.Unlock()
	if clearedAt != nil {
		e.rainClearedAt = cloneTime(clearedAt)
	}
	if eventLast != nil && (e.eventLastRain == nil || eventLast.After(*e.eventLastRain)) {
		e.eventLastRain = cloneTime(eventLast)
	}
}

// IsPaused reports whether watering is held, auto-expiring timed pauses.
func (e *Engine) IsPaused(now time.Time) bool {
	return e.PauseDetail(now).Paused
}

func (e *Engine) isPausedLocked(now time.Time) bool {
	if !e.paused {
		return false
	}
	if e.pausedUntil != nil && !e.pausedUntil.After(now) {
		e.clearActiveLocked()
		return false
	}
	return true
}

func (e *Engine) clearActiveLocked() {
	e.paused = false
	e.pausedUntil = nil
	e.pauseReason = ""
	e.pauseSource = ""
	e.rainInches = 0
	e.rainEventAt = nil
	e.lastRainAt = nil
}

// PauseSnapshot returns pause fields after expire check.
func (e *Engine) PauseSnapshot(now time.Time) (paused bool, until *time.Time, reason string) {
	d := e.PauseDetail(now)
	if !d.Paused {
		return false, nil, ""
	}
	return true, d.Until, d.Reason
}

// PauseDetail returns pause fields after expire check.
func (e *Engine) PauseDetail(now time.Time) PauseSnap {
	e.mu.Lock()
	defer e.mu.Unlock()
	_ = e.isPausedLocked(now)
	return e.snapLocked()
}

// PauseRaw returns the pause without expiring a timed hold.
// The rain poller uses this so a stale fetch cannot clear via expiry.
func (e *Engine) PauseRaw() PauseSnap {
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.snapLocked()
}

func (e *Engine) snapLocked() PauseSnap {
	s := PauseSnap{
		Paused:        e.paused,
		Reason:        e.pauseReason,
		Source:        e.pauseSource,
		RainInches:    e.rainInches,
		RainEventAt:   cloneTime(e.rainEventAt),
		LastRainAt:    cloneTime(e.lastRainAt),
		RainClearedAt: cloneTime(e.rainClearedAt),
		EventLastRain: cloneTime(e.eventLastRain),
	}
	if e.pausedUntil != nil {
		s.Until = cloneTime(e.pausedUntil)
	}
	if s.Paused && s.Source == "" {
		s.Source = PauseSourceManual
	}
	return s
}

// StationRainExempt reports the station's rain_pause_exempt flag.
func (e *Engine) StationRainExempt(id string) bool {
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.stationExemptLocked(id)
}

func (e *Engine) stationExemptLocked(id string) bool {
	for _, s := range e.cfg.Stations {
		if s.ID == id {
			return s.RainPauseExempt
		}
	}
	return false
}

// exemptRunLocked is true only for an automatic rain pause whose every step
// is rain_pause_exempt. Manual pauses never allow a run.
func (e *Engine) exemptRunLocked(steps []Step) bool {
	if !e.paused || e.pauseSource != PauseSourceAuto || e.pauseReason != "rain" {
		return false
	}
	if len(steps) == 0 {
		return false
	}
	for _, s := range steps {
		if !e.stationExemptLocked(s.StationID) {
			return false
		}
	}
	return true
}

func cloneTime(t *time.Time) *time.Time {
	if t == nil || t.IsZero() {
		return nil
	}
	u := t.UTC()
	return &u
}

// ApplyConfig replaces config only when Idle (reject while watering).
func (e *Engine) ApplyConfig(cfg Config) error {
	if err := cfg.NormalizeAndValidate(); err != nil {
		return err
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.running || e.phase != PhaseIdle {
		return fmt.Errorf("%w: config writes deferred until Idle", ErrBusy)
	}
	e.cfg = cfg
	return nil
}

// ClearFault resets PhaseFault after operator ack; always all-off first.
func (e *Engine) ClearFault() error {
	e.mu.Lock()
	defer e.mu.Unlock()
	if err := e.allOffLocked(); err != nil {
		return err
	}
	e.lastErr = nil
	e.phase = PhaseIdle
	return nil
}

// Stop cancels an active run and forces all-off.
func (e *Engine) Stop() error {
	e.mu.Lock()
	if e.cancel != nil {
		e.cancel()
	}
	e.mu.Unlock()
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.allOffLocked()
}

// Close stops and closes the driver.
func (e *Engine) Close() error {
	_ = e.Stop()
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.drv.Close()
}

// RunItinerary is the manual watering entry (Kind manual, Program "manual").
// Sole caller of gpio Set for watering, together with RunProgram.
// Validation failures are not recorded.
func (e *Engine) RunItinerary(ctx context.Context, steps []Step) error {
	return e.runTagged(ctx, "", "manual", KindManual, steps)
}

// RunProgram is the schedule watering entry (Kind schedule).
func (e *Engine) RunProgram(ctx context.Context, programID, program string, steps []Step) error {
	return e.runTagged(ctx, programID, program, KindSchedule, steps)
}

func (e *Engine) runTagged(ctx context.Context, programID, program, kind string, steps []Step) error {
	e.mu.Lock()
	if e.running {
		e.mu.Unlock()
		return ErrBusy
	}
	if e.phase == PhaseFault {
		e.mu.Unlock()
		return ErrFaulted
	}
	if e.isPausedLocked(time.Now()) && !e.exemptRunLocked(steps) {
		e.mu.Unlock()
		return ErrPaused
	}
	for _, s := range steps {
		if !e.knownStation(s.StationID) {
			e.mu.Unlock()
			return fmt.Errorf("%w: %s", ErrUnknownStation, s.StationID)
		}
		if s.Duration <= 0 {
			e.mu.Unlock()
			return fmt.Errorf("engine: non-positive duration for %s", s.StationID)
		}
		if s.Duration > e.cfg.MaxOn() {
			e.mu.Unlock()
			return fmt.Errorf("%w: %s", ErrDurationTooLong, s.StationID)
		}
	}
	runCtx, cancel := context.WithCancel(ctx)
	e.cancel = cancel
	e.running = true
	e.lastErr = nil
	e.mu.Unlock()

	tim := newStationTimer(steps)
	var (
		rec    RunRecord
		record bool
	)
	// Emit only after this reset. A blocked Record must not keep the engine busy
	// or sit on mu (Stop never calls the recorder).
	defer func() {
		e.mu.Lock()
		e.running = false
		e.cancel = nil
		e.mu.Unlock()
		cancel()
		if record {
			e.emit(rec)
		}
	}()

	started := time.Now()
	err := e.run(runCtx, steps, tim)
	if err != nil {
		// Fail-safe: any exit including context.Canceled must all-off (CISO #5).
		// Stop() also cancels; all-off here is idempotent.
		if errors.Is(err, context.Canceled) || errors.Is(err, context.DeadlineExceeded) {
			e.mu.Lock()
			offErr := e.allOffLocked()
			e.phase = PhaseIdle
			e.mu.Unlock()
			end := time.Now()
			tim.closeOpen(end)
			outcome := OutcomeStopped
			errStr := ""
			if actuationRefused(e.drv) {
				outcome = OutcomeRefused
				errStr = "lockout driver: relays not energized"
			}
			rec = buildRecord(programID, program, kind, tim, started, end, outcome, errStr)
			if outcome == OutcomeRefused {
				for i := range rec.Stations {
					rec.Stations[i].ActualSec = 0
				}
			}
			record = true
			if offErr != nil {
				return errors.Join(err, offErr)
			}
			return err
		}
		_ = e.fault(err)
		end := time.Now()
		tim.closeOpen(end)
		rec = buildRecord(programID, program, kind, tim, started, end, OutcomeError, err.Error())
		record = true
		return err
	}
	e.mu.Lock()
	e.phase = PhaseIdle
	e.mu.Unlock()
	end := time.Now()
	tim.closeOpen(end)
	outcome := OutcomeCompleted
	errStr := ""
	if actuationRefused(e.drv) {
		outcome = OutcomeRefused
		errStr = "lockout driver: relays not energized"
	}
	rec = buildRecord(programID, program, kind, tim, started, end, outcome, errStr)
	if outcome == OutcomeRefused {
		for i := range rec.Stations {
			rec.Stations[i].ActualSec = 0
		}
	}
	record = true
	return nil
}

// actuationRefused reports a driver that accepts Set but does not energize.
// Used only to label the history record; phase timing, all-off, and locking
// are unchanged on both the completed and Stop/cancel paths.
func actuationRefused(drv gpio.Driver) bool {
	r, ok := drv.(gpio.Refuser)
	return ok && r.RefusesActuation()
}

func (e *Engine) run(ctx context.Context, steps []Step, tim *stationTimer) error {
	if len(steps) == 0 {
		return nil
	}
	mode := e.cfg.Sequencing.Mode
	if mode == SequenceIsolate {
		return e.runIsolate(ctx, steps, tim)
	}
	return e.runOverlap(ctx, steps, tim)
}

// turn is only called from the run goroutine. It updates tim after the relay write.
func (e *Engine) turn(id string, on bool, idx int, tim *stationTimer) error {
	if err := e.setStation(id, on); err != nil {
		return err
	}
	if tim == nil {
		return nil
	}
	now := time.Now()
	if on {
		tim.on(id, idx, now)
	} else {
		tim.off(id, now)
	}
	return nil
}

func (e *Engine) runOverlap(ctx context.Context, steps []Step, tim *stationTimer) error {
	overlap := time.Duration(e.cfg.Sequencing.OverlapMS) * time.Millisecond
	if overlap <= 0 {
		overlap = 2 * time.Second
	}

	if err := e.setPhase(PhasePowerUp); err != nil {
		return err
	}
	if err := e.setPSU(true); err != nil {
		return err
	}

	var prev string
	prevIdx := -1
	for i, step := range steps {
		dur := step.Duration
		ov := overlap
		if dur < ov {
			ov = dur
		}

		if err := e.setPhase(PhaseStationOn); err != nil {
			return err
		}
		if err := e.turn(step.StationID, true, i, tim); err != nil {
			return err
		}

		if prev != "" {
			// ≤2 ON: current + previous only during overlap window
			if err := e.setPhase(PhaseOverlap); err != nil {
				return err
			}
			if err := sleepCtx(ctx, ov); err != nil {
				return err
			}
			if err := e.turn(prev, false, prevIdx, tim); err != nil {
				return err
			}
			remain := dur - ov
			if remain > 0 {
				if err := e.setPhase(PhaseStationOn); err != nil {
					return err
				}
				if err := sleepCtx(ctx, remain); err != nil {
					return err
				}
			}
		} else {
			if err := sleepCtx(ctx, dur); err != nil {
				return err
			}
		}
		prev = step.StationID
		prevIdx = i
	}

	if prev != "" {
		if err := e.turn(prev, false, prevIdx, tim); err != nil {
			return err
		}
	}
	if err := e.setPhase(PhasePowerDown); err != nil {
		return err
	}
	if err := e.setPSU(false); err != nil {
		return err
	}
	return e.setPhase(PhaseIdle)
}

func (e *Engine) runIsolate(ctx context.Context, steps []Step, tim *stationTimer) error {
	for i, step := range steps {
		if err := e.setPhase(PhasePowerDown); err != nil {
			return err
		}
		if err := e.setPSU(false); err != nil {
			return err
		}
		if err := e.allStationsOff(); err != nil {
			return err
		}
		if err := e.setPhase(PhasePowerUp); err != nil {
			return err
		}
		if err := e.setPSU(true); err != nil {
			return err
		}
		if err := e.setPhase(PhaseStationOn); err != nil {
			return err
		}
		if err := e.turn(step.StationID, true, i, tim); err != nil {
			return err
		}
		if err := sleepCtx(ctx, step.Duration); err != nil {
			return err
		}
		if err := e.turn(step.StationID, false, i, tim); err != nil {
			return err
		}
		if err := e.setPSU(false); err != nil {
			return err
		}
	}
	return e.setPhase(PhaseIdle)
}

func (e *Engine) fault(cause error) error {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.phase = PhaseFault
	e.lastErr = cause
	log.Printf("engine: FAULT: %v — all-off", cause)
	if err := e.allOffLocked(); err != nil {
		return errors.Join(cause, err)
	}
	return cause
}

func (e *Engine) setPhase(p Phase) error {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.phase = p
	return nil
}

func (e *Engine) knownStation(id string) bool {
	for _, s := range e.cfg.Stations {
		if s.ID == id {
			return true
		}
	}
	return false
}

func (e *Engine) setPSU(on bool) error {
	e.mu.Lock()
	defer e.mu.Unlock()
	level := gpio.Off
	if on {
		level = gpio.On
	}
	if err := e.drv.Set(e.cfg.Power.ID, level); err != nil {
		return err
	}
	e.psuOn = on
	return nil
}

func (e *Engine) setStation(id string, on bool) error {
	e.mu.Lock()
	defer e.mu.Unlock()
	// Enforce ≤2 stations ON
	if on {
		count := 0
		for _, v := range e.on {
			if v {
				count++
			}
		}
		if !e.on[id] && count >= 2 {
			return fmt.Errorf("engine: overlap ceiling: would exceed 2 stations ON")
		}
	}
	level := gpio.Off
	if on {
		level = gpio.On
	}
	if err := e.drv.Set(id, level); err != nil {
		return err
	}
	e.on[id] = on
	return nil
}

func (e *Engine) allStationsOff() error {
	e.mu.Lock()
	defer e.mu.Unlock()
	for _, s := range e.cfg.Stations {
		if err := e.drv.Set(s.ID, gpio.Off); err != nil {
			return err
		}
		e.on[s.ID] = false
	}
	return nil
}

func (e *Engine) allOffLocked() error {
	for _, s := range e.cfg.Stations {
		if err := e.drv.Set(s.ID, gpio.Off); err != nil {
			return err
		}
		e.on[s.ID] = false
	}
	if err := e.drv.Set(e.cfg.Power.ID, gpio.Off); err != nil {
		return err
	}
	e.psuOn = false
	if e.phase != PhaseFault {
		e.phase = PhaseIdle
	}
	return nil
}

func sleepCtx(ctx context.Context, d time.Duration) error {
	t := time.NewTimer(d)
	defer t.Stop()
	select {
	case <-ctx.Done():
		return ctx.Err()
	case <-t.C:
		return nil
	}
}
