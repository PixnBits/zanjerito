package api

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/gpio"
	"github.com/PixnBits/zanjerito/internal/rain"
	"github.com/PixnBits/zanjerito/internal/schedule"
	"github.com/PixnBits/zanjerito/internal/soil"
	"github.com/PixnBits/zanjerito/internal/store"
)

func kioskCfg() engine.Config {
	al := true
	return engine.Config{
		Chip:      "gpiochip0",
		ActiveLow: &al,
		Timezone:  "America/Phoenix",
		MaxOnSec:  900,
		Power:     engine.StationConfig{ID: "psu", Title: "Supply", BCM: 21, Physical: 40, WiringPi: 29},
		Stations: []engine.StationConfig{
			{ID: "s1", Title: "Test Station 1", Color: "#111111", BCM: 5, Physical: 29, WiringPi: 21, RainPauseExempt: true},
			{ID: "s2", Title: "Test Station 2", Color: "#222222", BCM: 6, Physical: 31, WiringPi: 22},
		},
	}
}

func kioskServer(t *testing.T, schedules []store.Schedule) *Server {
	t.Helper()
	cfg := kioskCfg()
	path := filepath.Join(t.TempDir(), "cfg.json")
	if err := store.Save(path, store.File{Config: cfg, Schedules: schedules}); err != nil {
		t.Fatal(err)
	}
	e, err := engine.New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = e.Close() })
	return New(e, path)
}

func kioskProgram(t *testing.T) []store.Schedule {
	t.Helper()
	loc := phoenixLoc(t)
	slot := time.Now().In(loc).Add(2 * time.Hour).Truncate(time.Minute)
	start := slot.Format("15:04")
	return []store.Schedule{{
		ID: "prog-a", Enabled: true, Note: "Program A", Start: start,
		Steps: []store.Step{
			{StationID: "s1", Minutes: 5},
			{StationID: "s2", Minutes: 7},
		},
	}}
}

func getKiosk(t *testing.T, s *Server) (map[string]any, string) {
	t.Helper()
	rr := doJSON(t, s, http.MethodGet, "/api/kiosk", nil)
	if rr.Code != 200 {
		t.Fatalf("kiosk %d %s", rr.Code, rr.Body.String())
	}
	if cc := rr.Header().Get("Cache-Control"); cc != "no-store" {
		t.Fatalf("cache %q", cc)
	}
	body := rr.Body.String()
	assertNoPinKeys(t, body)
	var got map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	return got, body
}

func assertNoPinKeys(t *testing.T, body string) {
	t.Helper()
	for _, leak := range []string{"wiringPi", "WiringPi", "physical", "\"bcm\"", "\"power\"", "Supply"} {
		if strings.Contains(body, leak) {
			t.Fatalf("kiosk leaked %q in %s", leak, body)
		}
	}
	var v any
	if err := json.Unmarshal([]byte(body), &v); err != nil {
		t.Fatal(err)
	}
	var walk func(any)
	walk = func(n any) {
		switch x := n.(type) {
		case map[string]any:
			for k, child := range x {
				switch k {
				case "bcm", "physical", "wiringPi", "power", "BCM", "Physical", "WiringPi", "Power":
					t.Fatalf("forbidden key %q", k)
				}
				walk(child)
			}
		case []any:
			for _, c := range x {
				walk(c)
			}
		}
	}
	walk(v)
}

func obj(t *testing.T, v any) map[string]any {
	t.Helper()
	m, ok := v.(map[string]any)
	if !ok {
		t.Fatalf("want object, got %#v", v)
	}
	return m
}

