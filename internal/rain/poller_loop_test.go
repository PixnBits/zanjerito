package rain

import (
	"context"
	"strings"
	"sync"
	"testing"
	"time"
)

// fakeAfter is a Loop wait that records durations and fires on demand.
// After's channel is buffered so Fire cannot deadlock if Loop has not
// entered select yet. Stop matches time.Timer.Stop.
type fakeAfter struct {
	mu      sync.Mutex
	waits   []time.Duration
	current *fakeTimer
}

type fakeTimer struct {
	mu      sync.Mutex
	d       time.Duration
	ch      chan time.Time
	stopped bool
	fired   bool
}

func newFakeAfter() *fakeAfter {
	return &fakeAfter{}
}

func (f *fakeAfter) After(d time.Duration) (<-chan time.Time, func() bool) {
	ch := make(chan time.Time, 1)
	ft := &fakeTimer{d: d, ch: ch}
	f.mu.Lock()
	f.waits = append(f.waits, d)
	f.current = ft
	f.mu.Unlock()
	return ch, ft.stop
}

func (t *fakeTimer) stop() bool {
	t.mu.Lock()
	defer t.mu.Unlock()
	if t.stopped || t.fired {
		t.stopped = true
		return false
	}
	t.stopped = true
	return true
}

func (f *fakeAfter) waitN(t *testing.T, n int) []time.Duration {
	t.Helper()
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		f.mu.Lock()
		if len(f.waits) >= n {
			out := append([]time.Duration(nil), f.waits...)
			f.mu.Unlock()
			return out
		}
		f.mu.Unlock()
		time.Sleep(time.Millisecond)
	}
	f.mu.Lock()
	got := append([]time.Duration(nil), f.waits...)
	f.mu.Unlock()
	t.Fatalf("waiting for %d scheduled waits, got %v", n, got)
	return nil
}

func (f *fakeAfter) fire(t *testing.T) {
	t.Helper()
	f.mu.Lock()
	cur := f.current
	f.mu.Unlock()
	if cur == nil {
		t.Fatal("no timer to fire")
	}
	cur.mu.Lock()
	if cur.stopped || cur.fired {
		cur.mu.Unlock()
		t.Fatal("timer already stopped or fired")
	}
	cur.fired = true
	ch := cur.ch
	cur.mu.Unlock()
	ch <- time.Time{}
}

func (f *fakeAfter) stopped() bool {
	f.mu.Lock()
	cur := f.current
	f.mu.Unlock()
	if cur == nil {
		return false
	}
	cur.mu.Lock()
	defer cur.mu.Unlock()
	return cur.stopped
}

func startLoop(t *testing.T, p *Poller) (context.CancelFunc, <-chan struct{}) {
	t.Helper()
	ctx, cancel := context.WithCancel(context.Background())
	done := make(chan struct{})
	go func() {
		p.Loop(ctx)
		close(done)
	}()
	t.Cleanup(func() {
		cancel()
		select {
		case <-done:
		case <-time.After(2 * time.Second):
			t.Errorf("Loop did not stop")
		}
	})
	return cancel, done
}

func assertWaitPrefix(t *testing.T, got, want []time.Duration) {
	t.Helper()
	if len(got) < len(want) {
		t.Fatalf("waits %v want prefix %v", got, want)
	}
	for i, w := range want {
		if got[i] != w {
			t.Fatalf("wait[%d]=%s want %s (all %v)", i, got[i], w, got)
		}
	}
}

func TestLoopRetriesAfterFailureThenNormalCadence(t *testing.T) {
	t.Run("default30m", func(t *testing.T) {
		testLoopRetryCadence(t, 0, 30*time.Minute, []time.Duration{
			time.Minute,
			2 * time.Minute,
			4 * time.Minute,
			8 * time.Minute,
			16 * time.Minute,
			30 * time.Minute,
		})
	})
	t.Run("poll5m", func(t *testing.T) {
		testLoopRetryCadence(t, 5, 5*time.Minute, []time.Duration{
			time.Minute,
			2 * time.Minute,
			4 * time.Minute,
			5 * time.Minute,
		})
	})
}

func testLoopRetryCadence(t *testing.T, pollMinutes float64, interval time.Duration, growth []time.Duration) {
	t.Helper()
	p, fake, clk, _ := newTestPoller(t)
	if pollMinutes > 0 {
		p.Cfg.PollMinutes = pollMinutes
	}
	if p.Cfg.PollInterval() != interval {
		t.Fatalf("PollInterval %s want %s", p.Cfg.PollInterval(), interval)
	}
	now := clk.Now()
	fake.Set(nil, errString("network is unreachable"))
	ft := newFakeAfter()
	p.after = ft.After
	startLoop(t, p)

	waits := ft.waitN(t, 1)
	for i := 1; i < len(growth); i++ {
		ft.fire(t)
		waits = ft.waitN(t, i+1)
	}
	assertWaitPrefix(t, waits, growth)

	fake.Set(beat(now, 0, now), nil)
	ft.fire(t)
	waits = ft.waitN(t, len(growth)+1)
	if got := waits[len(growth)]; got != interval {
		t.Fatalf("after success wait %s want %s (all %v)", got, interval, waits)
	}

	staleAt := now.Add(-8 * time.Hour)
	fake.Set([]Sample{{Time: staleAt, Inches: 0.4}}, nil)
	ft.fire(t)
	waits = ft.waitN(t, len(growth)+2)
	if got := waits[len(growth)+1]; got != interval {
		t.Fatalf("stale wait %s want interval %s (all %v)", got, interval, waits)
	}

	fake.Set(nil, errString("network is unreachable"))
	ft.fire(t)
	waits = ft.waitN(t, len(growth)+3)
	if got := waits[len(growth)+2]; got != time.Minute {
		t.Fatalf("after reset wait %s want 1m (all %v)", got, waits)
	}
}

