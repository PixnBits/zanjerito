package rain

import (
	"math"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/store"
)

// RoundInches snaps to hundredths for status JSON and pause.json.
func RoundInches(v float64) float64 {
	return math.Round(v*100) / 100
}

// ApplyStoredPause restores a pause.json snapshot into the engine.
// An empty source is manual. Rain clear memory is restored even when inactive.
func ApplyStoredPause(eng *engine.Engine, ps store.PauseState) {
	if eng == nil {
		return
	}
	if ps.Active {
		src := ps.Source
		if src != engine.PauseSourceAuto {
			src = engine.PauseSourceManual
		}
		meta := engine.PauseMeta{Source: src}
		if src == engine.PauseSourceAuto {
			meta.RainInches = ps.RainInches
			meta.RainEventAt = ps.RainEventAt
			meta.LastRainAt = ps.LastRainAt
		}
		eng.SetPauseMeta(ps.Until, ps.Reason, meta)
	}
	eng.RestoreRainMemory(ps.RainClearedAt, ps.LastRainAt)
}

// RememberManualPause arms a user pause (source manual) and keeps rain memory.
// It writes pause.json itself. The poller path persists outside its lock instead.
func RememberManualPause(eng *engine.Engine, configPath string, until *time.Time, reason string, now time.Time) error {
	return store.SavePause(configPath, manualPauseState(eng, until, reason, now))
}

// RememberManualClear drops the watering hold and records rain_cleared_at.
// The same rain event will not re-arm an automatic pause.
// It writes pause.json itself. The poller path persists outside its lock instead.
func RememberManualClear(eng *engine.Engine, configPath string, now time.Time) error {
	return store.SavePause(configPath, manualClearState(eng, now))
}

func manualPauseState(eng *engine.Engine, until *time.Time, reason string, now time.Time) store.PauseState {
	prev := eng.PauseDetail(now)
	eng.SetPause(until, reason)
	after := eng.PauseDetail(now)
	last := prev.LastRainAt
	if last == nil {
		last = prev.EventLastRain
	}
	if after.EventLastRain != nil {
		last = after.EventLastRain
	}
	return store.PauseState{
		Active:        true,
		Until:         until,
		Reason:        reason,
		Source:        engine.PauseSourceManual,
		RainClearedAt: after.RainClearedAt,
		LastRainAt:    last,
		RainEventAt:   prev.RainEventAt,
	}
}

func manualClearState(eng *engine.Engine, now time.Time) store.PauseState {
	prev := eng.PauseDetail(now)
	eng.ClearPause()
	eng.NoteRainCleared(now)
	after := eng.PauseDetail(now)
	last := prev.LastRainAt
	if last == nil {
		last = prev.EventLastRain
	}
	if after.EventLastRain != nil {
		last = after.EventLastRain
	}
	return store.PauseState{
		RainClearedAt: after.RainClearedAt,
		LastRainAt:    last,
		RainEventAt:   prev.RainEventAt,
	}
}

// pauseStateFromEngine is the poller's pause/extend/clear snapshot.
// Rain inches are stored already rounded. Inactive holds keep event memory only.
func pauseStateFromEngine(eng *engine.Engine, now time.Time) store.PauseState {
	d := eng.PauseDetail(now)
	ps := store.PauseState{RainClearedAt: d.RainClearedAt}
	if d.Paused {
		ps.Active = true
		ps.Until = d.Until
		ps.Reason = d.Reason
		ps.Source = d.Source
		if ps.Source == "" {
			ps.Source = engine.PauseSourceManual
		}
		ps.RainInches = RoundInches(d.RainInches)
		ps.RainEventAt = d.RainEventAt
		ps.LastRainAt = d.LastRainAt
		return ps
	}
	ps.LastRainAt = d.EventLastRain
	return ps
}