func TestKioskIdle(t *testing.T) {
	sch := kioskProgram(t)
	s := kioskServer(t, sch)
	got, body := getKiosk(t, s)
	for _, k := range []string{
		"now", "timezone", "phase", "lockout", "last_error", "current_station", "stations_on",
		"pause", "rain_strip", "rain", "next_run", "next_effective_run", "run", "stations", "soil",
	} {
		if _, ok := got[k]; !ok {
			t.Fatalf("missing %s in %s", k, body)
		}
	}
	if got["phase"] != "Idle" || got["lockout"] != false || got["timezone"] != "America/Phoenix" {
		t.Fatalf("status %v", got)
	}
	if got["run"] != nil {
		t.Fatalf("run %v", got["run"])
	}
	if _, ok := got["stations_on"].([]any); !ok {
		t.Fatalf("stations_on %T", got["stations_on"])
	}
	pause := obj(t, got["pause"])
	if pause["paused"] != false || pause["until"] != nil || pause["source"] != "" {
		t.Fatalf("pause %v", pause)
	}
	rainObj := obj(t, got["rain"])
	if rainObj["enabled"] != false || rainObj["have_totals"] != false {
		t.Fatalf("rain %v", rainObj)
	}
	strip := obj(t, got["rain_strip"])
	if strip["show"] != false {
		t.Fatalf("strip %v", strip)
	}
	soilObj := obj(t, got["soil"])
	if soilObj["enabled"] != false || soilObj["show_bars"] != false || soilObj["updated_at"] != nil {
		t.Fatalf("soil %v", soilObj)
	}
	stations, _ := got["stations"].([]any)
	if len(stations) != 2 {
		t.Fatalf("stations %v", got["stations"])
	}
	s1 := obj(t, stations[0])
	for _, k := range []string{"id", "title", "color", "on", "state", "rain_pause_exempt", "soil_percent"} {
		if _, ok := s1[k]; !ok {
			t.Fatalf("station missing %s in %v", k, s1)
		}
	}
	if s1["id"] != "s1" || s1["title"] != "Test Station 1" || s1["on"] != false || s1["state"] != "idle" || s1["rain_pause_exempt"] != true || s1["soil_percent"] != nil {
		t.Fatalf("s1 %v", s1)
	}
	next := obj(t, got["next_run"])
	eff := obj(t, got["next_effective_run"])
	if next["schedule_id"] != "prog-a" || next["name"] != "Program A" || next["total_min"] != float64(12) || next["skipped_by_pause"] != false {
		t.Fatalf("next %v", next)
	}
	if eff["at"] != next["at"] || eff["skipped_by_pause"] != false {
		t.Fatalf("effective %v next %v", eff, next)
	}
	loc := phoenixLoc(t)
	want, ok := schedule.NextRun(sch, time.Now(), loc)
	if !ok {
		t.Fatal("no next run")
	}
	gotAt, err := time.Parse(time.RFC3339, next["at"].(string))
	if err != nil {
		t.Fatal(err)
	}
	// The handler's clock is within this test. Allow either side of a minute boundary.
	alt, _ := schedule.NextRun(sch, time.Now().Add(-time.Second), loc)
	if !gotAt.Equal(want.At) && !gotAt.Equal(alt.At) {
		t.Fatalf("at %s want %s or %s", gotAt, want.At, alt.At)
	}

	empty := kioskServer(t, nil)
	got, _ = getKiosk(t, empty)
	if got["next_run"] != nil || got["next_effective_run"] != nil {
		t.Fatalf("missing schedules should be null next, got %v %v", got["next_run"], got["next_effective_run"])
	}
}

