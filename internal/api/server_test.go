package api

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/gpio"
	"github.com/PixnBits/zanjerito/internal/store"
)

func validCfg() engine.Config {
	al := true
	return engine.Config{
		Chip:      "gpiochip0",
		ActiveLow: &al,
		Timezone:  "America/Phoenix",
		MaxOnSec:  900,
		Power:     engine.StationConfig{ID: "psu", BCM: 21},
		Stations: []engine.StationConfig{
			{ID: "front-north", Title: "Front North", BCM: 6},
			{ID: "front-south", Title: "Front South", BCM: 5},
		},
	}
}

func newTestServer(t *testing.T) *Server {
	t.Helper()
	path := filepath.Join(t.TempDir(), "cfg.json")
	f := store.File{
		Config:    validCfg(),
		Schedules: []store.Schedule{{ID: "dawn", Enabled: true, Note: "keep"}},
	}
	if err := store.Save(path, f); err != nil {
		t.Fatal(err)
	}
	e, err := engine.New(f.Config, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = e.Close() })
	return New(e, path)
}

func doJSON(t *testing.T, s *Server, method, path string, body any) *httptest.ResponseRecorder {
	t.Helper()
	var rdr io.Reader
	if body != nil {
		b, err := json.Marshal(body)
		if err != nil {
			t.Fatal(err)
		}
		rdr = bytes.NewReader(b)
	}
	req := httptest.NewRequest(method, path, rdr)
	if body != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	rr := httptest.NewRecorder()
	s.ServeHTTP(rr, req)
	return rr
}

func TestStatusIdle(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/api/status", nil)
	if rr.Code != 200 {
		t.Fatalf("status %d %s", rr.Code, rr.Body.String())
	}
	var got map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	if got["phase"] != "Idle" {
		t.Fatalf("phase %v", got["phase"])
	}
	if got["timezone"] != "America/Phoenix" {
		t.Fatalf("tz %v", got["timezone"])
	}
	if got["lockout"] != false {
		t.Fatalf("lockout %v", got["lockout"])
	}
}

