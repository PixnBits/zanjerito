package api

import (
	"bytes"
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
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
