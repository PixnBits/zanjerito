package rain

import (
	"testing"
	"time"
)

func testPolicy() Config {
	return Config{
		TriggerInches: 0.25,
		WindowHours:   24,
		DryDays:       2,
		HeavyInches:   1,
		HeavyDryDays:  4,
		StaleHours:    7,
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
