package api

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"flag"
	"net/http"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"syscall"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/gpio"
	"github.com/PixnBits/zanjerito/internal/history"
	"github.com/PixnBits/zanjerito/internal/rain"
	"github.com/PixnBits/zanjerito/internal/store"
)

// update rewrites internal/api/testdata/ha. Default is off.
// go test ./internal/api -update
// Do not pass -update to go test ./... — other packages do not define the flag.
var update = flag.Bool("update", false, "rewrite internal/api/testdata/ha golden files")

const (
	haTitle1      = "Title Alpha Zebra"
	haTitle2      = "Title Beta Zebra"
	haID1         = "station-alpha-id"
	haID2         = "station-beta-id"
	haSchedID     = "schedule-dawn-id"
	haSchedName   = "Program Dawn Name"
	haPowerTitle  = "Supply Power Name"
	haColor1      = "#abc123"
	haColor2      = "#def456"
	haEngineErr   = "engine-last-error-xyzzy"
	haRainErr     = "rain-feed-error-xyzzy"
	haHistErr     = "history-error-xyzzy"
	haHistReason  = "history-reason-xyzzy"
	haPauseReason = "pause-reason-xyzzy"
	haGauge       = "gauge-id-xyzzy"
)

func haClock(t *testing.T) time.Time {
	t.Helper()
	// Year 2000 is before the engine's wall-clock step start, so RunProgress
	// clamps elapsed to 0 and remaining seconds are the planned durations.
	return time.Date(2000, 1, 2, 8, 0, 0, 0, phoenixLoc(t))
}

func haCfg() engine.Config {
	al := true
	return engine.Config{
		Chip:      "gpiochip0",
		ActiveLow: &al,
		Timezone:  "America/Phoenix",
		MaxOnSec:  900,
		Power:     engine.StationConfig{ID: "psu", Title: haPowerTitle, Color: "#010101", BCM: 21, Physical: 40, WiringPi: 29},
		Stations: []engine.StationConfig{
			{ID: haID1, Title: haTitle1, Color: haColor1, BCM: 5, Physical: 29, WiringPi: 21},
			{ID: haID2, Title: haTitle2, Color: haColor2, BCM: 6, Physical: 31, WiringPi: 22},
		},
	}
}

func haSchedules() []store.Schedule {
	return []store.Schedule{{
		ID: haSchedID, Enabled: true, Note: haSchedName, Start: "09:30",
		Steps: []store.Step{
			{StationID: haID1, Minutes: 5},
			{StationID: haID2, Minutes: 7},
		},
	}}
}

func haServer(t *testing.T) *Server {
	t.Helper()
	cfg := haCfg()
	path := filepath.Join(t.TempDir(), "cfg.json")
	if err := store.Save(path, store.File{Config: cfg, Schedules: haSchedules()}); err != nil {
		t.Fatal(err)
	}
	s := kioskServerFrom(t, path, cfg)
	s.haNow = func() time.Time { return haClock(t) }
	return s
}

func kioskServerFrom(t *testing.T, path string, cfg engine.Config) *Server {
	t.Helper()
	e, err := engine.New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = e.Close() })
	return New(e, path)
}

func haHistory(t *testing.T, s *Server, fixed time.Time) {
	t.Helper()
	hl, err := history.Open(filepath.Join(filepath.Dir(s.Path), "history.json"), func() time.Time { return fixed })
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(hl.Close)
	start := time.Date(2000, 1, 2, 5, 0, 0, 0, fixed.Location())
	hl.Append(engine.RunRecord{
		ProgramID: haSchedID,
		Program:   haSchedName,
		Kind:      engine.KindSchedule,
		Stations:  []engine.StationRun{{StationID: haID1, PlannedSec: 300, ActualSec: 120}},
		Start:     start,
		End:       start.Add(time.Hour),
		Outcome:   engine.OutcomeStopped,
		Error:     haHistErr,
		Reason:    haHistReason,
	})
	s.History = hl
}