func TestKioskPauseSkipsNext(t *testing.T) {
	sch := kioskProgram(t)
	loc := phoenixLoc(t)

	t.Run("until covers the next fire", func(t *testing.T) {
		s := kioskServer(t, sch)
		got, _ := getKiosk(t, s)
		next := obj(t, got["next_run"])
		at, err := time.Parse(time.RFC3339, next["at"].(string))
		if err != nil {
			t.Fatal(err)
		}
		until := at.Add(time.Second)
		s.Eng.SetPause(&until, "away")
		got, body := getKiosk(t, s)
		next = obj(t, got["next_run"])
		if next["skipped_by_pause"] != true {
			t.Fatalf("skipped %v body %s", next, body)
		}
		gotAt, err := time.Parse(time.RFC3339, next["at"].(string))
		if err != nil || !gotAt.Equal(at) {
			t.Fatalf("next at changed %v", next["at"])
		}
		eff := obj(t, got["next_effective_run"])
		if eff["skipped_by_pause"] != false {
			t.Fatalf("effective skipped %v", eff)
		}
		effAt, err := time.Parse(time.RFC3339, eff["at"].(string))
		if err != nil {
			t.Fatal(err)
		}
		if !effAt.After(at) {
			t.Fatalf("effective %s should be after %s", effAt, at)
		}
		want, ok := schedule.NextRunAfterPause(sch, until, loc)
		if !ok || !effAt.Equal(want.At) {
			t.Fatalf("effective %s want %+v", effAt, want)
		}
		pause := obj(t, got["pause"])
		if pause["paused"] != true || pause["source"] != "manual" || pause["reason"] != "away" || pause["until"] == nil {
			t.Fatalf("pause %v", pause)
		}
	})

	t.Run("fire exactly at until is not skipped", func(t *testing.T) {
		s := kioskServer(t, sch)
		got, _ := getKiosk(t, s)
		at, err := time.Parse(time.RFC3339, obj(t, got["next_run"])["at"].(string))
		if err != nil {
			t.Fatal(err)
		}
		s.Eng.SetPause(&at, "away")
		got, _ = getKiosk(t, s)
		next := obj(t, got["next_run"])
		eff := obj(t, got["next_effective_run"])
		if next["skipped_by_pause"] != false || eff["at"] != next["at"] {
			t.Fatalf("next %v effective %v", next, eff)
		}
	})

	t.Run("indefinite", func(t *testing.T) {
		s := kioskServer(t, sch)
		s.Eng.SetPause(nil, "away")
		got, body := getKiosk(t, s)
		next := obj(t, got["next_run"])
		if next["skipped_by_pause"] != true || next["schedule_id"] != "prog-a" {
			t.Fatalf("next %v", next)
		}
		if got["next_effective_run"] != nil {
			t.Fatalf("effective %v body %s", got["next_effective_run"], body)
		}
		pause := obj(t, got["pause"])
		if pause["paused"] != true || pause["until"] != nil || pause["label"] == "" {
			t.Fatalf("pause %v", pause)
		}
	})
}

func TestKioskRunningProgress(t *testing.T) {
	s := kioskServer(t, kioskProgram(t))
	done := make(chan error, 1)
	go func() {
		done <- s.Eng.RunProgram(context.Background(), "prog-a", "Program A", []engine.Step{
			{StationID: "s1", Duration: 2 * time.Second},
			{StationID: "s2", Duration: 2 * time.Second},
		})
	}()

	var got map[string]any
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		var body string
		got, body = getKiosk(t, s)
		run, _ := got["run"].(map[string]any)
		if run != nil && run["step_index"] == float64(0) {
			if run["kind"] != "schedule" || run["program_id"] != "prog-a" || run["program"] != "Program A" {
				t.Fatalf("run %v", run)
			}
			if run["step_count"] != float64(2) || run["current_station"] != "s1" || run["next_station"] != "s2" {
				t.Fatalf("cursor %v", run)
			}
			if run["run_total_sec"] != float64(4) {
				t.Fatalf("total %v", run["run_total_sec"])
			}
			steps, _ := run["steps"].([]any)
			if len(steps) != 2 {
				t.Fatalf("steps %v body %s", steps, body)
			}
			a, b := obj(t, steps[0]), obj(t, steps[1])
			if a["state"] != "active" || a["station_id"] != "s1" || a["title"] != "Test Station 1" || a["planned_sec"] != float64(2) {
				t.Fatalf("step0 %v", a)
			}
			if b["state"] != "pending" || b["station_id"] != "s2" || b["planned_sec"] != float64(2) {
				t.Fatalf("step1 %v", b)
			}
			rem, _ := run["step_remaining_sec"].(float64)
			runRem, _ := run["run_remaining_sec"].(float64)
			if rem < 0 || rem > 2 || runRem != rem+2 {
				t.Fatalf("remain step %v run %v", rem, runRem)
			}
			stations, _ := got["stations"].([]any)
			s1, s2 := obj(t, stations[0]), obj(t, stations[1])
			if s1["on"] != true || s1["state"] != "running" || s2["on"] != false || s2["state"] != "queued" {
				t.Fatalf("stations %v %v", s1, s2)
			}
			break
		}
		got = nil
		time.Sleep(15 * time.Millisecond)
	}
	if got == nil || got["run"] == nil {
		t.Fatal("did not observe step 0")
	}
	_ = s.Eng.Stop()
	deadline = time.Now().Add(2 * time.Second)
	for {
		got, _ = getKiosk(t, s)
		if got["run"] == nil && got["phase"] == "Idle" {
			break
		}
		if time.Now().After(deadline) {
			t.Fatalf("still running %#v", got["run"])
		}
		time.Sleep(15 * time.Millisecond)
	}
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("run did not return")
	}
}

