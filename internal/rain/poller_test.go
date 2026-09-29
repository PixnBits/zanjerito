package rain

import (
	"bytes"
	"context"
	"fmt"
	"log"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/gpio"
	"github.com/PixnBits/zanjerito/internal/store"
)

type testClock struct {
	mu sync.Mutex
	t  time.Time
}

func (c *testClock) Now() time.Time {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.t
}

func (c *testClock) Set(t time.Time) {
	c.mu.Lock()
	c.t = t
	c.mu.Unlock()
}

func newTestPoller(t *testing.T) (*Poller, *Fake, *testClock, *bytes.Buffer) {
	t.Helper()
	al := true
	cfg := engine.Config{
		Chip: "gpiochip0", ActiveLow: &al, Timezone: "America/Phoenix", MaxOnSec: 900,
		Power:    engine.StationConfig{ID: "psu", BCM: 21},
		Stations: []engine.StationConfig{{ID: "front-north", Title: "Front North", BCM: 6}},
	}
	eng, err := engine.New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = eng.Close() })
	fake := &Fake{}
	clk := &testClock{t: time.Now().UTC().Truncate(time.Second)}
	var buf bytes.Buffer
	p := &Poller{
		Source: fake,
		Eng:    eng,
		Path:   filepath.Join(t.TempDir(), "config.json"),
		Cfg: Config{
			Enabled: true, GaugeID: "TEST-GAUGE",
			TriggerInches: 0.25, WindowHours: 24,
			DryDays: 2, HeavyInches: 1, HeavyDryDays: 4, StaleHours: 7,
		},
		Now: func() time.Time { return clk.Now() },
		Log: log.New(&buf, "", 0),
	}
	return p, fake, clk, &buf
}

func beat(now time.Time, inches float64, at time.Time) []Sample {
	return []Sample{{Time: at, Inches: inches}, {Time: now, Inches: 0}}
}

func TestPollerThresholdTiersAndExtend(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	now := clk.Now()
	rainAt := now.Add(-time.Hour)

	fake.Set(beat(now, 0.24, rainAt), nil)
	p.Poll(context.Background())
	if p.Eng.PauseRaw().Paused {
		t.Fatal("0.24 must not pause")
	}
	if st := p.Status(); st.Unavailable || !st.HaveTotal {
		t.Fatalf("fresh under threshold: %+v", st)
	}

	fake.Set(beat(now, 0.25, rainAt), nil)
	p.Poll(context.Background())
	got := p.Eng.PauseRaw()
	if !got.Paused || got.Reason != "rain" || got.Source != engine.PauseSourceAuto {
		t.Fatalf("0.25 pause %+v", got)
	}
	if got.Until == nil || !got.Until.Equal(rainAt.Add(48*time.Hour)) {
		t.Fatalf("until %v", got.Until)
	}
	disk, err := store.LoadPause(p.Path, now)
	if err != nil || !disk.Active || disk.Source != "auto" || disk.Reason != "rain" {
		t.Fatalf("disk %+v %v", disk, err)
	}

	p.Eng.ClearPause()
	heavyAt := now.Add(-30 * time.Minute)
	fake.Set(beat(now, 1, heavyAt), nil)
	p.Poll(context.Background())
	got = p.Eng.PauseRaw()
	if got.Until == nil || !got.Until.Equal(heavyAt.Add(96*time.Hour)) {
		t.Fatalf("heavy until %v", got.Until)
	}

	p.Eng.ClearPause()
	later := now.Add(-10 * time.Minute)
	fake.Set(beat(now, 0.99, later), nil)
	p.Poll(context.Background())
	got = p.Eng.PauseRaw()
	if !got.Paused || got.Until == nil || !got.Until.Equal(later.Add(48*time.Hour)) {
		t.Fatalf("0.99 until %v paused %v", got.Until, got.Paused)
	}
}

