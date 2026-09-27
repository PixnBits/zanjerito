package rain

import (
	"strings"
	"testing"
	"time"
)

func testPolicy() Config {
	return Config{
		TriggerInches:      0.25,
		WindowHours:        24,
		DryDays:            2,
		HeavyInches:        1,
		HeavyDryDays:       4,
		StaleHours:         7,
		MaxIncrementInches: 2,
		MaxWindowInches:    6,
	}
}

func TestDecideThresholdWindowAndTiers(t *testing.T) {
	now := time.Date(2026, 9, 26, 18, 0, 0, 0, time.UTC)
	cfg := testPolicy()
	rainAt := now.Add(-time.Hour)

	low := Decide([]Sample{{Time: rainAt, Inches: 0.24}, {Time: now, Inches: 0}}, now, cfg, PauseView{}, Memory{})
	if low.Kind != ActionNone {
		t.Fatalf("0.24 kind %v", low.Kind)
	}

	hit := Decide([]Sample{{Time: rainAt, Inches: 0.25}, {Time: now, Inches: 0}}, now, cfg, PauseView{}, Memory{})
	if hit.Kind != ActionPause || !hit.Until.Equal(rainAt.Add(48*time.Hour)) || hit.Inches != 0.25 {
		t.Fatalf("0.25 %+v", hit)
	}

	heavy := Decide([]Sample{{Time: rainAt, Inches: 1}, {Time: now, Inches: 0}}, now, cfg, PauseView{}, Memory{})
	if heavy.Kind != ActionPause || !heavy.Until.Equal(rainAt.Add(96*time.Hour)) {
		t.Fatalf("heavy %+v", heavy)
	}
	below := Decide([]Sample{{Time: rainAt, Inches: 0.99}, {Time: now, Inches: 0}}, now, cfg, PauseView{}, Memory{})
	if !below.Until.Equal(rainAt.Add(48 * time.Hour)) {
		t.Fatalf("0.99 until %s", below.Until)
	}

	// (now-window, now]: exactly the start is out; one nanosecond later is in.
	start := now.Add(-24 * time.Hour)
	edge := Decide([]Sample{{Time: start, Inches: 0.5}, {Time: now, Inches: 0}}, now, cfg, PauseView{}, Memory{})
	if edge.Kind != ActionNone {
		t.Fatalf("boundary sample should be excluded, %+v", edge)
	}
	in := Decide([]Sample{{Time: start.Add(time.Nanosecond), Inches: 0.5}, {Time: now, Inches: 0}}, now, cfg, PauseView{}, Memory{})
	if in.Kind != ActionPause {
		t.Fatalf("just inside window should count, %+v", in)
	}
}

func TestDecideManualClearAndNewRain(t *testing.T) {
	now := time.Date(2026, 9, 26, 18, 0, 0, 0, time.UTC)
	cfg := testPolicy()
	old := now.Add(-2 * time.Hour)
	cleared := now.Add(-time.Minute)
	samples := []Sample{{Time: old, Inches: 0.30}, {Time: now, Inches: 0}}
	got := Decide(samples, now, cfg, PauseView{}, Memory{ClearedAt: &cleared, LastEventRain: &old})
	if got.Kind != ActionNone {
		t.Fatalf("same event after clear: %+v", got)
	}
	// New tip after the clear. The pre-clear rain still counts toward 0.25.
	tip := now.Add(-30 * time.Second)
	samples = []Sample{{Time: old, Inches: 0.20}, {Time: tip, Inches: 0.05}, {Time: now, Inches: 0}}
	got = Decide(samples, now, cfg, PauseView{}, Memory{ClearedAt: &cleared, LastEventRain: &old})
	if got.Kind != ActionPause || !got.LastRain.Equal(tip) || got.Inches < 0.25-1e-9 {
		t.Fatalf("new rain should re-trigger, %+v", got)
	}
	// 0.10 + 0.10 stays under the trigger even though the tip is new.
	samples = []Sample{{Time: old, Inches: 0.10}, {Time: tip, Inches: 0.10}, {Time: now, Inches: 0}}
	got = Decide(samples, now, cfg, PauseView{}, Memory{ClearedAt: &cleared})
	if got.Kind != ActionNone {
		t.Fatalf("under threshold after clear: %+v", got)
	}
}

