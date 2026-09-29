package soil

import (
	"bytes"
	"context"
	"encoding/json"
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

func viewStations() []engine.StationConfig {
	return []engine.StationConfig{
		{ID: "front-north", Title: "Front North"},
		{ID: "front-south", Title: "Front South"},
	}
}

func assertETUnknown(t *testing.T, v View, waiting bool) {
	t.Helper()
	if v.ETKnown {
		t.Fatalf("et_known true reason %q", v.ETReason)
	}
	if v.UpdatedAt != nil {
		t.Fatalf("updated_at %q", *v.UpdatedAt)
	}
	if waiting {
		if v.ETReason != "waiting for first ET fetch" {
			t.Fatalf("reason %q", v.ETReason)
		}
	} else if !strings.HasPrefix(v.ETReason, "no ET data yet") {
		t.Fatalf("reason %q", v.ETReason)
	}
	if strings.Contains(v.ETReason, "azXX") || strings.Contains(v.ETReason, "Test Station") || strings.Contains(v.ETReason, "http") {
		t.Fatalf("reason leaked %q", v.ETReason)
	}
	if len(v.Zones) == 0 {
		t.Fatal("expected zones")
	}
	for _, z := range v.Zones {
		if z.Percent != nil || z.BalanceInches != nil {
			t.Fatalf("meaningful balance %+v", z)
		}
	}
	b, err := json.Marshal(v)
	if err != nil {
		t.Fatal(err)
	}
	body := string(b)
	for _, need := range []string{`"et_known":false`, `"percent":null`, `"balance_inches":null`, `"updated_at":null`} {
		if !strings.Contains(body, need) {
			t.Fatalf("json missing %s: %s", need, body)
		}
	}
}

type gateSource struct {
	started chan struct{}
	release chan struct{}
	once    sync.Once
	days    []DayET
}

func (g *gateSource) Fetch(ctx context.Context, _ time.Time, _ int) ([]DayET, error) {
	g.once.Do(func() { close(g.started) })
	select {
	case <-g.release:
	case <-ctx.Done():
		return nil, ctx.Err()
	}
	out := make([]DayET, len(g.days))
	copy(out, g.days)
	return out, nil
}

func TestViewNoETWhileFirstFetchInFlight(t *testing.T) {
	loc, err := time.LoadLocation("America/Phoenix")
	if err != nil {
		t.Fatal(err)
	}
	fetchAt := time.Date(2026, 9, 27, 8, 0, 0, 0, loc)
	viewAt := fetchAt.Add(5 * time.Hour)
	clock := fetchAt
	src := &gateSource{
		started: make(chan struct{}),
		release: make(chan struct{}),
		days:    []DayET{{Date: "2026-09-26", ETInches: 0.2}},
	}
	p := &Poller{
		Source: src,
		Cfg:    Config{Enabled: true, Station: "azXX", WindowDays: 14, CropFactor: 0.6, Capacity: 1, MaxDailyET: 0.6},
		Now:    func() time.Time { return clock },
		Loc:    loc,
		Log:    log.New(&bytes.Buffer{}, "", 0),
	}
	done := make(chan struct{})
	go func() {
		p.Poll(context.Background())
		close(done)
	}()
	select {
	case <-src.started:
	case <-time.After(2 * time.Second):
		t.Fatal("fetch did not start")
	}
	rain := map[string]float64{"2026-09-26": 0.15, "2026-09-27": 0.25}
	v := p.View(viewStations(), nil, rain, true, viewAt, loc)
	assertETUnknown(t, v, true)
	if v.WindowDays != 14 {
		t.Fatalf("window %d", v.WindowDays)
	}
	if v.Zones[0].RainTotalInches != 0.40 || v.Zones[0].Inputs.RainInches != 0.25 || !v.Zones[0].Inputs.RainKnown {
		t.Fatalf("rain still available: total %v inputs %+v", v.Zones[0].RainTotalInches, v.Zones[0].Inputs)
	}
	close(src.release)
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("poll did not finish")
	}
	clock = viewAt
	v = p.View(viewStations(), nil, rain, true, viewAt, loc)
	if !v.ETKnown || v.ETReason != "" {
		t.Fatalf("known %v reason %q", v.ETKnown, v.ETReason)
	}
	if v.UpdatedAt == nil || v.ET.LastOKAt == nil || *v.UpdatedAt != *v.ET.LastOKAt {
		t.Fatalf("updated %v last_ok %v", v.UpdatedAt, v.ET.LastOKAt)
	}
	got, err := time.Parse(time.RFC3339, *v.UpdatedAt)
	if err != nil {
		t.Fatal(err)
	}
	st := p.Status()
	if st.LastOK == nil || !got.Equal(*st.LastOK) {
		t.Fatalf("updated %s lastOK %v", got, st.LastOK)
	}
	if got.Equal(viewAt) {
		t.Fatal("updated_at used now, not LastOK")
	}
	for _, z := range v.Zones {
		if z.Percent == nil || z.BalanceInches == nil {
			t.Fatalf("percent missing %+v", z)
		}
	}
	if v.Zones[0].ETTotalInches <= 0 {
		t.Fatalf("et total %v", v.Zones[0].ETTotalInches)
	}
}

