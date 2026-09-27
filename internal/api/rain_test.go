package api

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"sync"
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

func TestStatusRoundsRainInches(t *testing.T) {
	s := newTestServer(t)
	now := time.Now().UTC().Truncate(time.Second)
	fake := &rain.Fake{}
	fake.Set([]rain.Sample{
		{Time: now.Add(-2 * time.Hour), Inches: 0.1},
		{Time: now.Add(-time.Hour), Inches: 0.2},
		{Time: now, Inches: 0},
	}, nil)
	p := &rain.Poller{
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
	s.Rain = p
	p.Poll(context.Background())

	rr := doJSON(t, s, http.MethodGet, "/api/status", nil)
	body := rr.Body.String()
	if strings.Contains(body, "0.30000000000000004") {
		t.Fatalf("dust in status %s", body)
	}
	if !strings.Contains(body, `"rain_inches":0.3`) || !strings.Contains(body, `"last_total_inches":0.3`) {
		t.Fatalf("status %s", body)
	}
	raw, err := os.ReadFile(store.PausePath(s.Path))
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(string(raw), "0.30000000000000004") {
		t.Fatalf("dust in pause.json %s", raw)
	}
	var disk store.PauseState
	if err := json.Unmarshal(raw, &disk); err != nil {
		t.Fatal(err)
	}
	if disk.RainInches != 0.3 {
		t.Fatalf("pause.json rain_inches %v raw %s", disk.RainInches, raw)
	}

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
	buf := make([]byte, 4096)
	n, err := resp.Body.Read(buf)
	if n == 0 && err != nil {
		t.Fatal(err)
	}
	cancel()
	sse := string(buf[:n])
	if strings.Contains(sse, "0.30000000000000004") || !strings.Contains(sse, `"rain_inches":0.3`) || !strings.Contains(sse, `"last_total_inches":0.3`) {
		t.Fatalf("sse %s", sse)
	}
}

func TestStatusWhileRainSaveBlocked(t *testing.T) {
	s, p, release := newBlockingRain(t)
	now := time.Now().UTC().Truncate(time.Second)
	p.Now = func() time.Time { return now }
	fake := &rain.Fake{}
	fake.Set([]rain.Sample{{Time: now.Add(-time.Hour), Inches: 0.5}, {Time: now, Inches: 0}}, nil)
	p.Source = fake
	p.Cfg = rain.Config{
		Enabled: true, GaugeID: "TEST-GAUGE",
		TriggerInches: 0.25, WindowHours: 24,
		DryDays: 2, HeavyInches: 1, HeavyDryDays: 4, StaleHours: 7,
	}

	pollDone := make(chan struct{})
	go func() {
		p.Poll(context.Background())
		close(pollDone)
	}()
	waitSave(t, release)
	assertStatusFast(t, s, p)
	rr := doJSON(t, s, http.MethodGet, "/api/status", nil)
	if rr.Code != 200 {
		t.Fatalf("status %d %s", rr.Code, rr.Body.String())
	}
	st := decodeMap(t, rr)
	if st["paused"] != true || st["pause_source"] != "auto" {
		t.Fatalf("engine pause not visible yet %v", st)
	}
	closeRelease(release)
	select {
	case <-pollDone:
	case <-time.After(2 * time.Second):
		t.Fatal("poll did not finish")
	}
	disk, err := store.LoadPause(s.Path, now)
	if err != nil || !disk.Active || disk.Source != "auto" {
		t.Fatalf("disk %+v %v", disk, err)
	}
}

func TestStatusWhileResumeSaveBlocked(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"duration_sec": 3600, "reason": "mow"})
	if rr.Code != 200 {
		t.Fatalf("pause %d %s", rr.Code, rr.Body.String())
	}
	_, gate, _ := newBlockingRainOn(t, s)
	done := make(chan *httptest.ResponseRecorder, 1)
	go func() {
		req := httptest.NewRequest(http.MethodDelete, "/api/pause", nil)
		rec := httptest.NewRecorder()
		s.ServeHTTP(rec, req)
		done <- rec
	}()
	waitSave(t, gate)
	assertStatusFast(t, s, s.Rain)
	rr = doJSON(t, s, http.MethodGet, "/api/status", nil)
	if rr.Code != 200 {
		t.Fatalf("status %d", rr.Code)
	}
	// The hold is already cleared in memory; the save is still blocked.
	if decodeMap(t, rr)["paused"] != false {
		t.Fatalf("status %s", rr.Body.String())
	}
	closeRelease(gate)
	rec := waitRecorder(t, done)
	if rec.Code != http.StatusOK {
		t.Fatalf("resume %d %s", rec.Code, rec.Body.String())
	}
	disk, err := store.LoadPause(s.Path, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if disk.Active || disk.RainClearedAt == nil {
		t.Fatalf("pause.json %+v", disk)
	}
}