func TestStationsGetPatch(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/api/stations/front-north", nil)
	if rr.Code != 200 || !strings.Contains(rr.Body.String(), "Front North") {
		t.Fatalf("get %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodPatch, "/api/stations/front-north", map[string]any{"title": "North Lawn"})
	if rr.Code != 200 || !strings.Contains(rr.Body.String(), "North Lawn") {
		t.Fatalf("patch %d %s", rr.Code, rr.Body.String())
	}
}

func TestSchedulesGetPut(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/api/schedules/dawn", nil)
	if rr.Code != 200 {
		t.Fatalf("get %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodPut, "/api/schedules/dusk", map[string]any{
		"id": "dusk", "enabled": true, "note": "new",
	})
	if rr.Code != 200 {
		t.Fatalf("put %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodGet, "/api/schedules", nil)
	if rr.Code != 200 || !strings.Contains(rr.Body.String(), "dusk") {
		t.Fatalf("list %d %s", rr.Code, rr.Body.String())
	}
}

func TestRunCancelAndBusyReject(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodPost, "/api/stations/front-north/run", map[string]any{"durationSec": 2})
	if rr.Code != http.StatusAccepted {
		t.Fatalf("run %d %s", rr.Code, rr.Body.String())
	}
	deadline := time.Now().Add(500 * time.Millisecond)
	for time.Now().Before(deadline) {
		if s.Eng.Status().Phase != engine.PhaseIdle {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	rr = doJSON(t, s, http.MethodPatch, "/api/stations/front-north", map[string]any{"title": "nope"})
	if rr.Code != http.StatusConflict || !strings.Contains(rr.Body.String(), "busy") {
		t.Fatalf("patch-while-run want 409 busy, got %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodPut, "/api/schedules/dawn", map[string]any{"id": "dawn", "enabled": false})
	if rr.Code != http.StatusConflict {
		t.Fatalf("put-while-run want 409, got %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodPost, "/api/stations/front-north/run", map[string]any{"durationSec": 1})
	if rr.Code != http.StatusConflict {
		t.Fatalf("second run want 409, got %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodPost, "/api/run/cancel", nil)
	if rr.Code != 200 {
		t.Fatalf("cancel %d %s", rr.Code, rr.Body.String())
	}
}

func TestPreempt202OnlyWhenRunStarts(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodPost, "/api/stations/front-north/run", map[string]any{"durationSec": 5})
	if rr.Code != http.StatusAccepted {
		t.Fatalf("first run %d %s", rr.Code, rr.Body.String())
	}
	deadline := time.Now().Add(500 * time.Millisecond)
	for time.Now().Before(deadline) {
		if s.Eng.Status().Phase != engine.PhaseIdle {
			break
		}
		time.Sleep(5 * time.Millisecond)
	}
	if s.Eng.Status().Phase == engine.PhaseIdle {
		t.Fatal("first run never left Idle")
	}
	rr = doJSON(t, s, http.MethodPost, "/api/stations/front-south/run", map[string]any{
		"durationSec": 3, "preempt": true,
	})
	if rr.Code != http.StatusAccepted {
		t.Fatalf("preempt want 202 after start, got %d %s", rr.Code, rr.Body.String())
	}
	deadline = time.Now().Add(2 * time.Second)
	gotSouth := false
	for time.Now().Before(deadline) {
		st := s.Eng.Status()
		if st.Phase == engine.PhaseIdle {
			t.Fatalf("202 but engine idle (swallowed start): %+v", st)
		}
		for _, id := range st.StationsOn {
			if id == "front-south" {
				gotSouth = true
			}
		}
		if st.CurrentStation == "front-south" || gotSouth {
			gotSouth = true
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	if !gotSouth {
		t.Fatalf("202 but new run did not start (status %+v)", s.Eng.Status())
	}
	rr = doJSON(t, s, http.MethodPost, "/api/stations/front-north/run", map[string]any{"durationSec": 1})
	if rr.Code != http.StatusConflict {
		t.Fatalf("non-preempt during new run want 409, got %d %s", rr.Code, rr.Body.String())
	}
}

func TestEventsSSE(t *testing.T) {
	s := newTestServer(t)
	ts := httptest.NewServer(s)
	defer ts.Close()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, ts.URL+"/api/events", nil)
	if err != nil {
		t.Fatal(err)
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if ct := resp.Header.Get("Content-Type"); !strings.Contains(ct, "text/event-stream") {
		t.Fatalf("content-type %q", ct)
	}
	buf := make([]byte, 512)
	n, err := resp.Body.Read(buf)
	if n == 0 && err != nil {
		t.Fatal(err)
	}
	cancel()
	body := string(buf[:n])
	if !strings.Contains(body, "event: status") {
		t.Fatalf("sse body %q", body)
	}
}

func TestPauseTimedIndefiniteResumeAndStatus(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/api/status", nil)
	var st map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &st); err != nil {
		t.Fatal(err)
	}
	if st["paused"] != false {
		t.Fatalf("want paused false, got %v", st["paused"])
	}
	if st["paused_until"] != nil {
		t.Fatalf("paused_until %v", st["paused_until"])
	}

	rr = doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"duration_sec": 120, "reason": "mow"})
	if rr.Code != 200 {
		t.Fatalf("timed pause %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodGet, "/api/status", nil)
	if err := json.Unmarshal(rr.Body.Bytes(), &st); err != nil {
		t.Fatal(err)
	}
	if st["paused"] != true {
		t.Fatalf("paused %v", st["paused"])
	}
	if st["paused_until"] == nil || st["paused_until"] == "" {
		t.Fatal("want paused_until RFC3339")
	}
	if st["reason"] != "mow" {
		t.Fatalf("reason %v", st["reason"])
	}

	rr = doJSON(t, s, http.MethodPost, "/api/stations/front-north/run", map[string]any{"durationSec": 1})
	if rr.Code != http.StatusConflict || !strings.Contains(rr.Body.String(), "paused") {
		t.Fatalf("manual run while paused want 409, got %d %s", rr.Code, rr.Body.String())
	}

	rr = doJSON(t, s, http.MethodDelete, "/api/pause", nil)
	if rr.Code != 200 {
		t.Fatalf("resume %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodGet, "/api/status", nil)
	if err := json.Unmarshal(rr.Body.Bytes(), &st); err != nil {
		t.Fatal(err)
	}
	if st["paused"] != false {
		t.Fatalf("after resume paused=%v", st["paused"])
	}

	rr = doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"indefinite": true})
	if rr.Code != 200 {
		t.Fatalf("indefinite %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodGet, "/api/status", nil)
	if err := json.Unmarshal(rr.Body.Bytes(), &st); err != nil {
		t.Fatal(err)
	}
	if st["paused"] != true {
		t.Fatal("indefinite paused")
	}
	if st["paused_until"] != nil {
		t.Fatalf("indefinite until must be null, got %v", st["paused_until"])
	}

	rr = doJSON(t, s, http.MethodPost, "/api/pause/resume", nil)
	if rr.Code != 200 {
		t.Fatalf("resume alias %d", rr.Code)
	}
}

func TestPauseCancelsActiveRunWithoutLeavingPausedOnStop(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodPost, "/api/stations/front-north/run", map[string]any{"durationSec": 5})
	if rr.Code != http.StatusAccepted {
		t.Fatalf("run %d %s", rr.Code, rr.Body.String())
	}
	deadline := time.Now().Add(500 * time.Millisecond)
	for time.Now().Before(deadline) {
		if s.Eng.Status().Phase != engine.PhaseIdle {
			break
		}
		time.Sleep(5 * time.Millisecond)
	}
	rr = doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"duration_sec": 60})
	if rr.Code != 200 {
		t.Fatalf("pause %d %s", rr.Code, rr.Body.String())
	}
	deadline = time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		if s.Eng.Status().Phase == engine.PhaseIdle {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	if s.Eng.Status().Phase != engine.PhaseIdle {
		t.Fatalf("pause should cancel run, phase=%s", s.Eng.Status().Phase)
	}
	if !s.Eng.IsPaused(time.Now()) {
		t.Fatal("should be paused")
	}
	// STOP while Idle+paused: cancel only, leave pause armed
	rr = doJSON(t, s, http.MethodPost, "/api/run/cancel", nil)
	if rr.Code != 200 {
		t.Fatalf("stop %d", rr.Code)
	}
	if !s.Eng.IsPaused(time.Now()) {
		t.Fatal("STOP must not clear pause")
	}
}

func TestPauseAutoExpireAPI(t *testing.T) {
	s := newTestServer(t)
	past := time.Now().Add(-2 * time.Second)
	s.Eng.SetPause(&past, "old")
	if err := store.SavePause(s.Path, store.PauseState{Active: true, Until: &past, Reason: "old"}); err != nil {
		t.Fatal(err)
	}
	rr := doJSON(t, s, http.MethodGet, "/api/status", nil)
	var st map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &st); err != nil {
		t.Fatal(err)
	}
	if st["paused"] != false {
		t.Fatalf("expired should report paused=false, got %v", st)
	}
}

func decodeMap(t *testing.T, rr *httptest.ResponseRecorder) map[string]any {
	t.Helper()
	var m map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &m); err != nil {
		t.Fatalf("json %v body %s", err, rr.Body.String())
	}
	return m
}

func phoenixLoc(t *testing.T) *time.Location {
	t.Helper()
	loc, err := time.LoadLocation("America/Phoenix")
	if err != nil {
		t.Fatal(err)
	}
	return loc
}

func TestPauseLongDurations(t *testing.T) {
	loc := phoenixLoc(t)
	s := newTestServer(t)
	tests := []struct {
		name string
		body map[string]any
		days int
	}{
		{"days 2", map[string]any{"days": 2}, 2},
		{"days 7", map[string]any{"days": 7}, 7},
		{"days 14", map[string]any{"days": 14}, 14},
		{"tomorrow_morning", map[string]any{"until": "tomorrow_morning"}, 1},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			_ = doJSON(t, s, http.MethodDelete, "/api/pause", nil)
			rr := doJSON(t, s, http.MethodPost, "/api/pause", tc.body)
			if rr.Code != 200 {
				t.Fatalf("%d %s", rr.Code, rr.Body.String())
			}
			st := decodeMap(t, rr)
			pu, _ := st["paused_until"].(string)
			if !strings.HasSuffix(pu, "-07:00") {
				t.Fatalf("paused_until %q want -07:00", pu)
			}
			got, err := time.Parse(time.RFC3339, pu)
			if err != nil {
				t.Fatal(err)
			}
			if got.Hour() != 6 || got.Minute() != 0 {
				t.Fatalf("cutoff hour/min %s", got)
			}
			now := time.Now().In(loc)
			y, m, d := now.Date()
			wantDay := time.Date(y, m, d+tc.days, 6, 0, 0, 0, loc)
			gy, gm, gd := got.In(loc).Date()
			wy, wm, wd := wantDay.Date()
			if gy != wy || gm != wm || gd != wd {
				t.Fatalf("date %04d-%02d-%02d want %04d-%02d-%02d", gy, gm, gd, wy, wm, wd)
			}
			label, _ := st["paused_label"].(string)
			if !strings.HasPrefix(label, "Paused until ") {
				t.Fatalf("paused_label %q", label)
			}
		})
	}
}

func TestPauseDaysBounds(t *testing.T) {
	s := newTestServer(t)
	for _, d := range []int{0, -1, 15, 100} {
		rr := doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"days": d})
		if rr.Code != 400 {
			t.Fatalf("days=%d want 400, got %d %s", d, rr.Code, rr.Body.String())
		}
		if !strings.Contains(rr.Body.String(), "days must be between 1 and 14") {
			t.Fatalf("days=%d error %s", d, rr.Body.String())
		}
	}
	rr := doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"days": 1})
	if rr.Code != 200 {
		t.Fatalf("days=1 %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodDelete, "/api/pause", nil)
	if rr.Code != 200 {
		t.Fatalf("resume %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"days": 14})
	if rr.Code != 200 {
		t.Fatalf("days=14 %d %s", rr.Code, rr.Body.String())
	}
}

func TestPauseConflictingFields(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{
		"days": 2, "duration_sec": 60,
	})
	if rr.Code != 400 {
		t.Fatalf("want 400, got %d %s", rr.Code, rr.Body.String())
	}
	if !strings.Contains(rr.Body.String(), "choose one of duration_sec, days, until, indefinite") {
		t.Fatalf("error %s", rr.Body.String())
	}
}

func TestPauseDurationSecStillWorks(t *testing.T) {
	s := newTestServer(t)
	before := time.Now()
	rr := doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"duration_sec": 3600})
	after := time.Now()
	if rr.Code != 200 {
		t.Fatalf("%d %s", rr.Code, rr.Body.String())
	}
	st := decodeMap(t, rr)
	pu, _ := st["paused_until"].(string)
	if !strings.HasSuffix(pu, "-07:00") {
		t.Fatalf("paused_until %q", pu)
	}
	got, err := time.Parse(time.RFC3339, pu)
	if err != nil {
		t.Fatal(err)
	}
	lo := before.Add(3600*time.Second - 5*time.Second)
	hi := after.Add(3600*time.Second + 5*time.Second)
	if got.Before(lo) || got.After(hi) {
		t.Fatalf("until %s not within ±5s of now+1h [%s, %s]", got, lo, hi)
	}
}

func TestPausedUntilLocalOffsetInAllResponses(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"days": 2})
	if rr.Code != 200 {
		t.Fatalf("post %d %s", rr.Code, rr.Body.String())
	}
	check := func(src string, raw []byte) {
		t.Helper()
		var m map[string]any
		if err := json.Unmarshal(raw, &m); err != nil {
			t.Fatalf("%s json %v body %s", src, err, raw)
		}
		pu, _ := m["paused_until"].(string)
		if !strings.HasSuffix(pu, "-07:00") {
			t.Fatalf("%s paused_until %q want -07:00", src, pu)
		}
		if strings.Contains(pu, "Z") {
			t.Fatalf("%s paused_until %q must not use Z", src, pu)
		}
	}
	check("POST /api/pause", rr.Body.Bytes())

	rr = doJSON(t, s, http.MethodGet, "/api/status", nil)
	if rr.Code != 200 {
		t.Fatalf("status %d %s", rr.Code, rr.Body.String())
	}
	check("GET /api/status", rr.Body.Bytes())

	ts := httptest.NewServer(s)
	defer ts.Close()
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, ts.URL+"/api/events", nil)
	if err != nil {
		t.Fatal(err)
	}
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	buf := make([]byte, 2048)
	n, err := resp.Body.Read(buf)
	if n == 0 && err != nil {
		t.Fatal(err)
	}
	cancel()
	body := string(buf[:n])
	var data string
	for _, line := range strings.Split(body, "\n") {
		if strings.HasPrefix(line, "data: ") {
			data = strings.TrimPrefix(line, "data: ")
			break
		}
	}
	if data == "" {
		t.Fatalf("no SSE data in %q", body)
	}
	check("SSE status", []byte(data))
}

func TestPauseRestartPersistence(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"days": 7})
	if rr.Code != 200 {
		t.Fatalf("post %d %s", rr.Code, rr.Body.String())
	}
	orig := decodeMap(t, rr)
	origUntil, err := time.Parse(time.RFC3339, orig["paused_until"].(string))
	if err != nil {
		t.Fatal(err)
	}
	loaded, err := store.LoadPause(s.Path, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if !loaded.Active || loaded.Until == nil {
		t.Fatalf("LoadPause %+v", loaded)
	}
	e2, err := engine.New(validCfg(), gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = e2.Close() })
	e2.SetPause(loaded.Until, loaded.Reason)
	s2 := New(e2, s.Path)
	rr = doJSON(t, s2, http.MethodGet, "/api/status", nil)
	st := decodeMap(t, rr)
	if st["paused"] != true {
		t.Fatalf("restart paused %v", st["paused"])
	}
	got, err := time.Parse(time.RFC3339, st["paused_until"].(string))
	if err != nil {
		t.Fatal(err)
	}
	if !got.Equal(origUntil) {
		t.Fatalf("paused_until %s want %s", got, origUntil)
	}
}