func TestPollerExtendNeverShortens(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	p.Cfg.WindowHours = 24
	t0 := clk.Now()
	a := t0.Add(-2 * time.Hour)
	b := t0
	fake.Set([]Sample{{Time: a, Inches: 0.80}, {Time: b, Inches: 0.30}}, nil)
	p.Poll(context.Background())
	first := p.Eng.PauseRaw()
	if first.Until == nil || !first.Until.Equal(b.Add(96*time.Hour)) || first.RainInches < 1.1-1e-9 {
		t.Fatalf("initial heavy %+v", first)
	}

	// Slide the window so only the 0.30 remains. Until must stay at +4 days.
	clk.Set(b.Add(23 * time.Hour))
	now := clk.Now()
	fake.Set([]Sample{{Time: a, Inches: 0.80}, {Time: b, Inches: 0.30}, {Time: now, Inches: 0}}, nil)
	p.Poll(context.Background())
	got := p.Eng.PauseRaw()
	if got.Until == nil || !got.Until.Equal(*first.Until) {
		t.Fatalf("shortened %v want %v", got.Until, first.Until)
	}
	if got.RainInches < 1.1-1e-9 {
		t.Fatalf("inches dropped to %v", got.RainInches)
	}

	// A later tip, still inside the heavy hold, extends it.
	clk.Set(b.Add(3 * 24 * time.Hour))
	now = clk.Now()
	tip := now.Add(-time.Minute)
	fake.Set([]Sample{{Time: tip, Inches: 0.40}, {Time: now, Inches: 0}}, nil)
	p.Poll(context.Background())
	got = p.Eng.PauseRaw()
	if got.Until == nil || !got.Until.After(*first.Until) {
		t.Fatalf("expected extension, until %v first %v", got.Until, first.Until)
	}
}

func TestPollerDryOutClears(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	now := clk.Now()
	rainAt := now.Add(-time.Hour)
	fake.Set(beat(now, 0.30, rainAt), nil)
	p.Poll(context.Background())
	if !p.Eng.PauseRaw().Paused {
		t.Fatal("expected pause")
	}
	clk.Set(rainAt.Add(48*time.Hour + time.Hour))
	now = clk.Now()
	fake.Set([]Sample{{Time: now, Inches: 0}}, nil)
	p.Poll(context.Background())
	if p.Eng.PauseRaw().Paused {
		t.Fatal("dry-out should clear auto pause")
	}
	disk, err := store.LoadPause(p.Path, now)
	if err != nil || disk.Active {
		t.Fatalf("disk still active %+v %v", disk, err)
	}

	// Same rain still inside a long window, and a longer dry-out that would
	// still be in the future, must not re-arm.
	p.Cfg.WindowHours = 200
	p.Cfg.StaleHours = 200
	p.Cfg.DryDays = 10
	fake.Set([]Sample{{Time: rainAt, Inches: 0.30}, {Time: now, Inches: 0}}, nil)
	p.Poll(context.Background())
	if p.Eng.PauseRaw().Paused {
		t.Fatal("same event must not re-pause after dry-out")
	}
}

func TestPollerBadDataChangesNothing(t *testing.T) {
	p, fake, clk, buf := newTestPoller(t)
	now := clk.Now()
	until := now.Add(48 * time.Hour)
	p.Eng.SetPauseMeta(&until, "rain", engine.PauseMeta{Source: engine.PauseSourceAuto, RainInches: 0.4})
	fake.Set(nil, errString("dial TEST-GAUGE https://alert.example/showdata2.php?id=TEST-GAUGE"))
	p.Poll(context.Background())
	got := p.Eng.PauseRaw()
	if !got.Paused || got.Source != engine.PauseSourceAuto {
		t.Fatalf("error cleared pause %+v", got)
	}
	st := p.Status()
	if !st.Unavailable || st.LastError == "" {
		t.Fatalf("status %+v", st)
	}
	if strings.Contains(st.LastError, "TEST-GAUGE") || strings.Contains(st.LastError, "http") || strings.Contains(buf.String(), "TEST-GAUGE") {
		t.Fatalf("leak status %q log %q", st.LastError, buf.String())
	}

	p.Eng.ClearPause()
	buf.Reset()
	p.Poll(context.Background())
	if p.Eng.PauseRaw().Paused {
		t.Fatal("error must not pause")
	}
	if !p.Status().Unavailable {
		t.Fatal("still unavailable")
	}

	fake.Set(beat(now, 0, now), nil)
	p.Poll(context.Background())
	if p.Status().Unavailable || p.Eng.PauseRaw().Paused {
		t.Fatalf("fresh zero rain status %+v paused %v", p.Status(), p.Eng.PauseRaw().Paused)
	}

	// Stale newest sample: pause stays, unpaused stays.
	p.Eng.SetPauseMeta(&until, "rain", engine.PauseMeta{Source: engine.PauseSourceAuto, RainInches: 0.4})
	staleAt := now.Add(-8 * time.Hour)
	fake.Set([]Sample{{Time: staleAt, Inches: 1}}, nil)
	p.Poll(context.Background())
	if !p.Eng.PauseRaw().Paused || !p.Status().Unavailable {
		t.Fatalf("stale changed pause=%v status=%+v", p.Eng.PauseRaw().Paused, p.Status())
	}
	p.Eng.ClearPause()
	p.Poll(context.Background())
	if p.Eng.PauseRaw().Paused {
		t.Fatal("stale must not pause")
	}
}