func TestInFlightPollDoesNotOverwriteResume(t *testing.T) {
	s, p, gate := newBlockingRain(t)
	now := time.Now().UTC().Truncate(time.Second)
	p.Now = func() time.Time { return now }
	fake := &rain.Fake{}
	fake.Set([]rain.Sample{{Time: now.Add(-time.Hour), Inches: 0.5}, {Time: now, Inches: 0}}, nil)
	p.Source = fake
	p.Cfg = rain.Config{
		Enabled: true, GaugeID: "TEST-GAUGE",
		TriggerInches: 0.25, WindowHours: 24,
		DryDays: 2, HeavyInches: 1, HeavyDryDays: 4, StaleHours: 7,
	}
	pollDone := make(chan struct{})
	go func() {
		p.Poll(context.Background())
		close(pollDone)
	}()
	waitSave(t, gate)
	assertStatusFast(t, s, p)
	resumeDone := make(chan *httptest.ResponseRecorder, 1)
	go func() {
		req := httptest.NewRequest(http.MethodDelete, "/api/pause", nil)
		rec := httptest.NewRecorder()
		s.ServeHTTP(rec, req)
		resumeDone <- rec
	}()
	// Resume is serialized behind the poll snapshot. Status must not wait
	// on either fsync. Unblock, then the later clear is what remains on disk.
	assertStatusFast(t, s, p)
	closeRelease(gate)
	select {
	case <-pollDone:
	case <-time.After(2 * time.Second):
		t.Fatal("poll did not finish")
	}
	rec := waitRecorder(t, resumeDone)
	if rec.Code != 200 {
		t.Fatalf("resume %d %s", rec.Code, rec.Body.String())
	}
	disk, err := store.LoadPause(s.Path, now)
	if err != nil {
		t.Fatal(err)
	}
	if disk.Active || disk.RainClearedAt == nil {
		t.Fatalf("older pause overwrote resume %+v", disk)
	}
}

func TestManualSaveFailureIs500(t *testing.T) {
	s := newTestServer(t)
	p := &rain.Poller{Eng: s.Eng, Path: s.Path}
	p.SetSaveFuncForTest(func(string, store.PauseState) error {
		return errRain("disk full")
	})
	s.Rain = p
	rr := doJSON(t, s, http.MethodPost, "/api/pause", map[string]any{"duration_sec": 60})
	if rr.Code != http.StatusInternalServerError || !strings.Contains(rr.Body.String(), "disk full") {
		t.Fatalf("pause %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodDelete, "/api/pause", nil)
	if rr.Code != http.StatusInternalServerError || !strings.Contains(rr.Body.String(), "disk full") {
		t.Fatalf("resume %d %s", rr.Code, rr.Body.String())
	}
}

// blockingSave is a pause writer that waits until release is closed.
type blockingSave struct {
	release chan struct{}
	entered chan struct{}
	once    sync.Once
	closed  sync.Once
}

func (b *blockingSave) save(path string, ps store.PauseState) error {
	b.once.Do(func() { close(b.entered) })
	<-b.release
	return store.SavePause(path, ps)
}

func (b *blockingSave) close() {
	b.closed.Do(func() { close(b.release) })
}

func newBlockingRain(t *testing.T) (*Server, *rain.Poller, *blockingSave) {
	t.Helper()
	s := newTestServer(t)
	p, gate, _ := newBlockingRainOn(t, s)
	return s, p, gate
}

func newBlockingRainOn(t *testing.T, s *Server) (*rain.Poller, *blockingSave, chan struct{}) {
	t.Helper()
	gate := &blockingSave{release: make(chan struct{}), entered: make(chan struct{})}
	t.Cleanup(gate.close)
	p := &rain.Poller{Eng: s.Eng, Path: s.Path, Cfg: rain.Config{Enabled: true, GaugeID: "TEST-GAUGE"}}
	p.SetSaveFuncForTest(gate.save)
	s.Rain = p
	return p, gate, gate.entered
}

func waitSave(t *testing.T, gate *blockingSave) {
	t.Helper()
	waitEntered(t, gate.entered)
}

func waitEntered(t *testing.T, entered chan struct{}) {
	t.Helper()
	select {
	case <-entered:
	case <-time.After(2 * time.Second):
		t.Fatal("save did not start")
	}
}

func closeRelease(gate *blockingSave) {
	gate.close()
}

func assertStatusFast(t *testing.T, s *Server, p *rain.Poller) {
	t.Helper()
	start := time.Now()
	_ = p.Status()
	if d := time.Since(start); d > 200*time.Millisecond {
		t.Fatalf("Poller.Status took %s", d)
	}
	start = time.Now()
	rr := doJSON(t, s, http.MethodGet, "/api/status", nil)
	if d := time.Since(start); d > 200*time.Millisecond {
		t.Fatalf("GET /api/status took %s body %s", d, rr.Body.String())
	}
	if rr.Code != 200 {
		t.Fatalf("status %d %s", rr.Code, rr.Body.String())
	}
}

func waitRecorder(t *testing.T, done chan *httptest.ResponseRecorder) *httptest.ResponseRecorder {
	t.Helper()
	select {
	case rec := <-done:
		return rec
	case <-time.After(2 * time.Second):
		t.Fatal("handler did not finish")
	}
	return nil
}