func TestLoopFirstPollFailureRecoversWithoutWaiting30m(t *testing.T) {
	p, fake, clk, _ := newTestPoller(t)
	now := clk.Now()
	fake.Set(nil, errString("dial tcp: lookup example.test: network is unreachable"))
	ft := newFakeAfter()
	p.after = ft.After
	startLoop(t, p)

	waits := ft.waitN(t, 1)
	if len(waits) != 1 || waits[0] != time.Minute {
		t.Fatalf("first wait %v want [1m]", waits)
	}
	st := p.Status()
	if !st.Unavailable || st.LastOK != nil {
		t.Fatalf("after first fail %+v", st)
	}

	clk.Set(now.Add(time.Minute))
	fake.Set(beat(clk.Now(), 0, clk.Now()), nil)
	ft.fire(t)
	waits = ft.waitN(t, 2)
	if waits[0] != time.Minute {
		t.Fatalf("recovered after %s wait, want 1m (all %v)", waits[0], waits)
	}
	if waits[1] != p.Cfg.PollInterval() {
		t.Fatalf("after recovery wait %s want %s", waits[1], p.Cfg.PollInterval())
	}
	st = p.Status()
	if st.Unavailable {
		t.Fatalf("still unavailable %+v", st)
	}
	if st.LastOK == nil {
		t.Fatal("LastOK unset after recovery")
	}
	if !st.LastOK.Equal(clk.Now().UTC()) {
		t.Fatalf("LastOK %v want %v", st.LastOK, clk.Now().UTC())
	}
}

func TestLoopShutdownDuringBackoff(t *testing.T) {
	p, fake, _, _ := newTestPoller(t)
	fake.Set(nil, errString("network is unreachable"))
	ft := newFakeAfter()
	p.after = ft.After
	cancel, done := startLoop(t, p)
	ft.waitN(t, 1)

	start := time.Now()
	cancel()
	select {
	case <-done:
	case <-time.After(500 * time.Millisecond):
		t.Fatal("Loop did not return promptly")
	}
	if d := time.Since(start); d >= 500*time.Millisecond {
		t.Fatalf("Loop took %s to stop", d)
	}
	if !ft.stopped() {
		t.Fatal("backoff timer was not stopped")
	}
}

type blockUntilCancel struct {
	inFetch chan struct{}
}

func (b *blockUntilCancel) Fetch(ctx context.Context) ([]Sample, error) {
	select {
	case <-b.inFetch:
	default:
		close(b.inFetch)
	}
	<-ctx.Done()
	return nil, ctx.Err()
}

func TestLoopFailureNeverBlocks(t *testing.T) {
	p, _, clk, _ := newTestPoller(t)
	src := &blockUntilCancel{inFetch: make(chan struct{})}
	p.Source = src

	start := time.Now()
	cancel, done := startLoop(t, p)
	if d := time.Since(start); d > 100*time.Millisecond {
		t.Fatalf("starting Loop blocked for %s", d)
	}

	select {
	case <-src.inFetch:
	case <-time.After(2 * time.Second):
		t.Fatal("Fetch did not start")
	}

	start = time.Now()
	st := p.Status()
	if d := time.Since(start); d > 200*time.Millisecond {
		t.Fatalf("Status took %s", d)
	}
	if !st.Enabled {
		t.Fatalf("status %+v", st)
	}
	start = time.Now()
	_, _ = p.DailyRain(time.UTC)
	_, _ = p.TotalSince(time.Hour, clk.Now())
	p.Do(func() {})
	if d := time.Since(start); d > 200*time.Millisecond {
		t.Fatalf("other Poller methods took %s", d)
	}

	cancel()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("Loop did not end after cancel")
	}
}

