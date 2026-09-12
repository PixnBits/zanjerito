// Package engine is the sole GPIO writer for zanjerito (docs/architecture.md).
package engine

import (
	"context"
	"errors"
	"fmt"
	"log"
	"sync"
	"time"

	"github.com/PixnBits/zanjerito/internal/gpio"
)

var (
	ErrBusy            = errors.New("engine: busy watering")
	ErrUnknownStation  = errors.New("engine: unknown station")
	ErrDurationTooLong = errors.New("engine: duration exceeds max_on_sec")
	ErrFaulted         = errors.New("engine: faulted")
)

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
	Phase         Phase
	CurrentStation string
	StationsOn    []string
	LastError     string
}

// Engine owns the driver and enforces safety invariants.
type Engine struct {
	mu     sync.Mutex
	cfg    Config
	drv    gpio.Driver
	phase  Phase
	on     map[string]bool // stations currently logically On (excl psu tracked separately)
	psuOn  bool
	lastErr error
	running bool
	cancel  context.CancelFunc
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

// RunItinerary executes steps with overlap or isolate sequencing.
// Sole caller of gpio Set for watering.
func (e *Engine) RunItinerary(ctx context.Context, steps []Step) error {
	e.mu.Lock()
	if e.running {
		e.mu.Unlock()
		return ErrBusy
	}
	if e.phase == PhaseFault {
		e.mu.Unlock()
		return ErrFaulted
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

	defer func() {
		e.mu.Lock()
		e.running = false
		e.cancel = nil
		e.mu.Unlock()
		cancel()
	}()

	err := e.run(runCtx, steps)
	if err != nil && !errors.Is(err, context.Canceled) {
		_ = e.fault(err)
		return err
	}
	e.mu.Lock()
	e.phase = PhaseIdle
	e.mu.Unlock()
	return err
}

func (e *Engine) run(ctx context.Context, steps []Step) error {
	if len(steps) == 0 {
		return nil
	}
	mode := e.cfg.Sequencing.Mode
	if mode == SequenceIsolate {
		return e.runIsolate(ctx, steps)
	}
	return e.runOverlap(ctx, steps)
}

func (e *Engine) runOverlap(ctx context.Context, steps []Step) error {
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
	for i, step := range steps {
		dur := step.Duration
		ov := overlap
		if dur < ov {
			ov = dur
		}

		if err := e.setPhase(PhaseStationOn); err != nil {
			return err
		}
		if err := e.setStation(step.StationID, true); err != nil {
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
			if err := e.setStation(prev, false); err != nil {
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
		_ = i
	}

	if prev != "" {
		if err := e.setStation(prev, false); err != nil {
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

func (e *Engine) runIsolate(ctx context.Context, steps []Step) error {
	for _, step := range steps {
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
		if err := e.setStation(step.StationID, true); err != nil {
			return err
		}
		if err := sleepCtx(ctx, step.Duration); err != nil {
			return err
		}
		if err := e.setStation(step.StationID, false); err != nil {
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
