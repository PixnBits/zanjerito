package api

import (
	"context"
	"encoding/json"
	"net/http"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/history"
	"github.com/PixnBits/zanjerito/internal/rain"
	"github.com/PixnBits/zanjerito/internal/soil"
)

func TestSoilAPIDisabled(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/api/soil", nil)
	if rr.Code != 200 {
		t.Fatalf("code %d %s", rr.Code, rr.Body.String())
	}
	if cc := rr.Header().Get("Cache-Control"); cc != "no-store" {
		t.Fatalf("cache %q", cc)
	}
	var got map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	if got["enabled"] != false {
		t.Fatalf("%s", rr.Body.String())
	}
	if got["reason"] != "no soil.local.json" {
		t.Fatalf("reason %v", got["reason"])
	}
	zones, _ := got["zones"].([]any)
	if zones == nil || len(zones) != 0 {
		t.Fatalf("zones %v", got["zones"])
	}
}

func TestSoilAPIEnabledMeasuredAndNullRate(t *testing.T) {
	s := newTestServer(t)
	loc := phoenixLoc(t)
	now := time.Date(2026, 9, 27, 15, 0, 0, 0, loc)
	rate := 0.5
	cfg := soil.Config{
		Enabled:    true,
		Station:    "azXX",
		WindowDays: 1,
		CropFactor: 0.6,
		Capacity:   1.0,
		MaxDailyET: 0.6,
		Zones: map[string]soil.Zone{
			"front-north": {InchesPerHour: &rate, CropFactor: 0.6, Capacity: 1.0},
		},
	}
	p := &soil.Poller{Cfg: cfg, Now: func() time.Time { return now }, Loc: loc}
	p.SeedCacheForTest([]soil.DayET{
		{Date: "2026-09-26", ETInches: 0.20},
		{Date: "2026-09-27", ETInches: 0.20},
	}, now)
	s.Soil = p

	fake := &rain.Fake{}
	rp := &rain.Poller{
		Source: fake,
		Eng:    s.Eng,
		Path:   s.Path,
		Cfg: rain.Config{
			Enabled: true, GaugeID: "TEST-GAUGE",
			TriggerInches: 0.25, WindowHours: 24,
			DryDays: 2, HeavyInches: 1, HeavyDryDays: 4, StaleHours: 7,
		},
		Now: func() time.Time { return now },
	}
	fake.Set([]rain.Sample{
		{Time: now.Add(-time.Hour), Inches: 0.10},
		{Time: now, Inches: 0},
	}, nil)
	rp.Poll(context.Background())
	s.Rain = rp

	hl, err := history.Open(filepath.Join(t.TempDir(), "history.json"), func() time.Time { return now })
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(hl.Close)
	hl.Append(engine.RunRecord{
		Outcome: engine.OutcomeCompleted,
		Start:   now.Add(-2 * time.Hour),
		End:     now.Add(-time.Hour),
		Stations: []engine.StationRun{
			{StationID: "front-north", PlannedSec: 1800, ActualSec: 1800},
		},
	})
	s.History = hl

	rr := doJSON(t, s, http.MethodGet, "/api/soil", nil)
	if rr.Code != 200 {
		t.Fatalf("%d %s", rr.Code, rr.Body.String())
	}
	body := rr.Body.String()
	if strings.Contains(body, "azXX") || strings.Contains(body, "Test Station") {
		t.Fatalf("leaked AZMET id or name: %s", body)
	}
	var got struct {
		Enabled   bool `json:"enabled"`
		RainKnown bool `json:"rain_known"`
		Zones     []struct {
			StationID      string  `json:"station_id"`
			Percent        int     `json:"percent"`
			RateMeasured   bool    `json:"rate_measured"`
			BalanceInches  float64 `json:"balance_inches"`
			CapacityInches float64 `json:"capacity_inches"`
			Inputs         struct {
				WateringInches *float64 `json:"watering_inches"`
				RainInches     float64  `json:"rain_inches"`
				ETInches       float64  `json:"et_inches"`
			} `json:"inputs"`
		} `json:"zones"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	if !got.Enabled || !got.RainKnown || len(got.Zones) != 2 {
		t.Fatalf("%s", body)
	}
	var north, south *struct {
		StationID      string  `json:"station_id"`
		Percent        int     `json:"percent"`
		RateMeasured   bool    `json:"rate_measured"`
		BalanceInches  float64 `json:"balance_inches"`
		CapacityInches float64 `json:"capacity_inches"`
		Inputs         struct {
			WateringInches *float64 `json:"watering_inches"`
			RainInches     float64  `json:"rain_inches"`
			ETInches       float64  `json:"et_inches"`
		} `json:"inputs"`
	}
	for i := range got.Zones {
		z := &got.Zones[i]
		if z.StationID == "front-north" {
			north = z
		}
		if z.StationID == "front-south" {
			south = z
		}
	}
	if north == nil || south == nil {
		t.Fatalf("stations %s", body)
	}
	if !north.RateMeasured || north.Inputs.WateringInches == nil {
		t.Fatalf("north %+v", north)
	}
	// 1800s * 0.5 in/hr = 0.25 in
	if *north.Inputs.WateringInches != 0.25 {
		t.Fatalf("watering %v", *north.Inputs.WateringInches)
	}
	if north.Inputs.RainInches != 0.10 {
		t.Fatalf("rain %v", north.Inputs.RainInches)
	}
	if north.Percent < 0 || north.Percent > 100 {
		t.Fatalf("percent %d", north.Percent)
	}
	if south.RateMeasured || south.Inputs.WateringInches != nil {
		t.Fatalf("south must be null rate %+v", south)
	}
}

func TestSoilHandlerDoesNotBlockOnSlowFetch(t *testing.T) {
	s := newTestServer(t)
	loc := phoenixLoc(t)
	now := time.Date(2026, 9, 27, 12, 0, 0, 0, loc)
	started := make(chan struct{})
	release := make(chan struct{})
	var once sync.Once
	src := &blockingSoil{
		started: started,
		release: release,
		once:    &once,
	}
	p := &soil.Poller{
		Source: src,
		Cfg:    soil.Config{Enabled: true, WindowDays: 14, CropFactor: 0.6, Capacity: 1, MaxDailyET: 0.6},
		Now:    func() time.Time { return now },
		Loc:    loc,
	}
	p.SeedCacheForTest([]soil.DayET{{Date: "2026-09-26", ETInches: 0.2}}, now)
	s.Soil = p

	done := make(chan struct{})
	go func() {
		p.Poll(context.Background())
		close(done)
	}()
	select {
	case <-started:
	case <-time.After(2 * time.Second):
		t.Fatal("fetch did not start")
	}
	start := time.Now()
	rr := doJSON(t, s, http.MethodGet, "/api/soil", nil)
	if d := time.Since(start); d > 200*time.Millisecond {
		t.Fatalf("GET /api/soil took %s body %s", d, rr.Body.String())
	}
	if rr.Code != 200 || !strings.Contains(rr.Body.String(), `"enabled":true`) {
		t.Fatalf("%d %s", rr.Code, rr.Body.String())
	}
	close(release)
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("poll did not finish")
	}
}

type blockingSoil struct {
	started chan struct{}
	release chan struct{}
	once    *sync.Once
}

func (b *blockingSoil) Fetch(ctx context.Context, _ time.Time, _ int) ([]soil.DayET, error) {
	b.once.Do(func() { close(b.started) })
	select {
	case <-b.release:
		return []soil.DayET{{Date: "2026-09-26", ETInches: 0.2}}, nil
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

func TestUISoilStrings(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/", nil)
	if rr.Code != 200 {
		t.Fatalf("GET / %d", rr.Code)
	}
	body := rr.Body.String()
	for _, need := range []string{
		"Soil water",
		"estimate",
		"Measure sprinkler output to enable",
		"ET unavailable since",
		"/api/soil",
		"rain & ET only",
	} {
		if !strings.Contains(body, need) {
			t.Fatalf("ui missing %q", need)
		}
	}
	for _, leak := range []string{"api.azmet.arizona.edu", "azXX"} {
		if strings.Contains(body, leak) {
			t.Fatalf("ui leaked %q", leak)
		}
	}
}
