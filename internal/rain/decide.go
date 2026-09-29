package rain

import (
	"math"
	"time"
)

// futureSkew is how far ahead of now a sample may be and still count.
// Rows further ahead are ignored for staleness and for the window sum.
const futureSkew = 15 * time.Minute

// errImplausible is Decide's Error when a reading cannot be trusted.
const errImplausible = "implausible rain reading"

// errInvalidIncrement is Decide's Error when a sample increment is negative.
const errInvalidIncrement = "invalid rain increment"

// ActionKind is the poller's next step. Pause covers a new pause and an
// extension (Until is never earlier than the pause already in effect).
type ActionKind int

const (
	ActionNone ActionKind = iota
	ActionPause
	ActionClear
	ActionUnavailable
)

// Action is the pure result of Decide. Until, Inches, EventAt, and LastRain
// are set for ActionPause (and LastRain for ActionClear when known).
// Total is the in-window sum on a fresh read, including ActionNone.
type Action struct {
	Kind     ActionKind
	Until    time.Time
	Inches   float64
	EventAt  time.Time // first positive increment in the window (kept on extend)
	LastRain time.Time // newest positive increment in the window
	Total    float64
	Error    string
}

// PauseView is the current watering hold, without I/O.
// Source "auto" is an automatic rain pause; anything else active is manual
// (including an empty source). Until nil while Active is indefinite.
type PauseView struct {
	Active   bool
	Until    *time.Time
	Reason   string
	Source   string
	Inches   float64
	EventAt  *time.Time
	LastRain *time.Time
}

// Memory is what survives a clear. ClearedAt is the last manual Resume.
// LastEventRain is the newest positive increment of the last automatic event.
type Memory struct {
	ClearedAt     *time.Time
	LastEventRain *time.Time
}

// Decide chooses none, pause-or-extend, clear, or unavailable.
// Qualifying rain is an in-window increment sum at or above the trigger.
// A new event's newest positive increment must be after ClearedAt (when set)
// and after LastEventRain (when set). The sum still counts every in-window
// increment, including rain from before the clear.
// Stale, empty, all-future, implausible, or negative-increment input returns
// ActionUnavailable and must not pause, extend, or clear. Samples more than
// futureSkew ahead of now are ignored. Staleness uses the newest remaining sample.
func Decide(samples []Sample, now time.Time, cfg Config, pause PauseView, mem Memory) Action {
	cfg = cfg.normalized()
	if len(samples) == 0 {
		return Action{Kind: ActionUnavailable, Error: "no samples"}
	}
	if hasNegativeIncrement(samples) {
		return Action{Kind: ActionUnavailable, Error: errInvalidIncrement}
	}
	newest, haveNewest := newestUsable(samples, now)
	if !haveNewest {
		return Action{Kind: ActionUnavailable, Error: "no samples"}
	}
	stale := time.Duration(cfg.StaleHours * float64(time.Hour))
	if now.Sub(newest) > stale {
		return Action{Kind: ActionUnavailable, Error: "stale rain data"}
	}

	total, lastRain, firstRain, havePos := sumWindow(samples, now, cfg)
	if implausible(samples, now, cfg, total) {
		return Action{Kind: ActionUnavailable, Error: errImplausible}
	}
	base := Action{Total: total}
	qualifying := havePos && atLeast(total, cfg.TriggerInches)
	var until time.Time
	if havePos {
		days := cfg.DryDays
		if atLeast(total, cfg.HeavyInches) {
			days = cfg.HeavyDryDays
		}
		until = lastRain.Add(time.Duration(days * float64(24*time.Hour)))
	}

	// Timed automatic rain pause only. Manual (any reason), empty source,
	// and indefinite holds are left alone.
	auto := pause.Active && pause.Source == "auto" && pause.Reason == "rain" && pause.Until != nil
	if pause.Active && !auto {
		base.Kind = ActionNone
		return base
	}

	if auto {
		expired := !pause.Until.After(now)
		extends := qualifying && until.After(*pause.Until)
		if expired && !extends {
			base.Kind = ActionClear
			if havePos {
				base.LastRain = lastRain
				base.EventAt = firstRain
			}
			return base
		}
		if !qualifying {
			base.Kind = ActionNone
			return base
		}
		useUntil := *pause.Until
		if extends && until.After(now) {
			useUntil = until
		}
		inches := total
		if pause.Inches > inches {
			inches = pause.Inches
		}
		last := lastRain
		if pause.LastRain != nil && pause.LastRain.After(last) {
			last = *pause.LastRain
		}
		event := firstRain
		if pause.EventAt != nil {
			event = *pause.EventAt
		}
		changed := extends && until.After(*pause.Until)
		if inches > pause.Inches+1e-9 {
			changed = true
		}
		if pause.LastRain == nil || lastRain.After(*pause.LastRain) {
			changed = true
		}
		base.Until = useUntil
		base.Inches = inches
		base.LastRain = last
		base.EventAt = event
		if !changed || !useUntil.After(now) {
			if !useUntil.After(now) && expired {
				base.Kind = ActionClear
				return base
			}
			base.Kind = ActionNone
			return base
		}
		base.Kind = ActionPause
		return base
	}

	base.Kind = ActionNone
	if !qualifying || !havePos || !until.After(now) {
		return base
	}
	if mem.ClearedAt != nil && !lastRain.After(*mem.ClearedAt) {
		return base
	}
	if mem.LastEventRain != nil && !lastRain.After(*mem.LastEventRain) {
		return base
	}
	base.Kind = ActionPause
	base.Until = until
	base.Inches = total
	base.LastRain = lastRain
	base.EventAt = firstRain
	return base
}