func TestPollerNegativeIncrementUnavailable(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	now := clk.Now()
	rainAt := now.Add(-time.Hour)
	fake.Set(beat(now, 0.40, rainAt), nil)
	p.Poll(context.Background())
	if !p.Eng.PauseRaw().Paused {
		t.Fatal("expected pause")
	}
	good, ok := p.DailyRain(time.UTC)
	if !ok {
		t.Fatal("expected last-good rain")
	}

	fake.Set([]Sample{{Time: rainAt, Inches: -0.2}, {Time: now, Inches: 0}}, nil)
	p.Poll(context.Background())
	got := p.Eng.PauseRaw()
	if !got.Paused {
		t.Fatal("negative increment must not clear")
	}
	st := p.Status()
	if !st.Unavailable || st.Since == nil || st.LastError != "invalid rain increment" {
		t.Fatalf("status %+v", st)
	}
	gotRain, ok := p.DailyRain(time.UTC)
	if !ok {
		t.Fatal("last good should remain")
	}
	for day, inches := range good {
		if gotRain[day] != inches {
			t.Fatalf("cache mutated %v vs %v", gotRain, good)
		}
	}

	p.Eng.ClearPause()
	p.Poll(context.Background())
	if p.Eng.PauseRaw().Paused {
		t.Fatal("negative increment must not pause")
	}
	if !p.Status().Unavailable {
		t.Fatal("still unavailable")
	}
}

// fcdmcPrecipPage builds a stub precipitation page. rows are written in the
// order given; real pages are newest-first. The header uses the test gauge.
func fcdmcPrecipPage(rows []Sample) string {
	var b strings.Builder
	b.WriteString("<HTML><BODY><P><PRE>\n")
	b.WriteString("00000 Test Gauge\n")
	b.WriteString("Precipitation Gage\n")
	b.WriteString("Date       Time      inches Rainfall   inches\n")
	for _, s := range rows {
		fmt.Fprintf(&b, "%s   1.20  %6.2f  %6.2f\n", s.Time.Format("01/02/2006 15:04:05"), s.Inches, s.Inches)
	}
	b.WriteString("</PRE></P></BODY>\n")
	return b.String()
}

// useFCDMCPage serves body through the real HTTP source, which calls ParseFCDMC.
// It returns the server URL so tests can assert it never reaches the log.
func useFCDMCPage(t *testing.T, p *Poller, body string) string {
	t.Helper()
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(body))
	}))
	t.Cleanup(srv.Close)
	p.Cfg.GaugeID = "00000"
	p.Source = &FCDMC{
		URL:     srv.URL,
		Method:  http.MethodPost,
		Body:    DefaultBody,
		GaugeID: "00000",
		Loc:     time.UTC,
		HTTP:    srv.Client(),
	}
	return srv.URL
}

func parsedNegativePage(now time.Time, negAt time.Time, neg float64, rainAt time.Time, rain float64) string {
	return fcdmcPrecipPage([]Sample{
		{Time: now, Inches: 0},
		{Time: negAt, Inches: neg},
		{Time: rainAt, Inches: rain},
	})
}

func requireNegativeSample(t *testing.T, samples []Sample, at time.Time, inches float64) {
	t.Helper()
	for _, s := range samples {
		if s.Inches == inches && s.Time.Equal(at) {
			return
		}
	}
	t.Fatalf("parser dropped negative increment %+v", samples)
}

func TestPollerParsedNegativeIncrementUnavailable(t *testing.T) {
	p, _, clk, buf := newTestPoller(t)
	now := clk.Now()
	negAt := now.Add(-5 * time.Minute)
	rainAt := now.Add(-35 * time.Minute)
	page := parsedNegativePage(now, negAt, -0.50, rainAt, 0.30)

	samples, err := ParseFCDMC(strings.NewReader(page), time.UTC)
	if err != nil {
		t.Fatal(err)
	}
	requireNegativeSample(t, samples, negAt, -0.50)

	rawURL := useFCDMCPage(t, p, page)
	p.Poll(context.Background())
	if p.Eng.PauseRaw().Paused {
		t.Fatal("negative increment must not start a pause")
	}
	st := p.Status()
	if !st.Unavailable || st.Since == nil || st.LastError != "invalid rain increment" {
		t.Fatalf("status %+v", st)
	}
	if !st.Since.Equal(now.UTC()) {
		t.Fatalf("since %v want %v", st.Since, now.UTC())
	}
	msg := buf.String() + " " + st.LastError
	for _, leak := range []string{"00000", "Test Gauge", rawURL, "127.0.0.1", "http"} {
		if strings.Contains(msg, leak) {
			t.Fatalf("leak %q in %q", leak, msg)
		}
	}
}