func TestDecideDoesNotShortenOrOverride(t *testing.T) {
	now := time.Date(2026, 9, 26, 18, 0, 0, 0, time.UTC)
	cfg := testPolicy()
	last := now.Add(-time.Hour)
	long := last.Add(10 * 24 * time.Hour)
	samples := []Sample{{Time: last, Inches: 0.5}, {Time: now, Inches: 0}}

	manual := PauseView{Active: true, Until: &long, Reason: "rain", Source: "manual"}
	if got := Decide(samples, now, cfg, manual, Memory{}); got.Kind != ActionNone {
		t.Fatalf("manual %+v", got)
	}
	indef := PauseView{Active: true, Reason: "rain", Source: "manual"}
	if got := Decide(samples, now, cfg, indef, Memory{}); got.Kind != ActionNone {
		t.Fatalf("indefinite %+v", got)
	}
	mowUntil := now.Add(3 * time.Hour)
	mow := PauseView{Active: true, Until: &mowUntil, Reason: "mow", Source: "manual"}
	if got := Decide(samples, now, cfg, mow, Memory{}); got.Kind != ActionNone {
		t.Fatalf("mow %+v", got)
	}

	// Heavy hold, then the total drops below heavy with the same last rain.
	heavyUntil := last.Add(96 * time.Hour)
	auto := PauseView{
		Active: true, Until: &heavyUntil, Reason: "rain", Source: "auto",
		Inches: 1.1, LastRain: &last, EventAt: &last,
	}
	light := []Sample{{Time: last, Inches: 0.30}, {Time: now, Inches: 0}}
	got := Decide(light, now, cfg, auto, Memory{})
	if got.Kind != ActionNone {
		t.Fatalf("must not shorten, %+v", got)
	}

	// Newer rain extends and the amount keeps the max.
	newer := now.Add(-10 * time.Minute)
	ext := Decide([]Sample{{Time: last, Inches: 0.4}, {Time: newer, Inches: 0.2}, {Time: now, Inches: 0}}, now, cfg, PauseView{
		Active: true, Until: &heavyUntil, Reason: "rain", Source: "auto",
		Inches: 1.1, LastRain: &last, EventAt: &last,
	}, Memory{})
	// 0.6 is under heavy, new until is newer+2d which may be before heavyUntil.
	// Amount stays 1.1; until must not move earlier. Last rain updates, so this is a pause action.
	if ext.Kind != ActionPause {
		t.Fatalf("extend kind %+v", ext)
	}
	if ext.Until.Before(heavyUntil) {
		t.Fatalf("shortened until %s < %s", ext.Until, heavyUntil)
	}
	if ext.Inches < 1.1-1e-9 {
		t.Fatalf("inches %v", ext.Inches)
	}
	if !ext.LastRain.Equal(newer) {
		t.Fatalf("last rain %s", ext.LastRain)
	}
}

func TestDecideStaleAndClear(t *testing.T) {
	now := time.Date(2026, 9, 26, 18, 0, 0, 0, time.UTC)
	cfg := testPolicy()
	old := now.Add(-8 * time.Hour)
	got := Decide([]Sample{{Time: old, Inches: 1}}, now, cfg, PauseView{}, Memory{})
	if got.Kind != ActionUnavailable || got.Error == "" {
		t.Fatalf("stale %+v", got)
	}
	until := now.Add(-time.Minute)
	last := now.Add(-48 * time.Hour)
	auto := PauseView{Active: true, Until: &until, Reason: "rain", Source: "auto", LastRain: &last, Inches: 0.3}
	fresh := []Sample{{Time: last, Inches: 0.3}, {Time: now, Inches: 0}}
	got = Decide(fresh, now, cfg, auto, Memory{})
	if got.Kind != ActionClear {
		t.Fatalf("dry-out %+v", got)
	}
}