func TestKioskRainStrip(t *testing.T) {
	base := rain.Config{
		Enabled: true, GaugeID: "not-a-gauge",
		TriggerInches: 0.25, WindowHours: 24,
		DryDays: 2, HeavyInches: 1, HeavyDryDays: 4, StaleHours: 7,
	}
	check := func(t *testing.T, samples []rain.Sample, show bool, hours, inches float64, paused bool) {
		t.Helper()
		s := kioskServer(t, nil)
		now := time.Now().UTC().Truncate(time.Second)
		pollRainAt(t, s, now, samples, base)
		got, body := getKiosk(t, s)
		if strings.Contains(body, "not-a-gauge") {
			t.Fatalf("leaked gauge id %s", body)
		}
		strip := obj(t, got["rain_strip"])
		rainObj := obj(t, got["rain"])
		if strip["show"] != show || strip["hours"] != hours {
			t.Fatalf("strip %v body %s", strip, body)
		}
		nearIn(t, strip["inches"], inches)
		if show && rainObj["have_totals"] != true {
			t.Fatalf("totals %v", rainObj)
		}
		if paused {
			pause := obj(t, got["pause"])
			if pause["paused"] != true || pause["source"] != "auto" || strip["show"] != false {
				t.Fatalf("rain pause %v strip %v", pause, strip)
			}
		}
	}

	now := time.Now().UTC().Truncate(time.Second)
	t.Run("0.044 hidden", func(t *testing.T) {
		check(t, []rain.Sample{
			{Time: now.Add(-2 * time.Hour), Inches: 0.044},
			{Time: now, Inches: 0},
		}, false, 72, 0.04, false)
	})
	t.Run("0.049 rounds up and shows as 24h", func(t *testing.T) {
		check(t, []rain.Sample{
			{Time: now.Add(-2 * time.Hour), Inches: 0.049},
			{Time: now, Inches: 0},
		}, true, 24, 0.05, false)
	})
	t.Run("0.05 shows as 24h", func(t *testing.T) {
		check(t, []rain.Sample{
			{Time: now.Add(-2 * time.Hour), Inches: 0.05},
			{Time: now, Inches: 0},
		}, true, 24, 0.05, false)
	})
	t.Run("0.05 older than a day shows as 72h", func(t *testing.T) {
		check(t, []rain.Sample{
			{Time: now.Add(-40 * time.Hour), Inches: 0.05},
			{Time: now, Inches: 0},
		}, true, 72, 0.05, false)
	})
	t.Run("rain pause hides the strip", func(t *testing.T) {
		check(t, []rain.Sample{
			{Time: now.Add(-time.Hour), Inches: 0.40},
			{Time: now, Inches: 0},
		}, false, 24, 0.40, true)
	})
}

