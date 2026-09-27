package rain

import (
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/store"
)

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
func RememberManualPause(eng *engine.Engine, configPath string, until *time.Time, reason string, now time.Time) error {
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
	return store.SavePause(configPath, store.PauseState{
		Active:        true,
		Until:         until,
		Reason:        reason,
		Source:        engine.PauseSourceManual,
		RainClearedAt: after.RainClearedAt,
		LastRainAt:    last,
		RainEventAt:   prev.RainEventAt,
	})
}

// RememberManualClear drops the watering hold and records rain_cleared_at.
// The same rain event will not re-arm an automatic pause.
func RememberManualClear(eng *engine.Engine, configPath string, now time.Time) error {
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
	return store.SavePause(configPath, store.PauseState{
		RainClearedAt: after.RainClearedAt,
		LastRainAt:    last,
		RainEventAt:   prev.RainEventAt,
	})
}
