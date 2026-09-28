package api

import (
	"context"
	"encoding/json"
	"errors"
	"io/fs"
	"net/http"
	"os"
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
	if _, ok := got["config_error"]; ok {
		t.Fatalf("config_error %v", got["config_error"])
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
		Enabled    bool `json:"enabled"`
		ETKnown    bool `json:"et_known"`
		WindowDays int  `json:"window_days"`
		RainKnown  bool `json:"rain_known"`
		Zones      []struct {
			StationID       string   `json:"station_id"`
			Percent         *int     `json:"percent"`
			RateMeasured    bool     `json:"rate_measured"`
			BalanceInches   *float64 `json:"balance_inches"`
			CapacityInches  float64  `json:"capacity_inches"`
			RainTotalInches float64  `json:"rain_total_inches"`
			ETTotalInches   float64  `json:"et_total_inches"`
			Inputs          struct {
				WateringInches *float64 `json:"watering_inches"`
				RainInches     float64  `json:"rain_inches"`
				ETInches       float64  `json:"et_inches"`
			} `json:"inputs"`
		} `json:"zones"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	if !got.Enabled || !got.ETKnown || !got.RainKnown || got.WindowDays != 1 || len(got.Zones) != 2 {
		t.Fatalf("%s", body)
	}
	var north, south *struct {
		StationID       string   `json:"station_id"`
		Percent         *int     `json:"percent"`
		RateMeasured    bool     `json:"rate_measured"`
		BalanceInches   *float64 `json:"balance_inches"`
		CapacityInches  float64  `json:"capacity_inches"`
		RainTotalInches float64  `json:"rain_total_inches"`
		ETTotalInches   float64  `json:"et_total_inches"`
		Inputs          struct {
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
	if north.Percent == nil || north.BalanceInches == nil || *north.Percent < 0 || *north.Percent > 100 {
		t.Fatalf("percent %v balance %v", north.Percent, north.BalanceInches)
	}
	// Window is the previous day plus today. Crop ET is 0.20 * 0.6 each day. Rain is 0.10 today.
	if north.RainTotalInches != 0.10 || north.ETTotalInches != 0.24 {
		t.Fatalf("north totals rain %v et %v", north.RainTotalInches, north.ETTotalInches)
	}
	if south.RateMeasured || south.Inputs.WateringInches != nil {
		t.Fatalf("south must be null rate %+v", south)
	}
	if south.RainTotalInches != 0.10 || south.ETTotalInches != 0.24 {
		t.Fatalf("south totals rain %v et %v", south.RainTotalInches, south.ETTotalInches)
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

func TestSoilAPIConfigMissingAndInvalid(t *testing.T) {
	s := newTestServer(t)
	t.Setenv("ZANJERITO_SOIL_CONFIG", "")
	p, err := soil.Start(context.Background(), s.Path, time.UTC)
	if p != nil || !errors.Is(err, soil.ErrNoConfig) || !errors.Is(err, fs.ErrNotExist) {
		t.Fatalf("missing p=%v err=%v", p, err)
	}
	s.NoteSoil(p, err)
	rr := doJSON(t, s, http.MethodGet, "/api/soil", nil)
	var got map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	if got["enabled"] != false || got["reason"] != "no soil.local.json" {
		t.Fatalf("%s", rr.Body.String())
	}
	if _, ok := got["config_error"]; ok {
		t.Fatalf("config_error %v body %s", got["config_error"], rr.Body.String())
	}

	bodies := []string{
		`{`,
		`{"azmet_station":123}`,
		`{"azmet_station":"azXX","zones":{"front-north":{"inches_per_hour":"fast"}}}`,
	}
	for _, body := range bodies {
		dir := t.TempDir()
		path := filepath.Join(dir, "soil.local.json")
		if err := os.WriteFile(path, []byte(body), 0o644); err != nil {
			t.Fatal(err)
		}
		t.Setenv("ZANJERITO_SOIL_CONFIG", path)
		p, err := soil.Start(context.Background(), s.Path, time.UTC)
		if p != nil || err == nil {
			t.Fatalf("body %s p=%v err=%v", body, p, err)
		}
		s.NoteSoil(p, err)
		rr := doJSON(t, s, http.MethodGet, "/api/soil", nil)
		raw := rr.Body.String()
		got = map[string]any{}
		if err := json.Unmarshal(rr.Body.Bytes(), &got); err != nil {
			t.Fatal(err)
		}
		if got["enabled"] != false || got["reason"] != "soil.local.json invalid" {
			t.Fatalf("%s", raw)
		}
		msg, _ := got["config_error"].(string)
		if msg == "" || strings.Contains(msg, "/") || strings.Contains(msg, "azXX") || strings.Contains(msg, "fast") {
			t.Fatalf("config_error %q body %s", msg, raw)
		}
	}
}

func TestSoilAPIETUnknownThenKnown(t *testing.T) {
	s := newTestServer(t)
	loc := phoenixLoc(t)
	fetchAt := time.Date(2026, 9, 27, 8, 0, 0, 0, loc)
	viewAt := fetchAt.Add(4 * time.Hour)
	clock := fetchAt
	fake := &soil.Fake{}
	p := &soil.Poller{
		Source: fake,
		Cfg:    soil.Config{Enabled: true, Station: "azXX", WindowDays: 14, CropFactor: 0.6, Capacity: 1, MaxDailyET: 0.6},
		Now:    func() time.Time { return clock },
		Loc:    loc,
	}
	s.Soil = p

	rr := doJSON(t, s, http.MethodGet, "/api/soil", nil)
	body := rr.Body.String()
	for _, need := range []string{`"et_known":false`, `"percent":null`, `"balance_inches":null`, `"updated_at":null`, `"et_reason":"waiting for first ET fetch"`} {
		if !strings.Contains(body, need) {
			t.Fatalf("missing %s in %s", need, body)
		}
	}
	if strings.Contains(body, "azXX") {
		t.Fatalf("leak %s", body)
	}

	fake.Set(nil, errors.New("HTTP 500 azXX https://example.test/azXX"))
	p.Poll(context.Background())
	clock = viewAt
	rr = doJSON(t, s, http.MethodGet, "/api/soil", nil)
	body = rr.Body.String()
	for _, need := range []string{`"et_known":false`, `"percent":null`, `"updated_at":null`} {
		if !strings.Contains(body, need) {
			t.Fatalf("missing %s in %s", need, body)
		}
	}
	if !strings.Contains(body, `"et_reason":"no ET data yet (AZMET unavailable)"`) {
		t.Fatalf("%s", body)
	}
	if strings.Contains(body, "azXX") || strings.Contains(body, "example.test") {
		t.Fatalf("leak %s", body)
	}

	clock = fetchAt
	fake.Set([]soil.DayET{{Date: "2026-09-26", ETInches: 0.20}}, nil)
	p.Poll(context.Background())
	clock = viewAt
	rr = doJSON(t, s, http.MethodGet, "/api/soil", nil)
	body = rr.Body.String()
	var known struct {
		ETKnown    bool    `json:"et_known"`
		ETReason   string  `json:"et_reason"`
		UpdatedAt  *string `json:"updated_at"`
		WindowDays int     `json:"window_days"`
		Zones      []struct {
			Percent         *int     `json:"percent"`
			BalanceInches   *float64 `json:"balance_inches"`
			RainTotalInches float64  `json:"rain_total_inches"`
			ETTotalInches   float64  `json:"et_total_inches"`
			Inputs          struct {
				ETInches float64 `json:"et_inches"`
			} `json:"inputs"`
		} `json:"zones"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &known); err != nil {
		t.Fatal(err)
	}
	if !known.ETKnown || known.ETReason != "" || known.WindowDays != 14 || len(known.Zones) != 2 {
		t.Fatalf("%s", body)
	}
	if known.UpdatedAt == nil {
		t.Fatal("updated_at nil")
	}
	got, err := time.Parse(time.RFC3339, *known.UpdatedAt)
	if err != nil {
		t.Fatal(err)
	}
	st := p.Status()
	if st.LastOK == nil || !got.Equal(*st.LastOK) || got.Equal(viewAt) {
		t.Fatalf("updated %s lastOK %v view %s", got, st.LastOK, viewAt)
	}
	for _, z := range known.Zones {
		if z.Percent == nil || z.BalanceInches == nil {
			t.Fatalf("percent %+v", z)
		}
		if z.ETTotalInches != 0.24 {
			t.Fatalf("et total %v body %s", z.ETTotalInches, body)
		}
	}
	if strings.Contains(body, "azXX") {
		t.Fatalf("leak %s", body)
	}
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
		"No ET data yet",
		"et_known",
		"Soil settings file has an error",
		", last ",
		"Soil estimate: no ET data yet",
		"rain_total_inches",
		"et_total_inches",
		"window_days",
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