func TestPollerParsedNegativeKeepsExistingPause(t *testing.T) {
	p, _, clk, _ := newTestPoller(t)
	now := clk.Now()
	until := now.Add(36 * time.Hour)
	last := now.Add(-2 * time.Hour)
	p.Eng.SetPauseMeta(&until, "rain", engine.PauseMeta{
		Source:      engine.PauseSourceAuto,
		RainInches:  0.40,
		LastRainAt:  &last,
		RainEventAt: &last,
	})

	negAt := now.Add(-5 * time.Minute)
	rainAt := now.Add(-35 * time.Minute)
	page := parsedNegativePage(now, negAt, -0.50, rainAt, 0.30)
	samples, err := ParseFCDMC(strings.NewReader(page), time.UTC)
	if err != nil {
		t.Fatal(err)
	}
	requireNegativeSample(t, samples, negAt, -0.50)
	useFCDMCPage(t, p, page)

	p.Poll(context.Background())
	got := p.Eng.PauseRaw()
	if !got.Paused || got.Source != engine.PauseSourceAuto || got.Reason != "rain" {
		t.Fatalf("pause changed %+v", got)
	}
	if got.Until == nil || !got.Until.Equal(until.UTC()) || got.RainInches != 0.40 {
		t.Fatalf("extended or cleared %+v", got)
	}
	st := p.Status()
	if !st.Unavailable || st.Since == nil || st.LastError != "invalid rain increment" {
		t.Fatalf("status %+v", st)
	}
}

func TestPollerParsedNegativeAgesOut(t *testing.T) {
	p, _, clk, _ := newTestPoller(t)
	now := clk.Now()
	negAt := now.Add(-25 * time.Hour)
	rainAt := now.Add(-35 * time.Minute)
	page := parsedNegativePage(now, negAt, -0.50, rainAt, 0.30)

	samples, err := ParseFCDMC(strings.NewReader(page), time.UTC)
	if err != nil {
		t.Fatal(err)
	}
	requireNegativeSample(t, samples, negAt, -0.50)
	useFCDMCPage(t, p, page)

	p.Poll(context.Background())
	got := p.Eng.PauseRaw()
	if !got.AutoRain() || got.Until == nil || !got.Until.Equal(rainAt.Add(48*time.Hour)) {
		t.Fatalf("expected a normal pause, %+v", got)
	}
	if got.RainInches < 0.30-1e-9 || got.RainInches > 0.30+1e-9 {
		t.Fatalf("negative subtracted from pause inches %v", got.RainInches)
	}
	st := p.Status()
	if st.Unavailable || st.LastError != "" || !st.HaveTotal || st.LastTotal < 0.30-1e-9 || st.LastTotal > 0.30+1e-9 {
		t.Fatalf("status %+v", st)
	}
	days, ok := p.DailyRain(time.UTC)
	if !ok {
		t.Fatal("expected daily totals")
	}
	var sum float64
	for _, inches := range days {
		if inches < 0 {
			t.Fatalf("daily total went negative: %v", days)
		}
		sum += inches
	}
	if sum < 0.30-1e-9 || sum > 0.30+1e-9 {
		t.Fatalf("daily sum %v days %v", sum, days)
	}
}

