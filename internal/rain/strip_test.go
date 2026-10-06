package rain

import (
	"context"
	"math"
	"testing"
	"time"
)

func TestRainStripThreshold(t *testing.T) {
	now := time.Date(2026, 9, 28, 15, 0, 0, 0, time.UTC)
	old := now.Add(-80 * time.Hour)
	edge := now.Add(-72 * time.Hour) // exactly 72 h ago: outside (now-72h, now]
	ago50 := now.Add(-50 * time.Hour)
	ago40 := now.Add(-40 * time.Hour)
	recent := now.Add(-2 * time.Hour)
	futureOK := now.Add(futureSkew)
	futureLate := now.Add(futureSkew + time.Second)

	cases := []struct {
		name     string
		samples  []Sample
		enabled  bool
		unavail  bool
		paused   bool
		haveGood bool
		show     bool
		hours    int
		inches   float64
	}{
		{
			name:     "0.04 hidden",
			samples:  []Sample{{Time: recent, Inches: 0.04}, {Time: now, Inches: 0}},
			enabled:  true,
			haveGood: true,
			show:     false,
			hours:    72,
			inches:   0.04,
		},
		{
			name:     "0.05 shown as 72h when nothing in 24h",
			samples:  []Sample{{Time: ago50, Inches: 0.05}, {Time: now, Inches: 0}},
			enabled:  true,
			haveGood: true,
			show:     true,
			hours:    72,
			inches:   0.05,
		},
		{
			name:     "rain in 24h uses 24",
			samples:  []Sample{{Time: recent, Inches: 0.05}, {Time: ago50, Inches: 0.20}, {Time: now, Inches: 0}},
			enabled:  true,
			haveGood: true,
			show:     true,
			hours:    24,
			inches:   0.05,
		},
		{
			name:     "rain only 30-70h ago uses 72",
			samples:  []Sample{{Time: ago40, Inches: 0.08}, {Time: ago50, Inches: 0.02}, {Time: now, Inches: 0}},
			enabled:  true,
			haveGood: true,
			show:     true,
			hours:    72,
			inches:   0.10,
		},
		{
			name: "samples older than 72h ignored",
			samples: []Sample{
				{Time: old, Inches: 1},
				{Time: edge, Inches: 0.25},
				{Time: futureLate, Inches: 0.5},
				{Time: recent, Inches: -0.4},
				{Time: now, Inches: 0},
			},
			enabled:  true,
			haveGood: true,
			show:     false,
			hours:    72,
			inches:   0,
		},
		{
			name: "future skew included and older rain still ignored",
			samples: []Sample{
				{Time: old, Inches: 2},
				{Time: futureOK, Inches: 0.05},
				{Time: now, Inches: 0},
			},
			enabled:  true,
			haveGood: true,
			show:     true,
			hours:    24,
			inches:   0.05,
		},
		{
			name:     "unavailable hidden",
			samples:  []Sample{{Time: recent, Inches: 0.4}, {Time: now, Inches: 0}},
			enabled:  true,
			unavail:  true,
			haveGood: true,
			show:     false,
		},
		{
			name:     "disabled hidden",
			samples:  []Sample{{Time: recent, Inches: 0.4}, {Time: now, Inches: 0}},
			enabled:  false,
			haveGood: true,
			show:     false,
			hours:    24,
			inches:   0.4,
		},
		{
			name:     "no last good hidden",
			samples:  []Sample{{Time: recent, Inches: 0.4}, {Time: now, Inches: 0}},
			enabled:  true,
			haveGood: false,
			show:     false,
		},
		{
			name:     "active rain pause hidden",
			samples:  []Sample{{Time: recent, Inches: 0.4}, {Time: now, Inches: 0}},
			enabled:  true,
			paused:   true,
			haveGood: true,
			show:     false,
			hours:    24,
			inches:   0.4,
		},
		{
			name: "inches above 1 stay true",
			samples: []Sample{
				{Time: recent, Inches: 1.25},
				{Time: ago40, Inches: 0.25},
				{Time: now, Inches: 0},
			},
			enabled:  true,
			haveGood: true,
			show:     true,
			hours:    24,
			inches:   1.25,
		},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			p := &Poller{}
			p.lastGood = tc.samples
			p.haveGood = tc.haveGood
			p.st.Unavailable = tc.unavail
			in24, ok24 := p.TotalSince(24*time.Hour, now)
			in72, ok72 := p.TotalSince(72*time.Hour, now)
			if ok24 != ok72 {
				t.Fatalf("24h ok %v 72h ok %v", ok24, ok72)
			}
			if tc.unavail || !tc.haveGood {
				if ok72 {
					t.Fatal("expected no usable total")
				}
			}
			got := HomeStrip(tc.enabled, tc.unavail, tc.paused, ok72, in24, in72)
			if got.Show != tc.show || got.Hours != tc.hours || math.Abs(got.Inches-tc.inches) > 1e-9 {
				t.Fatalf("got %+v want show %v hours %d inches %v", got, tc.show, tc.hours, tc.inches)
			}
			if got.Show && got.Inches > 1 && got.Inches < tc.inches {
				t.Fatalf("fill must not cap reported inches: %+v", got)
			}
		})
	}
}

func TestTotalSinceNil(t *testing.T) {
	var p *Poller
	got, ok := p.TotalSince(72*time.Hour, time.Now())
	if ok || got != 0 {
		t.Fatalf("nil poller %v %v", got, ok)
	}
}

func TestTotalSinceFollowsLastGoodFetch(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	p.Cfg.GaugeID = "00000"
	now := clk.Now()
	fake.Set([]Sample{
		{Time: now.Add(-80 * time.Hour), Inches: 1},
		{Time: now.Add(-2 * time.Hour), Inches: 0.05},
		{Time: now, Inches: 0},
	}, nil)
	p.Poll(context.Background())
	got, ok := p.TotalSince(72*time.Hour, now)
	if !ok || math.Abs(got-0.05) > 1e-9 {
		t.Fatalf("last good %v ok %v", got, ok)
	}
	in24, ok24 := p.TotalSince(24*time.Hour, now)
	if !ok24 || math.Abs(in24-0.05) > 1e-9 {
		t.Fatalf("24h %v ok %v", in24, ok24)
	}

	fake.Set(nil, errString("temporary"))
	p.Poll(context.Background())
	if p.Status().Unavailable != true {
		t.Fatal("want unavailable")
	}
	if _, ok := p.TotalSince(72*time.Hour, now); ok {
		t.Fatal("unavailable feed must not publish a total")
	}
}
