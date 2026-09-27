package api

import (
	"context"
	"encoding/json"
	"net/http"
	"strings"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/rain"
	"github.com/PixnBits/zanjerito/internal/store"
)

func TestStatusRainDisabledByDefault(t *testing.T) {
	s := newTestServer(t)
	st := decodeMap(t, doJSON(t, s, http.MethodGet, "/api/status", nil))
	rainObj, _ := st["rain"].(map[string]any)
	if rainObj["enabled"] != false || rainObj["unavailable"] != false {
		t.Fatalf("rain %v", st["rain"])
	}
	if st["pause_source"] != "" {
		t.Fatalf("pause_source %v", st["pause_source"])
	}
}

func TestManualPauseSourceAndAutoRainLabel(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"days": 2, "reason": "mow"})
	if rr.Code != 200 {
		t.Fatalf("pause %d %s", rr.Code, rr.Body.String())
	}
	st := decodeMap(t, rr)
	if st["pause_source"] != "manual" {
		t.Fatalf("source %v", st["pause_source"])
	}
	if !strings.HasPrefix(st["paused_label"].(string), "Paused until ") {
		t.Fatalf("label %v", st["paused_label"])
	}

	_ = doJSON(t, s, http.MethodDelete, "/api/pause", nil)
	loc := phoenixLoc(t)
	now := time.Now().In(loc)
	until := time.Date(now.Year(), now.Month(), now.Day()+1, 6, 0, 0, 0, loc)
	last := now.Add(-time.Hour)
	s.Eng.SetPauseMeta(&until, "rain", engine.PauseMeta{
		Source: engine.PauseSourceAuto, RainInches: 0.4, LastRainAt: &last,
	})
	st = decodeMap(t, doJSON(t, s, http.MethodGet, "/api/status", nil))
	if st["pause_source"] != "auto" || st["reason"] != "rain" {
		t.Fatalf("auto status %v", st)
	}
	label, _ := st["paused_label"].(string)
	if !strings.Contains(label, "Paused for rain (0.4 in)") || !strings.Contains(label, "morning") {
		t.Fatalf("label %q", label)
	}
	inches, _ := st["rain_inches"].(float64)
	if inches < 0.39 || inches > 0.41 {
		t.Fatalf("inches %v", st["rain_inches"])
	}
	if st["last_rain_at"] == nil || st["last_rain_at"] == "" {
		t.Fatalf("last_rain_at %v", st["last_rain_at"])
	}
	if strings.Contains(rr.Body.String(), "<GAUGE") {
		t.Fatal("status leaked a gauge placeholder")
	}
}

func TestAutoRainPauseExemptStationRun(t *testing.T) {
	s := newTestServer(t)
	f, err := store.Load(s.Path)
	if err != nil {
		t.Fatal(err)
	}
	for i := range f.Stations {
		if f.Stations[i].ID == "front-north" {
			f.Stations[i].RainPauseExempt = true
		}
	}
	if err := store.ApplyAndSave(s.Eng, s.Path, f.Config); err != nil {
		t.Fatal(err)
	}
	until := time.Now().Add(2 * time.Hour)
	s.Eng.SetPauseMeta(&until, "rain", engine.PauseMeta{Source: engine.PauseSourceAuto, RainInches: 0.4})

	rr := doJSON(t, s, http.MethodPost, "/api/stations/front-south/run", map[string]any{"durationSec": 1})
	if rr.Code != http.StatusConflict || !strings.Contains(rr.Body.String(), "resume before running") {
		t.Fatalf("non-exempt %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodPost, "/api/stations/front-north/run", map[string]any{"durationSec": 1})
	if rr.Code != http.StatusAccepted {
		t.Fatalf("exempt %d %s", rr.Code, rr.Body.String())
	}
	_ = s.Eng.Stop()

	s.Eng.SetPause(&until, "rain")
	rr = doJSON(t, s, http.MethodPost, "/api/stations/front-north/run", map[string]any{"durationSec": 1})
	if rr.Code != http.StatusConflict {
		t.Fatalf("manual exempt %d %s", rr.Code, rr.Body.String())
	}
}

func TestPauseClearRemembersRain(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"days": 1})
	if rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodDelete, "/api/pause", nil)
	if rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	loaded, err := store.LoadPause(s.Path, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if loaded.Active || loaded.RainClearedAt == nil {
		t.Fatalf("clear memory %+v", loaded)
	}
}

func TestStatusRainUnavailable(t *testing.T) {
	s := newTestServer(t)
	fake := &rain.Fake{}
	fake.Set(nil, errRain("fetch TEST-GAUGE https://example.test/gauge"))
	p := &rain.Poller{
		Source: fake,
		Eng:    s.Eng,
		Path:   s.Path,
		Cfg:    rain.Config{Enabled: true, GaugeID: "TEST-GAUGE", StaleHours: 7},
	}
	s.Rain = p
	p.Poll(context.Background())
	body := doJSON(t, s, http.MethodGet, "/api/status", nil).Body.String()
	if strings.Contains(body, "TEST-GAUGE") || strings.Contains(body, "example.test") {
		t.Fatalf("status leaked gauge or url: %s", body)
	}
	var st map[string]any
	if err := json.Unmarshal([]byte(body), &st); err != nil {
		t.Fatal(err)
	}
	rainObj, _ := st["rain"].(map[string]any)
	if rainObj["enabled"] != true || rainObj["unavailable"] != true {
		t.Fatalf("rain %v", rainObj)
	}
	if rainObj["last_error"] == nil || rainObj["last_error"] == "" {
		t.Fatalf("last_error %v", rainObj)
	}
}

type errRain string

func (e errRain) Error() string { return string(e) }
