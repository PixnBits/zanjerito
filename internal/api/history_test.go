package api

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/history"
)

func TestHistoryAPI(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/api/history", nil)
	if rr.Code != 200 {
		t.Fatalf("nil history %d %s", rr.Code, rr.Body.String())
	}
	if cc := rr.Header().Get("Cache-Control"); cc != "no-store" {
		t.Fatalf("cache %q", cc)
	}
	var empty struct {
		Entries []json.RawMessage `json:"entries"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &empty); err != nil {
		t.Fatal(err)
	}
	if empty.Entries == nil || len(empty.Entries) != 0 {
		t.Fatalf("want empty array, got %s", rr.Body.String())
	}

	loc, err := time.LoadLocation("America/Phoenix")
	if err != nil {
		t.Fatal(err)
	}
	fixed := time.Date(2026, 9, 22, 8, 0, 0, 0, loc)
	hl, err := history.Open(filepath.Join(t.TempDir(), "history.json"), func() time.Time { return fixed })
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(hl.Close)
	const n = 55
	for i := 0; i < n; i++ {
		at := fixed.Add(time.Duration(i) * time.Minute)
		rec := engine.RunRecord{
			ProgramID: "prog",
			Program:   fmt.Sprintf("p%02d", i),
			Kind:      engine.KindSchedule,
			Start:     at,
			End:       at.Add(time.Minute),
			Outcome:   engine.OutcomeCompleted,
		}
		if i == n-1 {
			rec.Stations = []engine.StationRun{{StationID: "front-north", PlannedSec: 480, ActualSec: 480}}
		}
		hl.Append(rec)
	}
	s.History = hl

	rr = doJSON(t, s, http.MethodGet, "/api/history", nil)
	if rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr.Header().Get("Cache-Control") != "no-store" {
		t.Fatalf("cache %q", rr.Header().Get("Cache-Control"))
	}
	body := decodeHistory(t, rr.Body.Bytes())
	if len(body.Entries) != 50 {
		t.Fatalf("default limit got %d", len(body.Entries))
	}
	if body.Entries[0].Program != "p54" || body.Entries[1].Program != "p53" {
		t.Fatalf("order %s %s", body.Entries[0].Program, body.Entries[1].Program)
	}
	newestAt := fixed.Add(54 * time.Minute)
	if body.Entries[0].StartedAt != newestAt.Format(time.RFC3339) {
		t.Fatalf("started %s", body.Entries[0].StartedAt)
	}
	if body.Entries[0].EndedAt != newestAt.Add(time.Minute).Format(time.RFC3339) {
		t.Fatalf("ended %s", body.Entries[0].EndedAt)
	}
	if !strings.HasSuffix(body.Entries[0].StartedAt, "-07:00") {
		t.Fatalf("want RFC3339 offset, got %s", body.Entries[0].StartedAt)
	}
	if len(body.Entries[0].Stations) != 1 {
		t.Fatalf("stations %+v", body.Entries[0].Stations)
	}
	st := body.Entries[0].Stations[0]
	if st.StationID != "front-north" || st.PlannedSec != 480 || st.ActualSec != 480 || st.PlannedMin != 8 || st.ActualMin != 8 {
		t.Fatalf("station %+v", st)
	}
	if body.Entries[1].Stations == nil {
		t.Fatal("empty stations encoded null")
	}

	rr = doJSON(t, s, http.MethodGet, "/api/history?limit=2", nil)
	body = decodeHistory(t, rr.Body.Bytes())
	if rr.Code != 200 || len(body.Entries) != 2 || body.Entries[0].Program != "p54" {
		t.Fatalf("limit=2 %d %+v", rr.Code, programsOf(body))
	}

	rr = doJSON(t, s, http.MethodGet, "/api/history?limit=500", nil)
	body = decodeHistory(t, rr.Body.Bytes())
	if rr.Code != 200 || len(body.Entries) > 200 || len(body.Entries) != n {
		t.Fatalf("limit=500 len %d code %d", len(body.Entries), rr.Code)
	}

	for _, q := range []string{"abc", "0"} {
		rr = doJSON(t, s, http.MethodGet, "/api/history?limit="+q, nil)
		if rr.Code != 400 {
			t.Fatalf("limit=%s code %d %s", q, rr.Code, rr.Body.String())
		}
		if rr.Header().Get("Cache-Control") != "no-store" {
			t.Fatalf("limit=%s cache %q", q, rr.Header().Get("Cache-Control"))
		}
		var errBody map[string]string
		if err := json.Unmarshal(rr.Body.Bytes(), &errBody); err != nil || errBody["error"] == "" {
			t.Fatalf("limit=%s body %s", q, rr.Body.String())
		}
	}
}

type historyBody struct {
	Entries []struct {
		Program   string `json:"program"`
		StartedAt string `json:"started_at"`
		EndedAt   string `json:"ended_at"`
		Stations  []struct {
			StationID  string `json:"station_id"`
			PlannedSec int    `json:"planned_sec"`
			ActualSec  int    `json:"actual_sec"`
			PlannedMin int    `json:"planned_min"`
			ActualMin  int    `json:"actual_min"`
		} `json:"stations"`
	} `json:"entries"`
}

func decodeHistory(t *testing.T, b []byte) historyBody {
	t.Helper()
	var body historyBody
	if err := json.Unmarshal(b, &body); err != nil {
		t.Fatal(err)
	}
	return body
}

func TestHistoryNilLogRejectsBadLimit(t *testing.T) {
	s := newTestServer(t)
	if s.History != nil {
		t.Fatal("expected nil history")
	}
	for _, q := range []string{"abc", "0", "-1", "1.5"} {
		rr := doJSON(t, s, http.MethodGet, "/api/history?limit="+q, nil)
		if rr.Code != 400 {
			t.Fatalf("limit=%s code %d %s", q, rr.Code, rr.Body.String())
		}
		if rr.Header().Get("Cache-Control") != "no-store" {
			t.Fatalf("limit=%s cache %q", q, rr.Header().Get("Cache-Control"))
		}
	}
	rr := doJSON(t, s, http.MethodGet, "/api/history?limit=3", nil)
	if rr.Code != 200 {
		t.Fatalf("valid limit on nil history %d %s", rr.Code, rr.Body.String())
	}
	if rr.Header().Get("Cache-Control") != "no-store" {
		t.Fatalf("cache %q", rr.Header().Get("Cache-Control"))
	}
}

func TestHistoryGetDuringSlowWrite(t *testing.T) {
	s := newTestServer(t)
	hl, err := history.Open(filepath.Join(t.TempDir(), "history.json"), time.Now)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(hl.Close)
	entered := make(chan struct{})
	var once sync.Once
	hl.SetWriteFuncForTest(func(string, any) error {
		once.Do(func() { close(entered) })
		time.Sleep(3 * time.Second)
		return nil
	})
	hl.Start(context.Background())
	s.History = hl
	hl.Record(engine.RunRecord{
		ProgramID: "dawn",
		Program:   "Dawn",
		Kind:      engine.KindSchedule,
		Outcome:   engine.OutcomeCompleted,
		Start:     time.Now(),
		End:       time.Now(),
	})
	select {
	case <-entered:
	case <-time.After(2 * time.Second):
		t.Fatal("slow write did not start")
	}
	req := httptest.NewRequest(http.MethodGet, "/api/history", nil)
	rr := httptest.NewRecorder()
	done := make(chan struct{})
	go func() {
		s.ServeHTTP(rr, req)
		close(done)
	}()
	start := time.Now()
	select {
	case <-done:
	case <-time.After(200 * time.Millisecond):
		t.Fatal("GET /api/history blocked while a history write was in flight")
	}
	if elapsed := time.Since(start); elapsed >= 200*time.Millisecond {
		t.Fatalf("GET /api/history took %s", elapsed)
	}
	if rr.Code != 200 {
		t.Fatalf("code %d %s", rr.Code, rr.Body.String())
	}
	if rr.Header().Get("Cache-Control") != "no-store" {
		t.Fatalf("cache %q", rr.Header().Get("Cache-Control"))
	}
	body := decodeHistory(t, rr.Body.Bytes())
	if len(body.Entries) != 1 || body.Entries[0].Program != "Dawn" {
		t.Fatalf("%+v", programsOf(body))
	}
}

func programsOf(body historyBody) []string {
	out := make([]string, len(body.Entries))
	for i, e := range body.Entries {
		out[i] = e.Program
	}
	return out
}
