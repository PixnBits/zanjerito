package rain

import (
	"context"
	"testing"
	"time"
)

func TestDailyRainNilDisabled(t *testing.T) {
	var p *Poller
	got, ok := p.DailyRain(time.UTC)
	if ok || got != nil {
		t.Fatalf("nil poller: %v %v", got, ok)
	}
}

func TestDailyRainGroupsLastGoodByLocalDate(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	loc, err := time.LoadLocation(phoenixTZ)
	if err != nil {
		t.Fatal(err)
	}
	// 2026-09-24 23:00 Phoenix = 2026-09-25 06:00 UTC (no DST).
	day := time.Date(2026, 9, 24, 0, 0, 0, 0, loc)
	now := time.Date(2026, 9, 25, 8, 0, 0, 0, loc)
	clk.Set(now.UTC())
	fake.Set([]Sample{
		{Time: day.Add(10 * time.Hour), Inches: 0.10},
		{Time: day.Add(22 * time.Hour), Inches: 0.05},
		{Time: day.Add(25 * time.Hour), Inches: 0.20}, // 01:00 next local day
		{Time: now, Inches: 0},
	}, nil)
	p.Poll(context.Background())

	got, ok := p.DailyRain(loc)
	if !ok {
		t.Fatal("expected last-good rain")
	}
	if got["2026-09-24"] < 0.15-1e-9 || got["2026-09-24"] > 0.15+1e-9 {
		t.Fatalf("24th %v", got["2026-09-24"])
	}
	if got["2026-09-25"] < 0.20-1e-9 || got["2026-09-25"] > 0.20+1e-9 {
		t.Fatalf("25th %v", got)
	}

	// A later bad fetch must keep the last good totals.
	fake.Set(nil, errString("dial TEST-GAUGE"))
	p.Poll(context.Background())
	if !p.Status().Unavailable {
		t.Fatal("want unavailable")
	}
	got2, ok := p.DailyRain(loc)
	if !ok {
		t.Fatal("last good should remain")
	}
	if got2["2026-09-24"] != got["2026-09-24"] || got2["2026-09-25"] != got["2026-09-25"] {
		t.Fatalf("cache mutated %v vs %v", got2, got)
	}

	// Caller must not be able to mutate the poller's copy.
	got2["2026-09-24"] = 99
	got3, _ := p.DailyRain(loc)
	if got3["2026-09-24"] > 1 {
		t.Fatal("DailyRain must copy")
	}
}

func TestDailyRainNoGoodData(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	now := clk.Now()
	fake.Set([]Sample{{Time: now.Add(-time.Minute), Inches: 99.99}, {Time: now, Inches: 0}}, nil)
	p.Poll(context.Background())
	if !p.Status().Unavailable {
		t.Fatal("implausible should be unavailable")
	}
	_, ok := p.DailyRain(nil)
	if ok {
		t.Fatal("implausible fetch is not last-good")
	}
}
