package engine

import (
	"context"
	"errors"
	"sync"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/gpio"
)

type capture struct {
	mu     sync.Mutex
	got    []RunRecord
	drv    gpio.Driver
	allOff bool
	saw    bool
}

func (c *capture) Record(r RunRecord) {
	allOff := true
	if c.drv != nil {
		st := gpio.StateForTest(c.drv)
		if len(st) == 0 {
			allOff = false
		}
		for _, lv := range st {
			if lv != gpio.Off {
				allOff = false
			}
		}
	}
	c.mu.Lock()
	c.got = append(c.got, r)
	c.allOff = allOff
	c.saw = true
	c.mu.Unlock()
}

func (c *capture) snapshot() []RunRecord {
	c.mu.Lock()
	defer c.mu.Unlock()
	return append([]RunRecord(nil), c.got...)
}

func absInt(n int) int {
	if n < 0 {
		return -n
	}
	return n
}

func waitNotIdle(t *testing.T, e *Engine) {
	t.Helper()
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		if e.Status().Phase != PhaseIdle {
			return
		}
		time.Sleep(5 * time.Millisecond)
	}
	t.Fatal("run did not leave Idle")
}

func TestRunRecordsCompletedActualNearPlanned(t *testing.T) {
	cfg := testConfig(t)
	drv := gpio.NewFake()
	e, err := New(cfg, drv)
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	rec := &capture{drv: drv}
	e.SetRecorder(rec)

	dur := 1100 * time.Millisecond
	steps := []Step{{StationID: "front-west", Duration: dur}}
	if err := e.RunItinerary(context.Background(), steps); err != nil {
		t.Fatal(err)
	}
	got := rec.snapshot()
	if len(got) != 1 {
		t.Fatalf("got %d records", len(got))
	}
	g := got[0]
	if g.Outcome != OutcomeCompleted || g.Kind != KindManual || g.Program != "manual" {
		t.Fatalf("outcome=%s kind=%s program=%q err=%q", g.Outcome, g.Kind, g.Program, g.Error)
	}
	if !rec.allOff {
		t.Fatal("record emitted before relays were off")
	}
	if len(g.Stations) != 1 || g.Stations[0].StationID != "front-west" {
		t.Fatalf("stations %+v", g.Stations)
	}
	if g.Stations[0].PlannedSec != int(dur/time.Second) {
		t.Fatalf("planned %d", g.Stations[0].PlannedSec)
	}
	if d := absInt(g.Stations[0].ActualSec - g.Stations[0].PlannedSec); d > 1 {
		t.Fatalf("actual %d planned %d", g.Stations[0].ActualSec, g.Stations[0].PlannedSec)
	}
}

func TestLockoutRecordsRefused(t *testing.T) {
	cfg := testConfig(t)
	drv := gpio.NewLockout()
	e, err := New(cfg, drv)
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	rec := &capture{drv: drv}
	e.SetRecorder(rec)
	steps := []Step{
		{StationID: "front-west", Duration: 1100 * time.Millisecond},
		{StationID: "front-north", Duration: 1100 * time.Millisecond},
	}
	if err := e.RunItinerary(context.Background(), steps); err != nil {
		t.Fatal(err)
	}
	got := rec.snapshot()
	if len(got) != 1 {
		t.Fatalf("got %d records", len(got))
	}
	g := got[0]
	if g.Outcome != OutcomeRefused || g.Error != "lockout driver: relays not energized" {
		t.Fatalf("outcome=%s err=%q", g.Outcome, g.Error)
	}
	if len(g.Stations) != 2 {
		t.Fatalf("%+v", g.Stations)
	}
	for _, st := range g.Stations {
		if st.ActualSec != 0 {
			t.Fatalf("actual %d on %s", st.ActualSec, st.StationID)
		}
	}
	if g.Stations[0].PlannedSec != 1 || g.Stations[1].PlannedSec != 1 {
		t.Fatalf("planned %+v", g.Stations)
	}
	if e.Status().Phase != PhaseIdle {
		t.Fatalf("phase %s", e.Status().Phase)
	}
	st := gpio.StateForTest(drv)
	for id, lv := range st {
		if lv != gpio.Off {
			t.Fatalf("%s energized under lockout", id)
		}
	}
}

func TestLockoutStopRecordsRefused(t *testing.T) {
	cfg := testConfig(t)
	drv := gpio.NewLockout()
	e, err := New(cfg, drv)
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	rec := &capture{drv: drv}
	e.SetRecorder(rec)
	errCh := make(chan error, 1)
	go func() {
		errCh <- e.RunItinerary(context.Background(), []Step{{StationID: "front-west", Duration: 3 * time.Second}})
	}()
	waitNotIdle(t, e)
	if err := e.Stop(); err != nil {
		t.Fatal(err)
	}
	if err := <-errCh; !errors.Is(err, context.Canceled) {
		t.Fatalf("got %v", err)
	}
	got := rec.snapshot()
	if len(got) != 1 {
		t.Fatalf("got %d records", len(got))
	}
	g := got[0]
	if g.Outcome != OutcomeRefused || g.Error != "lockout driver: relays not energized" {
		t.Fatalf("outcome=%s err=%q", g.Outcome, g.Error)
	}
	if len(g.Stations) != 1 || g.Stations[0].ActualSec != 0 {
		t.Fatalf("stations %+v", g.Stations)
	}
	if e.Status().Phase != PhaseIdle {
		t.Fatalf("phase %s", e.Status().Phase)
	}
	if !rec.allOff {
		t.Fatal("refused record emitted while a relay was on")
	}
	st := gpio.StateForTest(drv)
	for id, lv := range st {
		if lv != gpio.Off {
			t.Fatalf("%s energized under lockout", id)
		}
	}
}