func TestPollerNegativeIncrementLogsOnce(t *testing.T) {
	p, _, clk, buf := newTestPoller(t)
	now := clk.Now()
	negAt := now.Add(-5 * time.Minute)
	rainAt := now.Add(-35 * time.Minute)
	page := parsedNegativePage(now, negAt, -0.50, rainAt, 0.30)
	if _, err := ParseFCDMC(strings.NewReader(page), time.UTC); err != nil {
		t.Fatal(err)
	}
	rawURL := useFCDMCPage(t, p, page)

	for i := 0; i < 3; i++ {
		p.Poll(context.Background())
	}
	want := fmt.Sprintf("rain: ignoring feed: negative increment at %s (-0.50 in)", negAt.Format("15:04"))
	if strings.Count(buf.String(), want) != 1 {
		t.Fatalf("logs:\n%s", buf.String())
	}
	if strings.Count(buf.String(), "negative increment") != 1 {
		t.Fatalf("logs:\n%s", buf.String())
	}
	if strings.Count(buf.String(), "invalid rain increment") != 1 {
		t.Fatalf("category log repeated:\n%s", buf.String())
	}
	msg := buf.String()
	for _, leak := range []string{"00000", "Test Gauge", rawURL, "127.0.0.1", "http"} {
		if strings.Contains(msg, leak) {
			t.Fatalf("leak %q in %q", leak, msg)
		}
	}

	// A second bad row is logged once; the first row is not repeated.
	neg2 := now.Add(-8 * time.Minute)
	page2 := fcdmcPrecipPage([]Sample{
		{Time: now, Inches: 0},
		{Time: negAt, Inches: -0.50},
		{Time: neg2, Inches: -0.25},
		{Time: rainAt, Inches: 0.30},
	})
	rawURL2 := useFCDMCPage(t, p, page2)
	p.Poll(context.Background())
	p.Poll(context.Background())
	want2 := fmt.Sprintf("rain: ignoring feed: negative increment at %s (-0.25 in)", neg2.Format("15:04"))
	if strings.Count(buf.String(), want) != 1 || strings.Count(buf.String(), want2) != 1 {
		t.Fatalf("logs:\n%s", buf.String())
	}
	if strings.Count(buf.String(), "negative increment") != 2 {
		t.Fatalf("logs:\n%s", buf.String())
	}
	if strings.Count(buf.String(), "invalid rain increment") != 1 {
		t.Fatalf("category log repeated:\n%s", buf.String())
	}

	// The remembered key is dropped once the row leaves the window, and the
	// same old row does not log again.
	clk.Set(now.Add(25 * time.Hour))
	p.Poll(context.Background())
	if len(p.negLogged) != 0 {
		t.Fatalf("neg log set not pruned: %d", len(p.negLogged))
	}
	if strings.Count(buf.String(), "negative increment") != 2 {
		t.Fatalf("logged again after age-out:\n%s", buf.String())
	}
	for _, leak := range []string{"00000", "Test Gauge", rawURL, rawURL2, "127.0.0.1", "http"} {
		if strings.Contains(buf.String(), leak) {
			t.Fatalf("leak %q in %q", leak, buf.String())
		}
	}
}

func TestPollerLeavesManualAlone(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	now := clk.Now()
	rainAt := now.Add(-time.Hour)
	fake.Set(beat(now, 0.8, rainAt), nil)

	long := now.Add(10 * 24 * time.Hour)
	p.Eng.SetPause(&long, "rain")
	p.Poll(context.Background())
	got := p.Eng.PauseRaw()
	if !got.Paused || got.Source != engine.PauseSourceManual || got.Until == nil || !got.Until.Equal(long.UTC()) {
		t.Fatalf("longer manual %+v", got)
	}

	p.Eng.ClearPause()
	short := now.Add(3 * time.Hour)
	p.Eng.SetPause(&short, "mow")
	p.Poll(context.Background())
	got = p.Eng.PauseRaw()
	if got.Reason != "mow" || got.Source != engine.PauseSourceManual || got.Until == nil || !got.Until.Equal(short.UTC()) {
		t.Fatalf("mow %+v", got)
	}

	p.Eng.ClearPause()
	p.Eng.SetPause(nil, "rain")
	p.Poll(context.Background())
	got = p.Eng.PauseRaw()
	if !got.Paused || got.Until != nil || got.Source != engine.PauseSourceManual {
		t.Fatalf("indefinite %+v", got)
	}
}

func TestPollerManualClearThenNewRain(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	now := clk.Now()
	rainAt := now.Add(-2 * time.Hour)
	fake.Set(beat(now, 0.40, rainAt), nil)
	p.Poll(context.Background())
	if !p.Eng.PauseRaw().AutoRain() {
		t.Fatal("expected auto pause")
	}
	if err := p.ManualClear(now); err != nil {
		t.Fatal(err)
	}
	if p.Eng.PauseRaw().Paused {
		t.Fatal("clear should resume")
	}
	p.Poll(context.Background())
	if p.Eng.PauseRaw().Paused {
		t.Fatal("same event re-paused after manual clear")
	}

	// Restart keeps the clear.
	disk, err := store.LoadPause(p.Path, now)
	if err != nil || disk.RainClearedAt == nil {
		t.Fatalf("disk memory %+v %v", disk, err)
	}
	eng2 := p.Eng
	ApplyStoredPause(eng2, disk)
	p.Poll(context.Background())
	if p.Eng.PauseRaw().Paused {
		t.Fatal("restart re-paused the cleared event")
	}

	clk.Set(now.Add(30 * time.Minute))
	now = clk.Now()
	tip := now.Add(-time.Minute)
	fake.Set([]Sample{{Time: rainAt, Inches: 0.20}, {Time: tip, Inches: 0.10}, {Time: now, Inches: 0}}, nil)
	p.Poll(context.Background())
	got := p.Eng.PauseRaw()
	if !got.AutoRain() || got.Until == nil || !got.Until.Equal(tip.Add(48*time.Hour)) {
		t.Fatalf("new rain should re-trigger %+v", got)
	}
}

