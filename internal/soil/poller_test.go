package soil

import (
	"bytes"
	"context"
	"log"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
)

func newTestPoller(t *testing.T, handler http.HandlerFunc) (*Poller, *httptest.Server, *bytes.Buffer) {
	t.Helper()
	srv := httptest.NewServer(handler)
	t.Cleanup(srv.Close)
	loc, err := time.LoadLocation("America/Phoenix")
	if err != nil {
		t.Fatal(err)
	}
	now := time.Date(2026, 9, 27, 12, 0, 0, 0, loc)
	var buf bytes.Buffer
	p := &Poller{
		Source: NewAZMET(Config{Station: "azXX", URL: srv.URL, Timeout: time.Second}),
		Path:   filepath.Join(t.TempDir(), "config.json"),
		Cfg: Config{
			Enabled:    true,
			Station:    "azXX",
			URL:        srv.URL,
			Timeout:    time.Second,
			WindowDays: 14,
			MaxDailyET: 0.6,
			CropFactor: 0.6,
			Capacity:   1.0,
		},
		Now: func() time.Time { return now },
		Log: log.New(&buf, "", 0),
		Loc: loc,
	}
	return p, srv, &buf
}

func TestPollerSuccessAndCacheKeptOn500(t *testing.T) {
	var mu sync.Mutex
	status := http.StatusOK
	var gotPath string
	body, err := os.ReadFile("testdata/azmet_daily.json")
	if err != nil {
		t.Fatal(err)
	}
	p, _, buf := newTestPoller(t, func(w http.ResponseWriter, r *http.Request) {
		mu.Lock()
		gotPath = r.URL.Path
		code := status
		mu.Unlock()
		if code != http.StatusOK {
			http.Error(w, "azXX boom", code)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write(body)
	})
	p.Poll(context.Background())
	mu.Lock()
	path := gotPath
	mu.Unlock()
	if !strings.Contains(path, "/azXX/") || !strings.Contains(path, "P14D") {
		t.Fatalf("path %s", path)
	}
	if st := p.Status(); st.Unavailable || st.LastOK == nil {
		t.Fatalf("first %+v log %s", st, buf.String())
	}
	first := p.DaysForTest()
	if len(first) != 3 {
		t.Fatalf("cache %d", len(first))
	}
	since0 := p.Status().Since

	mu.Lock()
	status = http.StatusInternalServerError
	mu.Unlock()
	p.Poll(context.Background())
	st := p.Status()
	if !st.Unavailable || st.Since == nil {
		t.Fatalf("500 status %+v", st)
	}
	if since0 != nil {
		t.Fatal("since should have been clear after success")
	}
	kept := p.DaysForTest()
	if len(kept) != 3 || kept[0].ETInches != first[0].ETInches {
		t.Fatalf("cache not kept %+v", kept)
	}
	if strings.Contains(buf.String(), "azXX") || strings.Contains(st.LastError, "azXX") {
		t.Fatalf("leak log %q err %q", buf.String(), st.LastError)
	}

	// same failure: log once
	p.Poll(context.Background())
	if strings.Count(buf.String(), "soil: fetch failed:") != 1 {
		t.Fatalf("logs:\n%s", buf.String())
	}
}

func TestPollerImplausibleKeepsCache(t *testing.T) {
	var mu sync.Mutex
	implausible := false
	good, err := os.ReadFile("testdata/azmet_daily.json")
	if err != nil {
		t.Fatal(err)
	}
	p, _, buf := newTestPoller(t, func(w http.ResponseWriter, r *http.Request) {
		mu.Lock()
		bad := implausible
		mu.Unlock()
		w.Header().Set("Content-Type", "application/json")
		if bad {
			_, _ = w.Write([]byte(`{"data":[{"datetime":"2026-09-26","eto_azmet":"40.0","meta_needs_review":"0","meta_station_id":"azXX","meta_station_name":"Test Station"}]}`))
			return
		}
		_, _ = w.Write(good)
	})
	p.Poll(context.Background())
	if p.Status().Unavailable {
		t.Fatalf("good %+v", p.Status())
	}
	mu.Lock()
	implausible = true
	mu.Unlock()
	p.Poll(context.Background())
	st := p.Status()
	if !st.Unavailable || st.Since == nil || !strings.Contains(st.LastError, "implausible") {
		t.Fatalf("implausible %+v", st)
	}
	if len(p.DaysForTest()) != 3 {
		t.Fatalf("cache dropped")
	}
	if strings.Contains(buf.String(), "azXX") || strings.Contains(buf.String(), "Test Station") {
		t.Fatalf("leak %s", buf.String())
	}
}

func TestPollerSinceClearedOnRecovery(t *testing.T) {
	fake := &Fake{}
	fake.Set(nil, errString("HTTP 500"))
	loc, _ := time.LoadLocation("America/Phoenix")
	now := time.Date(2026, 9, 27, 12, 0, 0, 0, loc)
	var buf bytes.Buffer
	p := &Poller{
		Source: fake,
		Path:   filepath.Join(t.TempDir(), "cfg.json"),
		Cfg:    Config{Enabled: true, WindowDays: 14, MaxDailyET: 0.6, CropFactor: 0.6, Capacity: 1},
		Now:    func() time.Time { return now },
		Log:    log.New(&buf, "", 0),
		Loc:    loc,
	}
	p.Poll(context.Background())
	st := p.Status()
	if !st.Unavailable || st.Since == nil {
		t.Fatalf("%+v", st)
	}
	fake.Set([]DayET{{Date: "2026-09-26", ETInches: 0.2}}, nil)
	p.Poll(context.Background())
	st = p.Status()
	if st.Unavailable || st.Since != nil || st.LastOK == nil {
		t.Fatalf("recovered %+v", st)
	}
	if strings.Count(buf.String(), "soil: data available again") != 1 {
		t.Fatalf("logs %s", buf.String())
	}
}

func TestPollerNeedsReviewLoggedOnce(t *testing.T) {
	body, err := os.ReadFile("testdata/azmet_daily.json")
	if err != nil {
		t.Fatal(err)
	}
	p, _, buf := newTestPoller(t, func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write(body)
	})
	p.Poll(context.Background())
	p.Poll(context.Background())
	if strings.Count(buf.String(), "needs review") != 1 {
		t.Fatalf("logs %s", buf.String())
	}
}