func TestRunProgramTaggedSchedule(t *testing.T) {
	cfg := testConfig(t)
	e, err := New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	rec := &capture{}
	e.SetRecorder(rec)
	steps := []Step{
		{StationID: "front-west", Duration: 80 * time.Millisecond},
		{StationID: "front-north", Duration: 80 * time.Millisecond},
	}
	if err := e.RunProgram(context.Background(), "dawn", "Dawn soak", steps); err != nil {
		t.Fatal(err)
	}
	got := rec.snapshot()
	if len(got) != 1 {
		t.Fatalf("got %d", len(got))
	}
	g := got[0]
	if g.Kind != KindSchedule || g.Program != "Dawn soak" || g.ProgramID != "dawn" || g.Outcome != OutcomeCompleted {
		t.Fatalf("%+v", g)
	}
	if len(g.Stations) != 2 || g.Stations[0].StationID != "front-west" || g.Stations[1].StationID != "front-north" {
		t.Fatalf("stations %+v", g.Stations)
	}
}

func TestRunRecordsIsolateStations(t *testing.T) {
	cfg := testConfig(t)
	cfg.Sequencing.Mode = SequenceIsolate
	e, err := New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	rec := &capture{}
	e.SetRecorder(rec)
	steps := []Step{
		{StationID: "front-west", Duration: 40 * time.Millisecond},
		{StationID: "front-south", Duration: 40 * time.Millisecond},
	}
	if err := e.RunItinerary(context.Background(), steps); err != nil {
		t.Fatal(err)
	}
	got := rec.snapshot()
	if len(got) != 1 || got[0].Outcome != OutcomeCompleted || len(got[0].Stations) != 2 {
		t.Fatalf("%+v", got)
	}
	if got[0].Stations[0].StationID != "front-west" || got[0].Stations[1].StationID != "front-south" {
		t.Fatalf("%+v", got[0].Stations)
	}
}

func TestRunRecordsStoppedActualLessThanPlanned(t *testing.T) {
	cfg := testConfig(t)
	drv := gpio.NewFake()
	e, err := New(cfg, drv)
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	rec := &capture{drv: drv}
	e.SetRecorder(rec)
	errCh := make(chan error, 1)
	go func() {
		errCh <- e.RunItinerary(context.Background(), []Step{{StationID: "front-west", Duration: 3 * time.Second}})
	}()
	waitNotIdle(t, e)
	if err := e.Stop(); err != nil {
		t.Fatal(err)
	}
	if err := <-errCh; !errors.Is(err, context.Canceled) {
		t.Fatalf("got %v", err)
	}
	got := rec.snapshot()
	if len(got) != 1 || got[0].Outcome != OutcomeStopped {
		t.Fatalf("%+v", got)
	}
	st := got[0].Stations
	if len(st) != 1 || st[0].ActualSec >= st[0].PlannedSec || st[0].PlannedSec != 3 {
		t.Fatalf("station %+v", st)
	}
	if !rec.allOff {
		t.Fatal("stopped record emitted while a relay was on")
	}
}

type failOn struct {
	gpio.Driver
	id string
}

func (f failOn) Set(id string, level gpio.Level) error {
	if id == f.id && level == gpio.On {
		return errors.New("station relay failed")
	}
	return f.Driver.Set(id, level)
}

func TestRunRecordsFault(t *testing.T) {
	cfg := testConfig(t)
	e, err := New(cfg, failOn{Driver: gpio.NewFake(), id: "front-west"})
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	rec := &capture{}
	e.SetRecorder(rec)
	err = e.RunItinerary(context.Background(), []Step{{StationID: "front-west", Duration: time.Second}})
	if err == nil || err.Error() != "station relay failed" {
		t.Fatalf("got %v", err)
	}
	got := rec.snapshot()
	if len(got) != 1 || got[0].Outcome != OutcomeError || got[0].Error != err.Error() {
		t.Fatalf("%+v", got)
	}
	if e.Status().Phase != PhaseFault {
		t.Fatalf("phase %s", e.Status().Phase)
	}
	// Rejection while faulted is not a second record.
	if err := e.RunItinerary(context.Background(), []Step{{StationID: "front-north", Duration: time.Second}}); !errors.Is(err, ErrFaulted) {
		t.Fatalf("got %v", err)
	}
	if len(rec.snapshot()) != 1 {
		t.Fatalf("faulted rejection recorded: %+v", rec.snapshot())
	}
}