func TestPollerDoesNotStopARun(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	now := clk.Now()
	fake.Set(beat(now, 0.5, now.Add(-time.Minute)), nil)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan error, 1)
	go func() {
		done <- p.Eng.RunItinerary(ctx, []engine.Step{{StationID: "front-north", Duration: 2 * time.Second}})
	}()
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		if p.Eng.Status().Phase != engine.PhaseIdle {
			break
		}
		time.Sleep(5 * time.Millisecond)
	}
	if p.Eng.Status().Phase == engine.PhaseIdle {
		t.Fatal("run did not start")
	}
	p.Poll(context.Background())
	if p.Eng.Status().Phase == engine.PhaseIdle {
		t.Fatal("poller must not stop an active run")
	}
	if !p.Eng.PauseRaw().AutoRain() {
		t.Fatal("expected auto pause while run continues")
	}
	_ = p.Eng.Stop()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("run did not finish")
	}
}

func TestLegacyPauseFileStaysManual(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	until := clk.Now().Add(10 * 24 * time.Hour)
	ApplyStoredPause(p.Eng, store.PauseState{Active: true, Until: &until, Reason: "rain"})
	got := p.Eng.PauseRaw()
	if !got.Paused || got.Source != engine.PauseSourceManual || got.Until == nil || !got.Until.Equal(until.UTC()) {
		t.Fatalf("legacy load %+v", got)
	}
	fake.Set(beat(clk.Now(), 1, clk.Now().Add(-time.Hour)), nil)
	p.Poll(context.Background())
	got = p.Eng.PauseRaw()
	if got.Source != engine.PauseSourceManual || got.Reason != "rain" || got.Until == nil || !got.Until.Equal(until.UTC()) {
		t.Fatalf("rain poll overrode a legacy pause %+v", got)
	}
}

func TestPollerLoopStops(t *testing.T) {
	p, _, _, _ := newTestPoller(t)
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	done := make(chan struct{})
	go func() {
		p.Loop(ctx)
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("loop did not stop")
	}
}

type errString string

func (e errString) Error() string { return string(e) }

func TestPollerImplausibleDoesNotTouchPause(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	now := clk.Now()
	rainAt := now.Add(-time.Minute)

	fake.Set(beat(now, 99.99, rainAt), nil)
	p.Poll(context.Background())
	if p.Eng.PauseRaw().Paused {
		t.Fatal("implausible must not pause")
	}
	if st := p.Status(); !st.Unavailable || !strings.Contains(st.LastError, "implausible") {
		t.Fatalf("status %+v", st)
	}

	until := now.Add(48 * time.Hour)
	last := rainAt
	p.Eng.SetPauseMeta(&until, "rain", engine.PauseMeta{
		Source: engine.PauseSourceAuto, RainInches: 0.4, LastRainAt: &last, RainEventAt: &last,
	})
	p.Poll(context.Background())
	got := p.Eng.PauseRaw()
	if !got.Paused || got.Until == nil || !got.Until.Equal(until.UTC()) {
		t.Fatalf("extended or cleared %+v", got)
	}
	if !p.Status().Unavailable {
		t.Fatal("want unavailable")
	}

	past := now.Add(-time.Minute)
	p.Eng.SetPauseMeta(&past, "rain", engine.PauseMeta{
		Source: engine.PauseSourceAuto, RainInches: 0.4, LastRainAt: &last,
	})
	p.Poll(context.Background())
	got = p.Eng.PauseRaw()
	if !got.Paused || got.Until == nil || !got.Until.Equal(past.UTC()) {
		t.Fatalf("expired auto cleared %+v", got)
	}

	hold := now.Add(10 * time.Hour)
	p.Eng.SetPauseMeta(&hold, "rain", engine.PauseMeta{
		Source: engine.PauseSourceAuto, RainInches: 0.4, LastRainAt: &last,
	})
	fake.Set([]Sample{
		{Time: now.Add(-4 * time.Hour), Inches: 1.9},
		{Time: now.Add(-3 * time.Hour), Inches: 1.9},
		{Time: now.Add(-2 * time.Hour), Inches: 1.9},
		{Time: now.Add(-time.Hour), Inches: 1.9},
		{Time: now, Inches: 0},
	}, nil)
	p.Poll(context.Background())
	got = p.Eng.PauseRaw()
	if got.Until == nil || !got.Until.Equal(hold.UTC()) || !p.Status().Unavailable {
		t.Fatalf("window cap %+v status %+v", got, p.Status())
	}

	p.Eng.ClearPause()
	tip := now.Add(-time.Second)
	fake.Set(beat(now, 2, tip), nil)
	p.Poll(context.Background())
	got = p.Eng.PauseRaw()
	if !got.Paused || p.Status().Unavailable {
		t.Fatalf("exact increment cap should pause %+v status %+v", got, p.Status())
	}
}

