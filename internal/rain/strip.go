package rain

import "time"

// RainStripMinInches is the Home rain-strip floor. It is not a pause threshold.
const RainStripMinInches = 0.05

const (
	rainStripHours24 = 24
	rainStripHours72 = 72
)

// RainStrip is the Home rain strip. Inches is the true total for Hours,
// not capped at 1 inch. The phone meter caps the fill; this value does not.
// Hours is 24 or 72 when the totals are usable, and 0 when they are not.
type RainStrip struct {
	Show   bool    `json:"show"`
	Inches float64 `json:"inches"`
	Hours  int     `json:"hours"`
}

// HomeStrip decides the Home rain strip from already-summed totals.
// show is true only when enabled, the feed is not unavailable, ok is true,
// the 72 h total is at least RainStripMinInches, and rainPaused is false.
// Hours is 24 and Inches is the 24 h total when that total is at least
// RainStripMinInches; otherwise Hours is 72 and Inches is the 72 h total.
// Comparison uses hundredths, the same rounding as status JSON.
func HomeStrip(enabled, unavailable, rainPaused, ok bool, total24, total72 float64) RainStrip {
	if !ok {
		return RainStrip{}
	}
	t24 := RoundInches(total24)
	t72 := RoundInches(total72)
	hours := rainStripHours72
	inches := t72
	if atLeast(t24, RainStripMinInches) {
		hours = rainStripHours24
		inches = t24
	}
	show := enabled && !unavailable && !rainPaused && atLeast(t72, RainStripMinInches)
	return RainStrip{Show: show, Inches: inches, Hours: hours}
}

// sumPositiveSince totals increments strictly after now-d and at or before
// now+futureSkew. Non-positive inches are ignored.
func sumPositiveSince(samples []Sample, d time.Duration, now time.Time) float64 {
	start := now.Add(-d)
	end := now.Add(futureSkew)
	var total float64
	for _, s := range samples {
		if !s.Time.After(start) || s.Time.After(end) {
			continue
		}
		if s.Inches > 0 {
			total += s.Inches
		}
	}
	return total
}