func TestRecordSkipped(t *testing.T) {
	cfg := testConfig(t)
	e, err := New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	at := time.Date(2026, 9, 22, 8, 23, 0, 0, time.UTC)
	// Nil recorder is a no-op.
	e.RecordSkipped("dawn", "Morning", nil, "rain", at)
	rec := &capture{}
	e.SetRecorder(rec)
	e.RecordSkipped("dawn", "Morning", []Step{{StationID: "front-west", Duration: 4 * time.Minute}}, "rain", at)
	got := rec.snapshot()
	if len(got) != 1 {
		t.Fatalf("%+v", got)
	}
	g := got[0]
	if g.Outcome != OutcomeSkipped || g.Kind != KindSchedule || g.Program != "Morning" || g.ProgramID != "dawn" || g.Reason != "rain" {
		t.Fatalf("%+v", g)
	}
	if !g.Start.Equal(at) || !g.End.Equal(at) {
		t.Fatalf("start %s end %s", g.Start, g.End)
	}
	if len(g.Stations) != 1 || g.Stations[0].PlannedSec != 240 || g.Stations[0].ActualSec != 0 {
		t.Fatalf("%+v", g.Stations)
	}
}

func TestRejectionsNotRecorded(t *testing.T) {
	cfg := testConfig(t)
	e, err := New(cfg, gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	rec := &capture{}
	e.SetRecorder(rec)

	if err := e.RunItinerary(context.Background(), []Step{{StationID: "nope", Duration: time.Second}}); err == nil {
		t.Fatal("expected unknown station")
	}
	if err := e.RunItinerary(context.Background(), []Step{{StationID: "front-west", Duration: 0}}); err == nil {
		t.Fatal("expected bad duration")
	}
	if err := e.RunItinerary(context.Background(), []Step{{StationID: "front-west", Duration: 901 * time.Second}}); err == nil {
		t.Fatal("expected too long")
	}
	e.SetPause(nil, "rain")
	if err := e.RunItinerary(context.Background(), []Step{{StationID: "front-west", Duration: time.Second}}); !errors.Is(err, ErrPaused) {
		t.Fatalf("got %v", err)
	}
	e.ClearPause()
	if len(rec.snapshot()) != 0 {
		t.Fatalf("validation recorded %+v", rec.snapshot())
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	done := make(chan struct{})
	go func() {
		_ = e.RunItinerary(ctx, []Step{{StationID: "front-west", Duration: 2 * time.Second}})
		close(done)
	}()
	waitNotIdle(t, e)
	if err := e.RunItinerary(context.Background(), []Step{{StationID: "front-north", Duration: time.Millisecond}}); !errors.Is(err, ErrBusy) {
		t.Fatalf("got %v", err)
	}
	if len(rec.snapshot()) != 0 {
		t.Fatal("busy rejection recorded")
	}
	cancel()
	<-done
}

type blockFirst struct {
	once sync.Once
	hold chan struct{}
}

func (b *blockFirst) Record(RunRecord) {
	block := false
	b.once.Do(func() { block = true })
	if block {
		<-b.hold
	}
}

func TestStopNotBlockedByRecorder(t *testing.T) {
	cfg := testConfig(t)
	drv := gpio.NewFake()
	e, err := New(cfg, drv)
	if err != nil {
		t.Fatal(err)
	}
	hold := make(chan struct{})
	defer e.Close()
	defer close(hold)
	e.SetRecorder(&blockFirst{hold: hold})

	go func() {
		_ = e.RunItinerary(context.Background(), []Step{{StationID: "front-west", Duration: 3 * time.Second}})
	}()
	waitNotIdle(t, e)

	type stopResult struct {
		err error
		d   time.Duration
	}
	stopCh := make(chan stopResult, 1)
	go func() {
		t0 := time.Now()
		err := e.Stop()
		stopCh <- stopResult{err: err, d: time.Since(t0)}
	}()
	select {
	case r := <-stopCh:
		if r.err != nil {
			t.Fatal(r.err)
		}
		if r.d > 100*time.Millisecond {
			t.Fatalf("Stop took %s; recorder must not delay it", r.d)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("Stop blocked")
	}
	st := gpio.StateForTest(drv)
	for id, lv := range st {
		if lv != gpio.Off {
			t.Fatalf("%s still %v after Stop", id, lv)
		}
	}

	deadline := time.Now().Add(time.Second)
	var runErr error
	for {
		runErr = e.RunItinerary(context.Background(), []Step{{StationID: "front-north", Duration: 30 * time.Millisecond}})
		if !errors.Is(runErr, ErrBusy) {
			break
		}
		if time.Now().After(deadline) {
			t.Fatal("still ErrBusy; running was not cleared before Record")
		}
		time.Sleep(5 * time.Millisecond)
	}
	if runErr != nil {
		t.Fatal(runErr)
	}
}