func TestPollerIgnoresFarFutureRows(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	now := clk.Now()
	until := now.Add(36 * time.Hour)
	p.Eng.SetPauseMeta(&until, "rain", engine.PauseMeta{Source: engine.PauseSourceAuto, RainInches: 0.4})
	fake.Set([]Sample{
		{Time: now.Add(-8 * time.Hour), Inches: 0.5},
		{Time: now.Add(48 * time.Hour), Inches: 9},
	}, nil)
	p.Poll(context.Background())
	got := p.Eng.PauseRaw()
	if !got.Paused || got.Until == nil || !got.Until.Equal(until.UTC()) {
		t.Fatalf("future/stale changed pause %+v", got)
	}
	st := p.Status()
	if !st.Unavailable || !strings.Contains(st.LastError, "stale") {
		t.Fatalf("status %+v", st)
	}

	p.Eng.ClearPause()
	p.Poll(context.Background())
	if p.Eng.PauseRaw().Paused {
		t.Fatal("stale future row must not pause")
	}

	tip := now.Add(10 * time.Minute)
	fake.Set([]Sample{{Time: tip, Inches: 0.3}, {Time: now.Add(48 * time.Hour), Inches: 40}}, nil)
	p.Poll(context.Background())
	if !p.Eng.PauseRaw().Paused || p.Status().Unavailable {
		t.Fatalf("10 min future should count paused=%v status=%+v", p.Eng.PauseRaw().Paused, p.Status())
	}
	if p.Eng.PauseRaw().RainInches > 1 {
		t.Fatalf("future inches counted %v", p.Eng.PauseRaw().RainInches)
	}
}

func TestPollerUnavailableLogOnce(t *testing.T) {
	p, fake, clk, buf := newTestPoller(t)
	now := clk.Now()
	fake.Set(nil, errString("dial TEST-GAUGE https://alert.example/showdata2.php?id=TEST-GAUGE"))
	for i := 0; i < 3; i++ {
		p.Poll(context.Background())
	}
	if strings.Count(buf.String(), "rain: fetch failed:") != 1 {
		t.Fatalf("logs:\n%s", buf.String())
	}
	if strings.Contains(buf.String(), "TEST-GAUGE") || strings.Contains(buf.String(), "http") {
		t.Fatalf("leak %s", buf.String())
	}
	fake.Set(beat(now, 0, now), nil)
	p.Poll(context.Background())
	p.Poll(context.Background())
	if strings.Count(buf.String(), "rain: data available again") != 1 {
		t.Fatalf("logs:\n%s", buf.String())
	}
	staleAt := now.Add(-8 * time.Hour)
	fake.Set([]Sample{{Time: staleAt, Inches: 0.4}}, nil)
	p.Poll(context.Background())
	p.Poll(context.Background())
	if strings.Count(buf.String(), "stale rain data") != 1 {
		t.Fatalf("logs:\n%s", buf.String())
	}
}

func TestPollerClosedServerRedacts(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(http.ResponseWriter, *http.Request) {}))
	rawURL := srv.URL + "/TEST-GAUGE"
	host := srv.Listener.Addr().String()
	srv.Close()

	p, _, _, buf := newTestPoller(t)
	p.Source = &FCDMC{
		URL:     rawURL,
		Method:  http.MethodPost,
		Body:    DefaultBody,
		GaugeID: "TEST-GAUGE",
		HTTP:    &http.Client{Timeout: time.Second},
	}
	p.Poll(context.Background())
	logText := buf.String()
	msg := logText + " " + p.Status().LastError
	if !p.Status().Unavailable {
		t.Fatalf("status %+v log %q", p.Status(), logText)
	}
	for _, leak := range []string{"TEST-GAUGE", rawURL, host, "127.0.0.1"} {
		if strings.Contains(msg, leak) {
			t.Fatalf("leak %q in %q", leak, msg)
		}
	}
	if strings.Count(msg, `"`)%2 != 0 {
		t.Fatalf("unbalanced quotes: %q", msg)
	}
	if !strings.Contains(logText, "rain: fetch failed:") || !strings.Contains(logText, `"<redacted>"`) {
		t.Fatalf("log %q", logText)
	}
}