func haRainFeed(t *testing.T, s *Server, fixed time.Time, samples []rain.Sample, fetchErr error) {
	t.Helper()
	fake := &rain.Fake{}
	fake.Set(samples, fetchErr)
	p := &rain.Poller{
		Source: fake,
		Eng:    s.Eng,
		Path:   s.Path,
		Cfg: rain.Config{
			Enabled: true, GaugeID: haGauge,
			TriggerInches: 5, HeavyInches: 5, StaleHours: 7,
		},
		Now: func() time.Time { return fixed },
	}
	s.Rain = p
	p.Poll(context.Background())
}

func haScenario(t *testing.T, name string) *Server {
	t.Helper()
	s := haServer(t)
	fixed := haClock(t)
	switch name {
	case "idle":
		haHistory(t, s, fixed)
		s.Eng.SetLastErrorForTest(errors.New(haEngineErr))
	case "running":
		haHistory(t, s, fixed)
		haStartRun(t, s)
	case "rain-paused":
		haHistory(t, s, fixed)
		haRainFeed(t, s, fixed, []rain.Sample{
			{Time: fixed.Add(-30 * time.Hour), Inches: 0.25},
			{Time: fixed.Add(-2 * time.Hour), Inches: 0.50},
			{Time: fixed, Inches: 0},
		}, nil)
		until := time.Date(2000, 1, 2, 18, 0, 0, 0, fixed.Location())
		last := fixed.Add(-2 * time.Hour)
		s.Eng.SetPauseMeta(&until, "rain", engine.PauseMeta{
			Source: engine.PauseSourceAuto, RainInches: 0.5, LastRainAt: &last,
		})
	case "manual-paused":
		haHistory(t, s, fixed)
		haRainFeed(t, s, fixed, nil, errors.New(haRainErr))
		s.Eng.SetPause(nil, haPauseReason)
	case "no-history":
	default:
		t.Fatalf("unknown scenario %s", name)
	}
	return s
}

func haStartRun(t *testing.T, s *Server) {
	t.Helper()
	done := make(chan struct{})
	go func() {
		_ = s.Eng.RunProgram(context.Background(), haSchedID, haSchedName, []engine.Step{
			{StationID: haID1, Duration: 90 * time.Second},
			{StationID: haID2, Duration: 60 * time.Second},
		})
		close(done)
	}()
	t.Cleanup(func() {
		_ = s.Eng.Stop()
		select {
		case <-done:
		case <-time.After(2 * time.Second):
		}
	})
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		st := s.Eng.Status()
		prog, ok := s.Eng.RunProgress(time.Now())
		on := false
		for _, id := range st.StationsOn {
			if id == haID1 {
				on = true
			}
		}
		if st.Phase == engine.PhaseStationOn && on && ok && prog.StepIndex == 0 {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatalf("run did not reach step 0: %+v", s.Eng.Status())
}

func getHA(t *testing.T, s *Server) []byte {
	t.Helper()
	rr := doJSON(t, s, http.MethodGet, "/api/ha", nil)
	if rr.Code != 200 {
		t.Fatalf("ha %d %s", rr.Code, rr.Body.String())
	}
	if ct := rr.Header().Get("Content-Type"); ct != "application/json" {
		t.Fatalf("content-type %q", ct)
	}
	if cc := rr.Header().Get("Cache-Control"); cc != "no-store" {
		t.Fatalf("cache %q", cc)
	}
	return rr.Body.Bytes()
}

func TestHAGolden(t *testing.T) {
	for _, name := range []string{"idle", "running", "rain-paused", "manual-paused", "no-history"} {
		t.Run(name, func(t *testing.T) {
			body := getHA(t, haScenario(t, name))
			assertHAShape(t, body)
			checkHAState(t, name, body)
			compareHAGolden(t, name, body)
		})
	}
}

func TestHAUnknownLastRun(t *testing.T) {
	s := haServer(t)
	fixed := haClock(t)
	hl, err := history.Open(filepath.Join(filepath.Dir(s.Path), "history.json"), func() time.Time { return fixed })
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(hl.Close)
	start := time.Date(2000, 1, 2, 5, 0, 0, 0, fixed.Location())
	hl.Append(engine.RunRecord{
		Kind:    "not-a-kind",
		Outcome: "not-an-outcome",
		Start:   start,
		End:     start.Add(time.Hour),
	})
	s.History = hl
	body := getHA(t, s)
	var p haBody
	if err := json.Unmarshal(body, &p); err != nil {
		t.Fatal(err)
	}
	if p.LastRunKind == nil || *p.LastRunKind != "unknown" || p.LastRunOut == nil || *p.LastRunOut != "unknown" {
		t.Fatalf("kind/outcome %+v %+v body %s", p.LastRunKind, p.LastRunOut, body)
	}
	if p.LastRunStart == nil || p.LastRunEnd == nil {
		t.Fatalf("history times nulled %s", body)
	}
	if strings.Contains(string(body), "not-a-kind") || strings.Contains(string(body), "not-an-outcome") {
		t.Fatalf("raw label leaked %s", body)
	}
}

// TestHALastRunAllowlist stores every label haLastRun passes to haToken.
// Dropping one from that allowlist reports "unknown" and fails the case.
func TestHALastRunAllowlist(t *testing.T) {
	kinds := []string{engine.KindSchedule, engine.KindManual}
	outcomes := []string{
		engine.OutcomeCompleted,
		engine.OutcomeStopped,
		engine.OutcomeSkipped,
		engine.OutcomeRefused,
		engine.OutcomeError,
	}
	for _, kind := range kinds {
		for _, outcome := range outcomes {
			t.Run(kind+"/"+outcome, func(t *testing.T) {
				s := haServer(t)
				haAppendLabel(t, s, kind, outcome)
				assertHALastRun(t, getHA(t, s), kind, outcome)
			})
		}
	}
}

func TestHAEmptyLastRunUnknown(t *testing.T) {
	cases := []struct {
		name, kind, outcome, wantKind, wantOutcome string
	}{
		{"empty kind", "", engine.OutcomeCompleted, "unknown", engine.OutcomeCompleted},
		{"empty outcome", engine.KindManual, "", engine.KindManual, "unknown"},
		{"both empty", "", "", "unknown", "unknown"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			s := haServer(t)
			haAppendLabel(t, s, tc.kind, tc.outcome)
			assertHALastRun(t, getHA(t, s), tc.wantKind, tc.wantOutcome)
		})
	}
	t.Run("no history", func(t *testing.T) {
		body := getHA(t, haServer(t))
		var p haBody
		if err := json.Unmarshal(body, &p); err != nil {
			t.Fatal(err)
		}
		if p.LastRunKind != nil || p.LastRunOut != nil || p.LastRunStart != nil || p.LastRunEnd != nil {
			t.Fatalf("no history should stay null %s", body)
		}
	})
}