func TestPollerRateLimitedLog(t *testing.T) {
	p, _, buf := newTestPoller(t, func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusTooManyRequests)
	})
	p.Poll(context.Background())
	p.Poll(context.Background())
	if strings.Count(buf.String(), "soil: rate-limited") != 1 {
		t.Fatalf("logs %s", buf.String())
	}
	if !p.Status().Unavailable || p.Status().Since == nil {
		t.Fatalf("%+v", p.Status())
	}
}

func TestPollerTimeoutDoesNotHoldStatus(t *testing.T) {
	started := make(chan struct{})
	release := make(chan struct{})
	p, _, _ := newTestPoller(t, func(w http.ResponseWriter, r *http.Request) {
		close(started)
		select {
		case <-release:
		case <-r.Context().Done():
		}
	})
	p.Source = &AZMET{
		BaseURL:   p.Source.(*AZMET).BaseURL,
		StationID: "azXX",
		HTTP:      &http.Client{Timeout: 80 * time.Millisecond},
	}
	done := make(chan struct{})
	go func() {
		p.Poll(context.Background())
		close(done)
	}()
	select {
	case <-started:
	case <-time.After(2 * time.Second):
		t.Fatal("handler not entered")
	}
	start := time.Now()
	_ = p.Status()
	if d := time.Since(start); d > 200*time.Millisecond {
		t.Fatalf("Status took %s", d)
	}
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("poll did not finish")
	}
}

func TestPollerCancelledCtxDoesNotMarkUnavailable(t *testing.T) {
	p, _, _ := newTestPoller(t, func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "nope", 500)
	})
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	p.Poll(ctx)
	if p.Status().Unavailable {
		t.Fatalf("%+v", p.Status())
	}
}

func TestViewUsesCacheWithoutFetch(t *testing.T) {
	loc, _ := time.LoadLocation("America/Phoenix")
	now := time.Date(2026, 9, 27, 12, 0, 0, 0, loc)
	p := &Poller{
		Cfg:    Config{Enabled: true, WindowDays: 1, CropFactor: 0.6, Capacity: 1, MaxDailyET: 0.6},
		Now:    func() time.Time { return now },
		Loc:    loc,
		Source: &Fake{}, // must not be called
	}
	p.SeedCacheForTest([]DayET{{Date: "2026-09-26", ETInches: 0.2}, {Date: "2026-09-27", ETInches: 0.2}}, now)
	stations := []engine.StationConfig{{ID: "front-north", Title: "Front North"}}
	v := p.View(stations, nil, nil, false, now, loc)
	if !v.Enabled || v.ET.Unavailable || len(v.Zones) != 1 {
		t.Fatalf("%+v", v)
	}
	if v.Zones[0].StationID != "front-north" || v.Zones[0].Inputs.ETKnown == false {
		t.Fatalf("zone %+v", v.Zones[0])
	}
}

type errString string

func (e errString) Error() string { return string(e) }