func TestLoopIdenticalFetchFailuresLogOnce(t *testing.T) {
	p, fake, clk, buf := newTestPoller(t)
	now := clk.Now()
	fake.Set(nil, errString("network is unreachable"))
	ft := newFakeAfter()
	p.after = ft.After
	startLoop(t, p)

	const n = 5
	ft.waitN(t, 1)
	for i := 0; i < n-1; i++ {
		ft.fire(t)
		ft.waitN(t, i+2)
	}
	if strings.Count(buf.String(), "rain: fetch failed:") != 1 {
		t.Fatalf("failure log repeated:\n%s", buf.String())
	}

	fake.Set(beat(now, 0, now), nil)
	ft.fire(t)
	ft.waitN(t, n+1)
	logs := buf.String()
	if strings.Count(logs, "rain: fetch failed:") != 1 {
		t.Fatalf("failure log after recovery:\n%s", logs)
	}
	if strings.Count(logs, "rain: data available again") != 1 {
		t.Fatalf("want one recovery line:\n%s", logs)
	}
	if strings.Contains(logs, "TEST-GAUGE") || strings.Contains(logs, "http") {
		t.Fatalf("leak %s", logs)
	}
}

func TestFetchRetryDelay(t *testing.T) {
	min := time.Minute
	cap30 := 30 * time.Minute
	cases := []struct {
		name     string
		fails    int
		interval time.Duration
		want     time.Duration
	}{
		{"30m/0", 0, 30 * min, 30 * min},
		{"30m/1", 1, 30 * min, min},
		{"30m/2", 2, 30 * min, 2 * min},
		{"30m/3", 3, 30 * min, 4 * min},
		{"30m/4", 4, 30 * min, 8 * min},
		{"30m/5", 5, 30 * min, 16 * min},
		{"30m/6", 6, 30 * min, cap30},
		{"30m/7", 7, 30 * min, cap30},

		{"2h/0", 0, 2 * time.Hour, 2 * time.Hour},
		{"2h/1", 1, 2 * time.Hour, min},
		{"2h/2", 2, 2 * time.Hour, 2 * min},
		{"2h/3", 3, 2 * time.Hour, 4 * min},
		{"2h/4", 4, 2 * time.Hour, 8 * min},
		{"2h/5", 5, 2 * time.Hour, 16 * min},
		{"2h/6", 6, 2 * time.Hour, cap30},
		{"2h/7", 7, 2 * time.Hour, cap30},

		{"5m/0", 0, 5 * min, 5 * min},
		{"5m/1", 1, 5 * min, min},
		{"5m/2", 2, 5 * min, 2 * min},
		{"5m/3", 3, 5 * min, 4 * min},
		{"5m/4", 4, 5 * min, 5 * min},

		{"20s/0", 0, 20 * time.Second, 20 * time.Second},
		{"20s/1", 1, 20 * time.Second, 20 * time.Second},
		{"20s/2", 2, 20 * time.Second, 20 * time.Second},
		{"20s/3", 3, 20 * time.Second, 20 * time.Second},

		{"1m/0", 0, min, min},
		{"1m/1", 1, min, min},
		{"1m/2", 2, min, min},
		{"1m/3", 3, min, min},

		{"30m/40", 40, 30 * min, cap30},
		{"30m/1000", 1000, 30 * min, cap30},
		{"2h/40", 40, 2 * time.Hour, cap30},
		{"2h/1000", 1000, 2 * time.Hour, cap30},
		{"5m/40", 40, 5 * min, 5 * min},
		{"5m/1000", 1000, 5 * min, 5 * min},
		{"20s/1000", 1000, 20 * time.Second, 20 * time.Second},

		{"neg/30m", -1, 30 * min, 30 * min},
		{"neg/2h", -1, 2 * time.Hour, 2 * time.Hour},
		{"neg/5m", -1, 5 * min, 5 * min},
		{"neg/20s", -1, 20 * time.Second, 20 * time.Second},
		{"neg/1m", -1, min, min},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got := fetchRetryDelay(tc.fails, tc.interval)
			if got != tc.want {
				t.Fatalf("fetchRetryDelay(%d, %s)=%s want %s", tc.fails, tc.interval, got, tc.want)
			}
		})
	}
}

func TestLoopPollTwoHoursCapsRetryAt30Minutes(t *testing.T) {
	testLoopRetryCadence(t, 120, 2*time.Hour, []time.Duration{
		time.Minute,
		2 * time.Minute,
		4 * time.Minute,
		8 * time.Minute,
		16 * time.Minute,
		30 * time.Minute,
		30 * time.Minute,
	})
}

func TestLoopPollSecondsUnderOneMinuteRetriesAtInterval(t *testing.T) {
	p, fake, _, _ := newTestPoller(t)
	p.Cfg.PollSeconds = 20
	if got := p.Cfg.PollInterval(); got != 20*time.Second {
		t.Fatalf("PollInterval %s want 20s", got)
	}
	fake.Set(nil, errString("network is unreachable"))
	ft := newFakeAfter()
	p.after = ft.After
	startLoop(t, p)

	const n = 3
	waits := ft.waitN(t, 1)
	for i := 1; i < n; i++ {
		ft.fire(t)
		waits = ft.waitN(t, i+1)
	}
	assertWaitPrefix(t, waits, []time.Duration{
		20 * time.Second,
		20 * time.Second,
		20 * time.Second,
	})
	for i, got := range waits {
		if got == time.Minute {
			t.Fatalf("wait[%d]=1m, short interval must not retry at fetchRetryMin (all %v)", i, waits)
		}
	}
}