func TestKioskSoilBars(t *testing.T) {
	loc := phoenixLoc(t)
	now := time.Date(2026, 9, 27, 15, 0, 0, 0, loc)
	mount := func(t *testing.T, fetched time.Time) *Server {
		t.Helper()
		s := kioskServer(t, nil)
		p := &soil.Poller{
			Cfg: soil.Config{Enabled: true, Station: "ref-et", WindowDays: 14},
			Now: func() time.Time { return now },
			Loc: loc,
		}
		p.SeedCacheForTest([]soil.DayET{{Date: "2026-09-27", ETInches: 0.20}}, fetched)
		s.Soil = p
		return s
	}

	t.Run("fresh shows percents", func(t *testing.T) {
		s := mount(t, now)
		soilRR := doJSON(t, s, http.MethodGet, "/api/soil", nil)
		var soilBody map[string]any
		if err := json.Unmarshal(soilRR.Body.Bytes(), &soilBody); err != nil {
			t.Fatal(err)
		}
		zones, _ := soilBody["zones"].([]any)
		if len(zones) == 0 || obj(t, zones[0])["percent"] == nil {
			t.Fatalf("soil view should still have percents %s", soilRR.Body.String())
		}
		got, body := getKiosk(t, s)
		if strings.Contains(body, "ref-et") {
			t.Fatalf("leaked et ref %s", body)
		}
		soilObj := obj(t, got["soil"])
		if soilObj["enabled"] != true || soilObj["et_known"] != true || soilObj["et_stale"] != false || soilObj["show_bars"] != true {
			t.Fatalf("soil %v", soilObj)
		}
		for _, raw := range got["stations"].([]any) {
			st := obj(t, raw)
			pct, ok := st["soil_percent"].(float64)
			if !ok || pct < 0 || pct > 100 {
				t.Fatalf("percent %v station %v", st["soil_percent"], st["id"])
			}
		}
	})

	t.Run("stale hides percents", func(t *testing.T) {
		s := mount(t, now.Add(-72*time.Hour))
		got, body := getKiosk(t, s)
		soilObj := obj(t, got["soil"])
		if soilObj["enabled"] != true || soilObj["et_known"] != true || soilObj["et_stale"] != true || soilObj["show_bars"] != false {
			t.Fatalf("soil %v body %s", soilObj, body)
		}
		if !strings.Contains(body, `"soil_percent":null`) {
			t.Fatalf("percent not null %s", body)
		}
		for _, raw := range got["stations"].([]any) {
			st := obj(t, raw)
			if st["soil_percent"] != nil {
				t.Fatalf("stale percent %v", st)
			}
		}
	})
}

func TestKioskReadOnly(t *testing.T) {
	s := kioskServer(t, kioskProgram(t))
	dir := filepath.Dir(s.Path)
	future := time.Now().Add(48 * time.Hour).UTC().Truncate(time.Second)
	if err := store.SavePause(s.Path, store.PauseState{Active: true, Until: &future, Reason: "away"}); err != nil {
		t.Fatal(err)
	}
	before := snapshotDir(t, dir)
	if s.Eng.IsPaused(time.Now()) {
		t.Fatal("engine picked up pause.json")
	}
	got, _ := getKiosk(t, s)
	if !bytesEqualDir(before, snapshotDir(t, dir)) {
		t.Fatal("GET /api/kiosk wrote the config directory")
	}
	if obj(t, got["pause"])["paused"] != false {
		t.Fatal("kiosk applied pause.json")
	}
	if s.Eng.IsPaused(time.Now()) {
		t.Fatal("kiosk armed the engine pause")
	}

	past := time.Now().Add(-2 * time.Hour).UTC().Truncate(time.Second)
	if err := store.SavePause(s.Path, store.PauseState{Active: true, Until: &past, Reason: "hold"}); err != nil {
		t.Fatal(err)
	}
	expired := snapshotDir(t, dir)
	_, _ = getKiosk(t, s)
	_, _ = getKiosk(t, s)
	if !bytesEqualDir(expired, snapshotDir(t, dir)) {
		t.Fatal("GET /api/kiosk rewrote an expired pause.json")
	}
}

func TestKioskConfigError(t *testing.T) {
	s := kioskServer(t, nil)
	s.Path = filepath.Join(t.TempDir(), "missing.json")
	rr := doJSON(t, s, http.MethodGet, "/api/kiosk", nil)
	if rr.Code != http.StatusInternalServerError {
		t.Fatalf("code %d %s", rr.Code, rr.Body.String())
	}
	var got map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	msg, _ := got["error"].(string)
	if msg == "" {
		t.Fatalf("error %v", got)
	}
}

func snapshotDir(t *testing.T, dir string) map[string][]byte {
	t.Helper()
	entries, err := os.ReadDir(dir)
	if err != nil {
		t.Fatal(err)
	}
	out := map[string][]byte{}
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		b, err := os.ReadFile(filepath.Join(dir, e.Name()))
		if err != nil {
			t.Fatal(err)
		}
		out[e.Name()] = b
	}
	return out
}

func bytesEqualDir(a, b map[string][]byte) bool {
	if len(a) != len(b) {
		return false
	}
	for k, av := range a {
		if !bytes.Equal(av, b[k]) {
			return false
		}
	}
	return true
}