func TestDecideEmpty(t *testing.T) {
	got := Decide(nil, time.Now(), testPolicy(), PauseView{}, Memory{})
	if got.Kind != ActionUnavailable {
		t.Fatalf("%+v", got)
	}
}

func TestDecidePlausibility(t *testing.T) {
	now := time.Date(2026, 9, 26, 18, 0, 0, 0, time.UTC)
	cfg := testPolicy()
	last := now.Add(-time.Hour)
	until := now.Add(48 * time.Hour)
	auto := PauseView{
		Active: true, Until: &until, Reason: "rain", Source: "auto",
		Inches: 0.4, LastRain: &last, EventAt: &last,
	}

	boom := Decide([]Sample{{Time: now.Add(-time.Minute), Inches: 99.99}, {Time: now, Inches: 0}}, now, cfg, PauseView{}, Memory{})
	if boom.Kind != ActionUnavailable || boom.Error != errImplausible {
		t.Fatalf("99.99 %+v", boom)
	}
	// Defaults apply when the caps are left at zero.
	bare := Config{TriggerInches: 0.25, WindowHours: 24, StaleHours: 7}
	if got := Decide([]Sample{{Time: now, Inches: 99.99}}, now, bare, PauseView{}, Memory{}); got.Kind != ActionUnavailable || got.Error != errImplausible {
		t.Fatalf("default caps %+v", got)
	}
	held := Decide([]Sample{{Time: now.Add(-time.Minute), Inches: 99.99}}, now, cfg, auto, Memory{})
	if held.Kind != ActionUnavailable {
		t.Fatalf("must not extend %+v", held)
	}
	past := now.Add(-time.Minute)
	expired := auto
	expired.Until = &past
	if got := Decide([]Sample{{Time: now, Inches: 99.99}}, now, cfg, expired, Memory{}); got.Kind != ActionUnavailable {
		t.Fatalf("must not clear %+v", got)
	}

	sum := []Sample{
		{Time: now.Add(-4 * time.Hour), Inches: 1.9},
		{Time: now.Add(-3 * time.Hour), Inches: 1.9},
		{Time: now.Add(-2 * time.Hour), Inches: 1.9},
		{Time: now.Add(-time.Hour), Inches: 1.9},
		{Time: now, Inches: 0},
	}
	if got := Decide(sum, now, cfg, PauseView{}, Memory{}); got.Kind != ActionUnavailable || got.Error != errImplausible {
		t.Fatalf("window cap %+v", got)
	}
	if got := Decide(sum, now, cfg, auto, Memory{}); got.Kind != ActionUnavailable {
		t.Fatalf("window cap changed pause %+v", got)
	}

	exactInc := Decide([]Sample{{Time: last, Inches: 2}, {Time: now, Inches: 0}}, now, cfg, PauseView{}, Memory{})
	if exactInc.Kind != ActionPause || exactInc.Inches != 2 {
		t.Fatalf("increment cap should allow 2.0 %+v", exactInc)
	}
	exactWin := Decide([]Sample{
		{Time: now.Add(-3 * time.Hour), Inches: 2},
		{Time: now.Add(-2 * time.Hour), Inches: 2},
		{Time: now.Add(-time.Hour), Inches: 2},
		{Time: now, Inches: 0},
	}, now, cfg, PauseView{}, Memory{})
	if exactWin.Kind != ActionPause || exactWin.Total != 6 {
		t.Fatalf("window cap should allow 6.0 %+v", exactWin)
	}

	// A glitch outside the window is not an in-window reading.
	old := Decide([]Sample{{Time: now.Add(-48 * time.Hour), Inches: 99}, {Time: now, Inches: 0}}, now, cfg, PauseView{}, Memory{})
	if old.Kind != ActionNone {
		t.Fatalf("old glitch %+v", old)
	}
}