func TestViewNoETAfterFailedFetch(t *testing.T) {
	cases := []struct {
		name  string
		code  int
		body  string
		class string
	}{
		{name: "http500", code: http.StatusInternalServerError, body: "azXX boom", class: "AZMET unavailable"},
		{name: "http429", code: http.StatusTooManyRequests, body: "azXX slow", class: "rate-limited"},
		{name: "badjson", code: http.StatusOK, body: `{`, class: "AZMET unavailable"},
		{name: "empty", code: http.StatusOK, body: `{"data":[],"meta_station_name":"Test Station"}`, class: "no observations"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			p, _, buf := newTestPoller(t, func(w http.ResponseWriter, r *http.Request) {
				if tc.code != http.StatusOK {
					http.Error(w, tc.body, tc.code)
					return
				}
				w.Header().Set("Content-Type", "application/json")
				_, _ = w.Write([]byte(tc.body))
			})
			p.Poll(context.Background())
			viewAt := time.Date(2026, 9, 27, 18, 0, 0, 0, p.Loc)
			v := p.View(viewStations(), nil, map[string]float64{"2026-09-27": 0.40}, true, viewAt, p.Loc)
			assertETUnknown(t, v, false)
			if !strings.Contains(v.ETReason, tc.class) {
				t.Fatalf("reason %q log %s", v.ETReason, buf.String())
			}
			if !v.ET.Unavailable || v.ET.LastOKAt != nil {
				t.Fatalf("et %+v", v.ET)
			}
			raw, err := json.Marshal(v)
			if err != nil {
				t.Fatal(err)
			}
			if strings.Contains(string(raw), "azXX") || strings.Contains(string(raw), "Test Station") || strings.Contains(buf.String(), "azXX") || strings.Contains(buf.String(), "Test Station") {
				t.Fatalf("leak body %s log %s", raw, buf.String())
			}
		})
	}
}