func hasNegativeIncrement(samples []Sample) bool {
	for _, s := range samples {
		if s.Inches < 0 {
			return true
		}
	}
	return false
}

// newestUsable is the newest sample that is not more than futureSkew ahead.
func newestUsable(samples []Sample, now time.Time) (time.Time, bool) {
	limit := now.Add(futureSkew)
	var newest time.Time
	have := false
	for _, s := range samples {
		if s.Time.After(limit) {
			continue
		}
		if !have || s.Time.After(newest) {
			newest = s.Time
			have = true
		}
	}
	return newest, have
}

// inWindow reports whether s falls in (now-window, now+futureSkew].
func inWindow(s Sample, now time.Time, cfg Config) bool {
	start := now.Add(-time.Duration(cfg.WindowHours * float64(time.Hour)))
	end := now.Add(futureSkew)
	return s.Time.After(start) && !s.Time.After(end)
}

// implausible is true when any in-window increment, or the in-window total,
// is strictly above its cap in integer hundredths of an inch. Values equal
// to the cap are allowed (float64 sums of exact hundredths can sit just over
// the raw cap, e.g. 150×0.04).
func implausible(samples []Sample, now time.Time, cfg Config, total float64) bool {
	if hundredths(total) > hundredths(cfg.MaxWindowInches) {
		return true
	}
	for _, s := range samples {
		if !inWindow(s, now, cfg) {
			continue
		}
		inches := s.Inches
		if inches < 0 {
			inches = 0
		}
		if hundredths(inches) > hundredths(cfg.MaxIncrementInches) {
			return true
		}
	}
	return false
}

func hundredths(v float64) int64 {
	return int64(math.Round(v * 100))
}

// sumWindow totals increments in (now-window, now+futureSkew].
// last is the newest positive increment in that window; first is the oldest.
// Samples more than futureSkew ahead of now are excluded.
func sumWindow(samples []Sample, now time.Time, cfg Config) (total float64, last, first time.Time, havePos bool) {
	for _, s := range samples {
		if !inWindow(s, now, cfg) {
			continue
		}
		inches := s.Inches
		if inches < 0 {
			inches = 0
		}
		total += inches
		if inches > 0 {
			if !havePos || s.Time.After(last) {
				last = s.Time
			}
			if !havePos || s.Time.Before(first) {
				first = s.Time
			}
			havePos = true
		}
	}
	return total, last, first, havePos
}

// atLeast treats sub-nanometer float dust as equal so 0.25 compares cleanly,
// without promoting 0.999 up to 1.0.
func atLeast(total, threshold float64) bool {
	return total+1e-9 >= threshold
}