// TestHAEmptyHistoryFile opens a history.json that exists and is empty.
// That is the len==0 return in haLastRun, not a nil History.
func TestHAEmptyHistoryFile(t *testing.T) {
	s := haServer(t)
	path := filepath.Join(filepath.Dir(s.Path), "history.json")
	if err := os.WriteFile(path, []byte{}, 0o644); err != nil {
		t.Fatal(err)
	}
	info, err := os.Stat(path)
	if err != nil {
		t.Fatal(err)
	}
	if info.Size() != 0 {
		t.Fatalf("history file %d bytes", info.Size())
	}
	hl, err := history.Open(path, func() time.Time { return haClock(t) })
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(hl.Close)
	if len(hl.List(1)) != 0 {
		t.Fatal("empty history file loaded entries")
	}
	s.History = hl
	body := getHA(t, s)
	var p haBody
	if err := json.Unmarshal(body, &p); err != nil {
		t.Fatal(err)
	}
	if p.LastRunKind != nil || p.LastRunOut != nil || p.LastRunStart != nil || p.LastRunEnd != nil {
		t.Fatalf("empty history log should stay null %s", body)
	}
	for _, key := range []string{"last_run_kind", "last_run_outcome", "last_run_started_at", "last_run_ended_at"} {
		if !bytes.Contains(body, []byte(`"`+key+`":null`)) {
			t.Fatalf("%s not null in %s", key, body)
		}
	}
}

func TestHALastRunWhitespaceCase(t *testing.T) {
	cases := []struct {
		name, kind, outcome string
	}{
		{"leading space", " manual", " completed"},
		{"title case", "Manual", "Completed"},
		{"trailing newline", "manual\n", "completed\n"},
		{"upper swapped label", "COMPLETED", "SCHEDULE"},
		{"upper kind", "MANUAL", "STOPPED"},
		{"upper schedule", "SCHEDULE", "SKIPPED"},
		{"trailing space", "manual ", "refused "},
		{"leading newline", "\nmanual", "\nerror"},
		{"mixed case", "sChEdUlE", "eRrOr"},
		{"tab", "\tmanual", "completed\t"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			s := haServer(t)
			haAppendLabel(t, s, tc.kind, tc.outcome)
			assertHALastRun(t, getHA(t, s), "unknown", "unknown")
		})
	}
}