func TestViewUpdatedAtStaysLastOKAfterFailedRetry(t *testing.T) {
	var mu sync.Mutex
	status := http.StatusOK
	p, _, _ := newTestPoller(t, func(w http.ResponseWriter, r *http.Request) {
		mu.Lock()
		code := status
		mu.Unlock()
		if code != http.StatusOK {
			http.Error(w, "azXX", code)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"data":[{"datetime":"2026-09-26","eto_azmet":"5.08"}]}`))
	})
	p.Poll(context.Background())
	first := p.Status().LastOK
	if first == nil {
		t.Fatal("no LastOK")
	}
	mu.Lock()
	status = http.StatusInternalServerError
	mu.Unlock()
	later := time.Date(2026, 9, 27, 18, 0, 0, 0, p.Loc)
	p.Now = func() time.Time { return later }
	p.Poll(context.Background())
	v := p.View([]engine.StationConfig{{ID: "front-north", Title: "Front North"}}, nil, nil, false, later, p.Loc)
	if !v.ETKnown || v.UpdatedAt == nil || v.Zones[0].Percent == nil {
		t.Fatalf("%+v", v)
	}
	got, err := time.Parse(time.RFC3339, *v.UpdatedAt)
	if err != nil {
		t.Fatal(err)
	}
	if !got.Equal(*first) || got.Equal(later) {
		t.Fatalf("updated %s lastOK %s later %s", got, first, later)
	}
	if v.ET.Since == nil {
		t.Fatal("missing since")
	}
	since, err := time.Parse(time.RFC3339, *v.ET.Since)
	if err != nil {
		t.Fatal(err)
	}
	if got.Equal(since) {
		t.Fatal("updated_at is the failed attempt")
	}
}

func TestViewETOutsideWindowIsUnknown(t *testing.T) {
	loc, err := time.LoadLocation("America/Phoenix")
	if err != nil {
		t.Fatal(err)
	}
	fetchAt := time.Date(2026, 9, 27, 8, 0, 0, 0, loc)
	viewAt := fetchAt.Add(2 * time.Hour)
	p := &Poller{
		Cfg: Config{Enabled: true, WindowDays: 14, CropFactor: 0.6, Capacity: 1, MaxDailyET: 0.6},
		Now: func() time.Time { return viewAt },
		Loc: loc,
	}
	p.SeedCacheForTest([]DayET{{Date: "2026-01-01", ETInches: 0.2}}, fetchAt)
	v := p.View([]engine.StationConfig{{ID: "front-north", Title: "Front North"}}, nil, nil, false, viewAt, loc)
	if v.ETKnown || v.UpdatedAt == nil || v.Zones[0].Percent != nil {
		t.Fatalf("known %v updated %v pct %v reason %q", v.ETKnown, v.UpdatedAt, v.Zones[0].Percent, v.ETReason)
	}
	if v.ETReason != "no ET data yet" {
		t.Fatalf("reason %q", v.ETReason)
	}
	got, err := time.Parse(time.RFC3339, *v.UpdatedAt)
	if err != nil {
		t.Fatal(err)
	}
	if !got.Equal(fetchAt) || got.Equal(viewAt) {
		t.Fatalf("updated %s", got)
	}
}

func TestViewETStaleBoundary(t *testing.T) {
	loc, err := time.LoadLocation("America/Phoenix")
	if err != nil {
		t.Fatal(err)
	}
	now := time.Date(2026, 9, 27, 12, 0, 0, 0, loc)
	p := &Poller{
		Cfg: Config{Enabled: true, WindowDays: 14, CropFactor: 0.6, Capacity: 1, MaxDailyET: 0.6},
		Loc: loc,
	}
	v := p.View(viewStations(), nil, nil, false, now, loc)
	if v.ETStale || v.ETKnown {
		t.Fatalf("never fetched known %v stale %v reason %q", v.ETKnown, v.ETStale, v.ETReason)
	}

	days := []DayET{{Date: "2026-09-26", ETInches: 0.2}}
	cases := []struct {
		name  string
		age   time.Duration
		stale bool
	}{
		{name: "47h59m", age: 47*time.Hour + 59*time.Minute, stale: false},
		{name: "48h01m", age: 48*time.Hour + time.Minute, stale: true},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			p.SeedCacheForTest(days, now.Add(-tc.age))
			v := p.View(viewStations(), nil, nil, false, now, loc)
			if v.ETStale != tc.stale || !v.ETKnown {
				t.Fatalf("known %v stale %v want stale %v", v.ETKnown, v.ETStale, tc.stale)
			}
		})
	}
}

func TestViewETStaleWhenCachedDaysInWindowAndFetchFails(t *testing.T) {
	loc, err := time.LoadLocation("America/Phoenix")
	if err != nil {
		t.Fatal(err)
	}
	now := time.Date(2026, 9, 27, 12, 0, 0, 0, loc)
	fetched := now.Add(-72 * time.Hour)
	var buf bytes.Buffer
	p := &Poller{
		Source: &Fake{},
		Cfg:    Config{Enabled: true, Station: "azXX", WindowDays: 14, CropFactor: 0.6, Capacity: 1, MaxDailyET: 0.6},
		Now:    func() time.Time { return now },
		Loc:    loc,
		Log:    log.New(&buf, "", 0),
	}
	p.SeedCacheForTest([]DayET{
		{Date: "2026-09-24", ETInches: 0.20},
		{Date: "2026-09-25", ETInches: 0.20},
		{Date: "2026-09-26", ETInches: 0.22},
	}, fetched)
	p.Source.(*Fake).Set(nil, errString("HTTP 500"))
	p.Poll(context.Background())
	v := p.View([]engine.StationConfig{{ID: "front-north", Title: "Front North"}}, nil, nil, false, now, loc)
	if !v.ETKnown || !v.ETStale {
		t.Fatalf("known %v stale %v reason %q", v.ETKnown, v.ETStale, v.ETReason)
	}
	if v.UpdatedAt == nil || v.Zones[0].Percent == nil {
		t.Fatalf("updated %v pct %v", v.UpdatedAt, v.Zones[0].Percent)
	}
	got, err := time.Parse(time.RFC3339, *v.UpdatedAt)
	if err != nil {
		t.Fatal(err)
	}
	if !got.Equal(fetched) || got.Equal(now) {
		t.Fatalf("updated %s fetched %s now %s", got, fetched, now)
	}
	if !v.ET.Unavailable {
		t.Fatal("expected unavailable after failed fetch")
	}
	if strings.Contains(buf.String(), "azXX") || strings.Contains(buf.String(), "Test Station") {
		t.Fatalf("leak %s", buf.String())
	}
}