func TestPauseLoadsLegacyUTCFile(t *testing.T) {
	s := newTestServer(t)
	future := time.Now().UTC().Add(48 * time.Hour).Truncate(time.Second)
	z := future.Format(time.RFC3339)
	if !strings.HasSuffix(z, "Z") {
		t.Fatalf("want Z timestamp, got %s", z)
	}
	body := `{"paused":true,"paused_until":"` + z + `","reason":"rain"}`
	if err := os.WriteFile(store.PausePath(s.Path), []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	loaded, err := store.LoadPause(s.Path, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if !loaded.Active || loaded.Until == nil {
		t.Fatalf("LoadPause %+v", loaded)
	}
	s.Eng.SetPause(loaded.Until, loaded.Reason)
	rr := doJSON(t, s, http.MethodGet, "/api/status", nil)
	st := decodeMap(t, rr)
	if st["paused"] != true {
		t.Fatalf("paused %v", st["paused"])
	}
	pu, _ := st["paused_until"].(string)
	if !strings.HasSuffix(pu, "-07:00") {
		t.Fatalf("paused_until %q want -07:00", pu)
	}
}

func TestPauseAutoExpireLongPause(t *testing.T) {
	s := newTestServer(t)
	past := time.Now().Add(-1 * time.Second)
	s.Eng.SetPause(&past, "old")
	if err := store.SavePause(s.Path, store.PauseState{Active: true, Until: &past, Reason: "old"}); err != nil {
		t.Fatal(err)
	}
	rr := doJSON(t, s, http.MethodGet, "/api/status", nil)
	st := decodeMap(t, rr)
	if st["paused"] != false {
		t.Fatalf("expired should report paused=false, got %v", st)
	}
	loaded, err := store.LoadPause(s.Path, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if loaded.Active {
		t.Fatalf("pause.json still active %+v", loaded)
	}
}

func TestStopUnchangedDuringLongPause(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"days": 7})
	if rr.Code != 200 {
		t.Fatalf("pause %d %s", rr.Code, rr.Body.String())
	}
	before := decodeMap(t, rr)
	pu, _ := before["paused_until"].(string)
	rr = doJSON(t, s, http.MethodPost, "/api/run/cancel", nil)
	if rr.Code != 200 {
		t.Fatalf("cancel %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodGet, "/api/status", nil)
	st := decodeMap(t, rr)
	if st["paused"] != true {
		t.Fatalf("still paused? %v", st["paused"])
	}
	if st["paused_until"] != pu {
		t.Fatalf("paused_until changed %v → %v", pu, st["paused_until"])
	}
	if st["phase"] != "Idle" {
		t.Fatalf("phase %v", st["phase"])
	}
}

func TestPauseTomorrowMorningUsesEnabledSchedules(t *testing.T) {
	loc := phoenixLoc(t)
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodPut, "/api/schedules/early", store.Schedule{
		Enabled: true, Start: "05:30",
		Steps: []store.Step{{StationID: "front-north", Minutes: 1}},
	})
	if rr.Code != 200 {
		t.Fatalf("put schedule %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"until": "tomorrow_morning"})
	if rr.Code != 200 {
		t.Fatalf("pause %d %s", rr.Code, rr.Body.String())
	}
	st := decodeMap(t, rr)
	got, err := time.Parse(time.RFC3339, st["paused_until"].(string))
	if err != nil {
		t.Fatal(err)
	}
	y, m, d := time.Now().In(loc).Date()
	want := time.Date(y, m, d+1, 5, 30, 0, 0, loc)
	if !got.Equal(want) {
		t.Fatalf("paused_until %s want %s (earliest enabled start tomorrow)", got, want)
	}
}

func TestPauseUntilKeywordRejectsUnknown(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"until": "next_tuesday"})
	if rr.Code != 400 {
		t.Fatalf("want 400, got %d %s", rr.Code, rr.Body.String())
	}
}