func TestPersistOlderSnapshotDoesNotOverwrite(t *testing.T) {
	p, _, _, _ := newTestPoller(t)
	newer := store.PauseState{Active: true, Reason: "newer", Source: engine.PauseSourceAuto, RainInches: 0.4}
	if err := p.persist(p.Path, newer, 2, store.SavePause); err != nil {
		t.Fatal(err)
	}
	wroteOlder := false
	err := p.persist(p.Path, store.PauseState{Active: true, Reason: "older", Source: "manual"}, 1, func(path string, ps store.PauseState) error {
		wroteOlder = true
		return store.SavePause(path, ps)
	})
	if err != nil {
		t.Fatal(err)
	}
	if wroteOlder {
		t.Fatal("older snapshot was written after a newer one")
	}
	disk, err := store.LoadPause(p.Path, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if !disk.Active || disk.Reason != "newer" || disk.RainInches != 0.4 {
		t.Fatalf("disk %+v", disk)
	}
}

func TestStatusDuringBlockedPauseSave(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	now := clk.Now()
	release := make(chan struct{})
	entered := make(chan struct{})
	var once sync.Once
	var closeOnce sync.Once
	closeRelease := func() { closeOnce.Do(func() { close(release) }) }
	t.Cleanup(closeRelease)
	p.SetSaveFuncForTest(func(path string, ps store.PauseState) error {
		once.Do(func() { close(entered) })
		<-release
		return store.SavePause(path, ps)
	})
	fake.Set(beat(now, 0.5, now.Add(-time.Hour)), nil)

	pollDone := make(chan struct{})
	go func() {
		p.Poll(context.Background())
		close(pollDone)
	}()
	select {
	case <-entered:
	case <-time.After(2 * time.Second):
		t.Fatal("save did not start")
	}
	start := time.Now()
	st := p.Status()
	if d := time.Since(start); d > 200*time.Millisecond {
		t.Fatalf("Status took %s", d)
	}
	if !st.Enabled || st.Unavailable {
		t.Fatalf("status %+v", st)
	}
	if !p.Eng.PauseRaw().Paused {
		t.Fatal("engine should already be paused while save is blocked")
	}
	closeRelease()
	select {
	case <-pollDone:
	case <-time.After(2 * time.Second):
		t.Fatal("poll did not finish")
	}
	disk, err := store.LoadPause(p.Path, now)
	if err != nil || !disk.Active || disk.Source != "auto" {
		t.Fatalf("disk %+v %v", disk, err)
	}
}

func TestBlockedOlderSaveDoesNotWin(t *testing.T) {
	p, _, _, _ := newTestPoller(t)
	release := make(chan struct{})
	entered := make(chan struct{})
	var closeOnce sync.Once
	closeRelease := func() { closeOnce.Do(func() { close(release) }) }
	t.Cleanup(closeRelease)
	save := func(path string, ps store.PauseState) error {
		if ps.Reason == "older" {
			close(entered)
			<-release
		}
		return store.SavePause(path, ps)
	}
	errCh := make(chan error, 2)
	go func() {
		errCh <- p.persist(p.Path, store.PauseState{Active: true, Reason: "older", Source: "manual"}, 1, save)
	}()
	select {
	case <-entered:
	case <-time.After(2 * time.Second):
		t.Fatal("older save did not start")
	}
	go func() {
		errCh <- p.persist(p.Path, store.PauseState{Active: false, Reason: "newer"}, 2, save)
	}()
	start := time.Now()
	_ = p.Status()
	if d := time.Since(start); d > 200*time.Millisecond {
		t.Fatalf("Status took %s", d)
	}
	closeRelease()
	for i := 0; i < 2; i++ {
		if err := <-errCh; err != nil {
			t.Fatal(err)
		}
	}
	disk, err := store.LoadPause(p.Path, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if disk.Active || disk.Reason != "newer" {
		t.Fatalf("latest lost %+v", disk)
	}
}