func haAppendLabel(t *testing.T, s *Server, kind, outcome string) {
	t.Helper()
	fixed := haClock(t)
	hl, err := history.Open(filepath.Join(filepath.Dir(s.Path), "history.json"), func() time.Time { return fixed })
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(hl.Close)
	start := time.Date(2000, 1, 2, 5, 0, 0, 0, fixed.Location())
	hl.Append(engine.RunRecord{
		Kind:    kind,
		Outcome: outcome,
		Start:   start,
		End:     start.Add(time.Hour),
	})
	s.History = hl
}

func assertHALastRun(t *testing.T, body []byte, kind, outcome string) {
	t.Helper()
	var p haBody
	if err := json.Unmarshal(body, &p); err != nil {
		t.Fatal(err)
	}
	if p.LastRunKind == nil || *p.LastRunKind != kind || p.LastRunOut == nil || *p.LastRunOut != outcome {
		t.Fatalf("kind/outcome %+v %+v want %q %q body %s", p.LastRunKind, p.LastRunOut, kind, outcome, body)
	}
	if p.LastRunStart == nil || p.LastRunEnd == nil {
		t.Fatalf("history times nulled %s", body)
	}
}

func TestHANoLeak(t *testing.T) {
	forbidden := []string{
		haTitle1, haTitle2, haID1, haID2, haSchedID, haSchedName, haPowerTitle,
		haColor1, haColor2, haEngineErr, haRainErr, haHistErr, haHistReason,
		haPauseReason, haGauge, "wiringPi", "WiringPi", "physical", "\"bcm\"", "\"power\"",
	}
	for _, name := range []string{"idle", "running", "rain-paused", "manual-paused", "no-history"} {
		t.Run(name, func(t *testing.T) {
			body := string(getHA(t, haScenario(t, name)))
			for _, leak := range forbidden {
				if strings.Contains(body, leak) {
					t.Fatalf("leaked %q in %s", leak, body)
				}
			}
		})
	}
}

func TestHANoWrite(t *testing.T) {
	for _, name := range []string{"idle", "running", "rain-paused", "manual-paused", "no-history"} {
		t.Run(name, func(t *testing.T) {
			s := haScenario(t, name)
			before := haSnapshot(t, s)
			_ = getHA(t, s)
			if after := haSnapshot(t, s); !reflect.DeepEqual(before, after) {
				t.Fatalf("GET changed state\nbefore %+v\nafter  %+v", before, after)
			}
		})
	}

	t.Run("expired pause stays stored", func(t *testing.T) {
		s := haServer(t)
		fixed := haClock(t)
		past := fixed.Add(-time.Hour)
		if err := store.SavePause(s.Path, store.PauseState{Active: true, Until: &past, Reason: haPauseReason}); err != nil {
			t.Fatal(err)
		}
		s.Eng.SetPause(&past, haPauseReason)
		if !s.Eng.PauseRaw().Paused {
			t.Fatal("setup pause missing")
		}
		dir := filepath.Dir(s.Path)
		files := snapshotDir(t, dir)
		ids := haFileIDs(t, dir)
		raw := s.Eng.PauseRaw()
		body := getHA(t, s)
		if !bytesEqualDir(files, snapshotDir(t, dir)) {
			t.Fatal("GET /api/ha wrote the config directory")
		}
		if got := haFileIDs(t, dir); !reflect.DeepEqual(ids, got) {
			t.Fatalf("file mtime or inode changed\nbefore %+v\nafter  %+v", ids, got)
		}
		if got := s.Eng.PauseRaw(); !pauseEqual(raw, got) {
			t.Fatalf("pause changed %+v -> %+v", raw, got)
		}
		var parsed haBody
		if err := json.Unmarshal(body, &parsed); err != nil {
			t.Fatal(err)
		}
		if parsed.Paused {
			t.Fatalf("expired pause reported paused: %s", body)
		}
	})

	t.Run("methods", func(t *testing.T) {
		s := haServer(t)
		before := haSnapshot(t, s)
		for _, method := range []string{
			http.MethodPost, http.MethodPut, http.MethodPatch,
			http.MethodDelete, http.MethodHead, http.MethodOptions,
		} {
			rr := doJSON(t, s, method, "/api/ha", nil)
			if rr.Code != http.StatusMethodNotAllowed {
				t.Fatalf("%s %d %s", method, rr.Code, rr.Body.String())
			}
			if got := rr.Header().Get("Allow"); got != http.MethodGet {
				t.Fatalf("%s Allow %q", method, got)
			}
		}
		if after := haSnapshot(t, s); !reflect.DeepEqual(before, after) {
			t.Fatalf("non-GET changed state\nbefore %+v\nafter  %+v", before, after)
		}
	})
}