func TestDecidePlausibilityHundredths(t *testing.T) {
	now := time.Date(2026, 9, 26, 18, 0, 0, 0, time.UTC)
	cfg := testPolicy()

	tips := func(n int, inches float64) []Sample {
		s := make([]Sample, n+1)
		for i := 0; i < n; i++ {
			s[i] = Sample{Time: now.Add(-time.Duration(i+1) * time.Minute), Inches: inches}
		}
		s[n] = Sample{Time: now, Inches: 0}
		return s
	}

	got := Decide(tips(150, 0.04), now, cfg, PauseView{}, Memory{})
	if got.Kind == ActionUnavailable {
		t.Fatalf("150×0.04 should be plausible, %+v", got)
	}
	if got.Kind != ActionPause {
		t.Fatalf("150×0.04 should pause from unpaused, %+v", got)
	}

	got = Decide(tips(30, 0.20), now, cfg, PauseView{}, Memory{})
	if got.Kind == ActionUnavailable {
		t.Fatalf("30×0.20 should be plausible, %+v", got)
	}
	if got.Kind != ActionPause {
		t.Fatalf("30×0.20 should pause, %+v", got)
	}

	over := tips(150, 0.04)
	over = append(over, Sample{Time: now.Add(-151 * time.Minute), Inches: 0.01})
	got = Decide(over, now, cfg, PauseView{}, Memory{})
	if got.Kind != ActionUnavailable || got.Error != errImplausible {
		t.Fatalf("6.01 window %+v", got)
	}

	got = Decide([]Sample{{Time: now.Add(-time.Hour), Inches: 2.00}, {Time: now, Inches: 0}}, now, cfg, PauseView{}, Memory{})
	if got.Kind != ActionPause {
		t.Fatalf("2.00 increment should pause %+v", got)
	}
	got = Decide([]Sample{{Time: now.Add(-time.Hour), Inches: 2.01}, {Time: now, Inches: 0}}, now, cfg, PauseView{}, Memory{})
	if got.Kind != ActionUnavailable || got.Error != errImplausible {
		t.Fatalf("2.01 increment %+v", got)
	}
}

func TestDecideFutureRows(t *testing.T) {
	now := time.Date(2026, 9, 26, 18, 0, 0, 0, time.UTC)
	cfg := testPolicy()
	staleAt := now.Add(-8 * time.Hour)
	far := now.Add(48 * time.Hour)
	until := now.Add(10 * time.Hour)
	last := staleAt
	auto := PauseView{
		Active: true, Until: &until, Reason: "rain", Source: "auto",
		Inches: 0.4, LastRain: &last, EventAt: &last,
	}

	got := Decide([]Sample{{Time: staleAt, Inches: 1}, {Time: far, Inches: 9}}, now, cfg, PauseView{}, Memory{})
	if got.Kind != ActionUnavailable || !strings.Contains(got.Error, "stale") {
		t.Fatalf("stale+future %+v", got)
	}
	if got := Decide([]Sample{{Time: staleAt, Inches: 1}, {Time: far, Inches: 9}}, now, cfg, auto, Memory{}); got.Kind != ActionUnavailable || !strings.Contains(got.Error, "stale") {
		t.Fatalf("stale+future must not clear %+v", got)
	}

	tip := now.Add(10 * time.Minute)
	got = Decide([]Sample{{Time: tip, Inches: 0.3}}, now, cfg, PauseView{}, Memory{})
	if got.Kind != ActionPause || !got.LastRain.Equal(tip) {
		t.Fatalf("10 min future %+v", got)
	}
	edge := now.Add(futureSkew)
	got = Decide([]Sample{{Time: edge, Inches: 0.3}}, now, cfg, PauseView{}, Memory{})
	if got.Kind != ActionPause || !got.LastRain.Equal(edge) {
		t.Fatalf("exactly futureSkew %+v", got)
	}

	beyond := now.Add(futureSkew + time.Second)
	got = Decide([]Sample{
		{Time: now.Add(-time.Hour), Inches: 0.10},
		{Time: beyond, Inches: 50},
		{Time: now, Inches: 0},
	}, now, cfg, PauseView{}, Memory{})
	if got.Kind != ActionNone || got.Total > 0.11 {
		t.Fatalf("future rain counted %+v", got)
	}

	allFar := Decide([]Sample{{Time: far, Inches: 1}}, now, cfg, PauseView{}, Memory{})
	if allFar.Kind != ActionUnavailable {
		t.Fatalf("all future %+v", allFar)
	}
}