type haSnap struct {
	Phase     string
	LastError string
	On        []string
	Pause     engine.PauseSnap
	Hist      []history.Entry
	Files     map[string][]byte
	FileIDs   map[string]haFileID
	RainOn    bool
	RainBad   bool
	RainErr   string
	RainOK    string
	RainTotal float64
	RainHave  bool
	Running   bool
	Step      int
}

func haSnapshot(t *testing.T, s *Server) haSnap {
	t.Helper()
	st := s.Eng.Status()
	on := append([]string(nil), st.StationsOn...)
	sortStrings(on)
	hist := []history.Entry{}
	if s.History != nil {
		hist = s.History.List(0)
	}
	dir := filepath.Dir(s.Path)
	snap := haSnap{
		Phase:     string(st.Phase),
		LastError: st.LastError,
		On:        on,
		Pause:     s.Eng.PauseRaw(),
		Hist:      hist,
		Files:     snapshotDir(t, dir),
		FileIDs:   haFileIDs(t, dir),
	}
	if s.Rain != nil {
		rs := s.Rain.Status()
		snap.RainOn = rs.Enabled
		snap.RainBad = rs.Unavailable
		snap.RainErr = rs.LastError
		snap.RainHave = rs.HaveTotal
		snap.RainTotal = rs.LastTotal
		if rs.LastOK != nil {
			snap.RainOK = rs.LastOK.UTC().Format(time.RFC3339Nano)
		}
	}
	if prog, ok := s.Eng.RunProgress(time.Now()); ok {
		snap.Running = true
		snap.Step = prog.StepIndex
	}
	return snap
}

// haFileID is one watched file's mtime and inode, from os.Stat.
type haFileID struct {
	MtimeUnixNano int64
	Ino           uint64
}

func haFileIDs(t *testing.T, dir string) map[string]haFileID {
	t.Helper()
	entries, err := os.ReadDir(dir)
	if err != nil {
		t.Fatal(err)
	}
	out := map[string]haFileID{}
	for _, e := range entries {
		if e.IsDir() {
			continue
		}
		info, err := os.Stat(filepath.Join(dir, e.Name()))
		if err != nil {
			t.Fatal(err)
		}
		st, ok := info.Sys().(*syscall.Stat_t)
		if !ok {
			t.Fatalf("%s: os.Stat Sys type %T", e.Name(), info.Sys())
		}
		out[e.Name()] = haFileID{MtimeUnixNano: info.ModTime().UnixNano(), Ino: st.Ino}
	}
	return out
}

func pauseEqual(a, b engine.PauseSnap) bool {
	if a.Paused != b.Paused || a.Reason != b.Reason || a.Source != b.Source || a.RainInches != b.RainInches {
		return false
	}
	return sameTime(a.Until, b.Until) && sameTime(a.LastRainAt, b.LastRainAt)
}

func sameTime(a, b *time.Time) bool {
	if a == nil || b == nil {
		return a == nil && b == nil
	}
	return a.Equal(*b)
}

func sortStrings(s []string) {
	for i := 1; i < len(s); i++ {
		for j := i; j > 0 && s[j] < s[j-1]; j-- {
			s[j], s[j-1] = s[j-1], s[j]
		}
	}
}

var haKeys = []string{
	"phase", "paused", "pause_source", "paused_until", "lockout", "has_error",
	"active_zone", "zones_on", "step_remaining_sec", "run_remaining_sec",
	"next_run_at", "next_run_ends_at", "next_run_total_min", "next_run_skipped_by_pause",
	"rain_enabled", "rain_unavailable", "rain_last_ok_at", "rain_24h_in", "rain_72h_in",
	"last_run_kind", "last_run_outcome", "last_run_started_at", "last_run_ended_at",
	"zones",
}

func assertHAShape(t *testing.T, body []byte) {
	t.Helper()
	dec := json.NewDecoder(bytes.NewReader(body))
	dec.DisallowUnknownFields()
	var parsed haBody
	if err := dec.Decode(&parsed); err != nil {
		t.Fatalf("decode %v body %s", err, body)
	}
	var m map[string]json.RawMessage
	if err := json.Unmarshal(body, &m); err != nil {
		t.Fatal(err)
	}
	if len(m) != len(haKeys) {
		t.Fatalf("key count %d body %s", len(m), body)
	}
	for _, k := range haKeys {
		if _, ok := m[k]; !ok {
			t.Fatalf("missing %s in %s", k, body)
		}
	}
	if parsed.Zones == nil {
		t.Fatal("zones null")
	}
}

func checkHAState(t *testing.T, name string, body []byte) {
	t.Helper()
	var p haBody
	if err := json.Unmarshal(body, &p); err != nil {
		t.Fatal(err)
	}
	if p.NextRunAt == nil || p.NextRunEndsAt == nil || p.NextRunMin != 12 {
		t.Fatalf("%s next %+v", name, p)
	}
	if len(p.Zones) != 2 || p.Zones[0].ID != "zone_1" || p.Zones[1].ID != "zone_2" {
		t.Fatalf("%s zones %+v", name, p.Zones)
	}
	switch name {
	case "idle":
		if p.Phase != "Idle" || !p.HasError || p.Paused || p.ActiveZone != nil || p.ZonesOn != 0 || p.StepRemaining != 0 || p.RunRemaining != 0 || p.NextSkipped {
			t.Fatalf("idle %+v", p)
		}
		if p.LastRunKind == nil || p.LastRunOut == nil || *p.LastRunOut != "stopped" {
			t.Fatalf("idle history %+v", p)
		}
	case "running":
		if p.Phase != "StationOn" || p.HasError || p.ActiveZone == nil || *p.ActiveZone != "zone_1" || p.ZonesOn != 1 || !p.Zones[0].On || p.Zones[1].On {
			t.Fatalf("running %+v", p)
		}
		if p.StepRemaining <= 0 || p.RunRemaining <= p.StepRemaining {
			t.Fatalf("running remain step %d run %d", p.StepRemaining, p.RunRemaining)
		}
	case "rain-paused":
		if !p.Paused || p.PauseSource != "auto" || p.PausedUntil == nil || !p.NextSkipped || !p.RainEnabled || p.RainUnavail || p.RainLastOK == nil {
			t.Fatalf("rain %+v", p)
		}
		if p.Rain24 <= 0 || p.Rain72 < p.Rain24 {
			t.Fatalf("rain inches %v %v", p.Rain24, p.Rain72)
		}
	case "manual-paused":
		if !p.Paused || p.PauseSource != "manual" || p.PausedUntil != nil || !p.NextSkipped || !p.RainEnabled || !p.RainUnavail || p.RainLastOK != nil {
			t.Fatalf("manual %+v", p)
		}
		if p.Rain24 != 0 || p.Rain72 != 0 {
			t.Fatalf("manual inches %v %v", p.Rain24, p.Rain72)
		}
	case "no-history":
		if p.Phase != "Idle" || p.HasError || p.LastRunKind != nil || p.LastRunOut != nil || p.LastRunStart != nil || p.LastRunEnd != nil {
			t.Fatalf("no-history %+v", p)
		}
	}
}

func compareHAGolden(t *testing.T, name string, body []byte) {
	t.Helper()
	path := filepath.Join("testdata", "ha", name+".json")
	if *update {
		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(path, body, 0o644); err != nil {
			t.Fatal(err)
		}
	}
	want, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(body, want) {
		t.Fatalf("%s mismatch\ngot  %s\nwant %s", name, body, want)
	}
}
