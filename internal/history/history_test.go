package history

import (
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/store"
)

func TestHistoryPath(t *testing.T) {
	got := store.HistoryPath(filepath.Join("cfg", "config.json"))
	want := filepath.Join("cfg", "history.json")
	if got != want {
		t.Fatalf("got %s want %s", got, want)
	}
}

func recAt(program string, at time.Time) engine.RunRecord {
	return engine.RunRecord{
		ProgramID: program,
		Program:   program,
		Kind:      engine.KindManual,
		Stations:  []engine.StationRun{{StationID: "front-west", PlannedSec: 60, ActualSec: 60}},
		Start:     at,
		End:       at,
		Outcome:   engine.OutcomeCompleted,
	}
}

func TestAppendListNewestFirst(t *testing.T) {
	path := filepath.Join(t.TempDir(), "history.json")
	base := time.Date(2026, 9, 22, 8, 0, 0, 0, time.UTC)
	l, err := Open(path, func() time.Time { return base })
	if err != nil {
		t.Fatal(err)
	}
	l.settle = time.Millisecond
	t.Cleanup(l.Close)
	l.Append(recAt("first", base))
	l.Append(recAt("second", base.Add(time.Minute)))
	l.Append(recAt("third", base.Add(2*time.Minute)))
	got := l.List(10)
	if len(got) != 3 || got[0].Program != "third" || got[1].Program != "second" || got[2].Program != "first" {
		t.Fatalf("%+v", programs(got))
	}
	if got[0].Stations[0].StationID != "front-west" {
		t.Fatalf("stations %+v", got[0].Stations)
	}
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var doc diskFile
	if err := json.Unmarshal(b, &doc); err != nil {
		t.Fatal(err)
	}
	if len(doc.Entries) != 3 || doc.Entries[0].Program != "first" || doc.Entries[2].Program != "third" {
		t.Fatal("file must be oldest-first")
	}
}

func programs(es []Entry) []string {
	out := make([]string, len(es))
	for i, e := range es {
		out[i] = e.Program
	}
	return out
}

func TestCapMaxEntries(t *testing.T) {
	path := filepath.Join(t.TempDir(), "history.json")
	base := time.Date(2026, 9, 1, 0, 0, 0, 0, time.UTC)
	l, err := Open(path, func() time.Time { return base.Add(400 * time.Hour) })
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(l.Close)
	for i := 0; i < MaxEntries+5; i++ {
		at := base.Add(time.Duration(i) * time.Minute)
		r := recAt(itoa(i), at)
		r.ProgramID = itoa(i)
		l.Append(r)
	}
	got := l.List(0)
	if len(got) != MaxEntries {
		t.Fatalf("len %d", len(got))
	}
	if got[0].ProgramID != itoa(MaxEntries+4) {
		t.Fatalf("newest %s", got[0].ProgramID)
	}
	if got[len(got)-1].ProgramID != "5" {
		t.Fatalf("oldest kept %s", got[len(got)-1].ProgramID)
	}
	for _, e := range got {
		if e.ProgramID == "0" || e.ProgramID == "4" {
			t.Fatalf("dropped id still present: %s", e.ProgramID)
		}
	}
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	var b [16]byte
	i := len(b)
	for n > 0 {
		i--
		b[i] = byte('0' + n%10)
		n /= 10
	}
	return string(b[i:])
}

func TestMaxAgeOnAppendAndLoad(t *testing.T) {
	path := filepath.Join(t.TempDir(), "history.json")
	start := time.Date(2026, 1, 1, 12, 0, 0, 0, time.UTC)
	cur := start
	l, err := Open(path, func() time.Time { return cur })
	if err != nil {
		t.Fatal(err)
	}
	l.Append(recAt("old", start))
	cur = start.Add(MaxAge + time.Hour)
	l.Append(recAt("new", cur))
	got := l.List(10)
	if len(got) != 1 || got[0].Program != "new" {
		t.Fatalf("append prune: %+v", programs(got))
	}
	l.Close()

	old := start
	recent := cur
	raw := diskFile{Entries: []Entry{
		{ID: "old", Program: "old", ProgramID: "old", Kind: engine.KindManual, Outcome: engine.OutcomeCompleted, Stations: []Station{}, StartedAt: old, EndedAt: old},
		{ID: "new", Program: "new", ProgramID: "new", Kind: engine.KindManual, Outcome: engine.OutcomeCompleted, Stations: []Station{}, StartedAt: recent, EndedAt: recent},
	}}
	path2 := filepath.Join(t.TempDir(), "history.json")
	if err := store.AtomicWriteJSON(path2, raw); err != nil {
		t.Fatal(err)
	}
	l2, err := Open(path2, func() time.Time { return recent })
	if err != nil {
		t.Fatal(err)
	}
	defer l2.Close()
	got = l2.List(10)
	if len(got) != 1 || got[0].Program != "new" || got[0].ID != "new" {
		t.Fatalf("load prune: %+v", got)
	}
}

func TestRestartPersistence(t *testing.T) {
	path := filepath.Join(t.TempDir(), "history.json")
	at := time.Date(2026, 9, 22, 15, 4, 5, 0, time.UTC)
	l, err := Open(path, time.Now)
	if err != nil {
		t.Fatal(err)
	}
	l.Append(engine.RunRecord{
		ProgramID: "dawn",
		Program:   "Dawn",
		Kind:      engine.KindSchedule,
		Stations:  []engine.StationRun{{StationID: "front-west", PlannedSec: 240, ActualSec: 200}},
		Start:     at,
		End:       at.Add(4 * time.Minute),
		Outcome:   engine.OutcomeCompleted,
	})
	l.Append(recAt("later", at.Add(time.Hour)))
	want := l.List(10)
	l.Close()

	l2, err := Open(path, time.Now)
	if err != nil {
		t.Fatal(err)
	}
	defer l2.Close()
	got := l2.List(10)
	if len(got) != len(want) {
		t.Fatalf("len %d want %d", len(got), len(want))
	}
	for i := range want {
		if got[i].ID != want[i].ID || got[i].Program != want[i].Program || got[i].ProgramID != want[i].ProgramID ||
			got[i].Kind != want[i].Kind || got[i].Outcome != want[i].Outcome {
			t.Fatalf("%d got %+v want %+v", i, got[i], want[i])
		}
		if !got[i].StartedAt.Equal(want[i].StartedAt) || !got[i].EndedAt.Equal(want[i].EndedAt) {
			t.Fatalf("%d times %s/%s vs %s/%s", i, got[i].StartedAt, got[i].EndedAt, want[i].StartedAt, want[i].EndedAt)
		}
		if len(got[i].Stations) != len(want[i].Stations) {
			t.Fatalf("stations %d", i)
		}
		for j := range want[i].Stations {
			if got[i].Stations[j] != want[i].Stations[j] {
				t.Fatalf("station %+v vs %+v", got[i].Stations[j], want[i].Stations[j])
			}
		}
	}
}

func TestMissingFile(t *testing.T) {
	path := filepath.Join(t.TempDir(), "missing-dir", "history.json")
	l, err := Open(path, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	if got := l.List(10); len(got) != 0 {
		t.Fatalf("%+v", got)
	}
}

func TestCorruptFile(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "history.json")
	if err := os.WriteFile(path, []byte("{"), 0o644); err != nil {
		t.Fatal(err)
	}
	l, err := Open(path, nil)
	if err != nil {
		t.Fatalf("corrupt file must not error: %v", err)
	}
	defer l.Close()
	if got := l.List(5); len(got) != 0 {
		t.Fatalf("%+v", got)
	}
	if _, err := os.Stat(path); !os.IsNotExist(err) {
		t.Fatalf("original still present: %v", err)
	}
	matches, err := filepath.Glob(path + ".corrupt-*")
	if err != nil {
		t.Fatal(err)
	}
	if len(matches) != 1 {
		t.Fatalf("quarantine files: %v", matches)
	}
}

func TestRecordNonBlockingWhenBufferFull(t *testing.T) {
	path := filepath.Join(t.TempDir(), "history.json")
	l, err := Open(path, time.Now)
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	// Writer is not started, so the buffer fills and stays full.
	for i := 0; i < recordBuf; i++ {
		l.Record(engine.RunRecord{Program: "fill", Outcome: engine.OutcomeCompleted})
	}
	done := make(chan struct{})
	go func() {
		l.Record(engine.RunRecord{Program: "drop", Outcome: engine.OutcomeCompleted})
		close(done)
	}()
	select {
	case <-done:
	case <-time.After(time.Second):
		t.Fatal("Record blocked with a full buffer")
	}
}

func TestRecordPersistsViaWriter(t *testing.T) {
	path := filepath.Join(t.TempDir(), "history.json")
	at := time.Date(2026, 9, 22, 8, 23, 0, 0, time.UTC)
	l, err := Open(path, func() time.Time { return at })
	if err != nil {
		t.Fatal(err)
	}
	l.settle = 15 * time.Millisecond
	l.Start(context.Background())
	l.Record(engine.RunRecord{
		ProgramID: "dawn",
		Program:   "Dawn",
		Kind:      engine.KindSchedule,
		Start:     at,
		End:       at.Add(time.Minute),
		Outcome:   engine.OutcomeStopped,
		Stations:  []engine.StationRun{{StationID: "front-north", PlannedSec: 60, ActualSec: 12}},
	})
	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if len(l.List(10)) == 1 {
			break
		}
		time.Sleep(5 * time.Millisecond)
	}
	if len(l.List(10)) != 1 {
		t.Fatal("writer did not append")
	}
	l.Close()
	l2, err := Open(path, time.Now)
	if err != nil {
		t.Fatal(err)
	}
	defer l2.Close()
	got := l2.List(10)
	if len(got) != 1 || got[0].Program != "Dawn" || got[0].Outcome != engine.OutcomeStopped {
		t.Fatalf("%+v", got)
	}
	if got[0].Stations[0].ActualSec != 12 {
		t.Fatalf("%+v", got[0].Stations)
	}
}

func TestOpenRemovesStaleTemps(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "history.json")
	if err := os.WriteFile(path, []byte("{\"entries\":[]}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	stale := filepath.Join(dir, ".history.json.tmp-interrupted")
	if err := os.WriteFile(stale, []byte("{"), 0o644); err != nil {
		t.Fatal(err)
	}
	other := filepath.Join(dir, "pause.json")
	if err := os.WriteFile(other, []byte("{}"), 0o644); err != nil {
		t.Fatal(err)
	}
	l, err := Open(path, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	if _, err := os.Stat(stale); !os.IsNotExist(err) {
		t.Fatalf("stale temp still present: %v", err)
	}
	if _, err := os.Stat(path); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(other); err != nil {
		t.Fatal(err)
	}
}

func TestCloseWithoutStartIsImmediate(t *testing.T) {
	l, err := Open(filepath.Join(t.TempDir(), "history.json"), time.Now)
	if err != nil {
		t.Fatal(err)
	}
	start := time.Now()
	l.Close()
	if elapsed := time.Since(start); elapsed > 200*time.Millisecond {
		t.Fatalf("Close without Start took %s", elapsed)
	}
}

func TestCloseReturnsWhileWriteInFlight(t *testing.T) {
	l, err := Open(filepath.Join(t.TempDir(), "history.json"), time.Now)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(l.Close)
	entered := make(chan struct{})
	var once sync.Once
	l.SetWriteFuncForTest(func(string, any) error {
		once.Do(func() { close(entered) })
		time.Sleep(5 * time.Second)
		return nil
	})
	l.settle = time.Millisecond
	l.Start(context.Background())
	l.Record(recAt("slow", time.Now()))
	select {
	case <-entered:
	case <-time.After(2 * time.Second):
		t.Fatal("slow write did not start")
	}
	start := time.Now()
	l.Close()
	if elapsed := time.Since(start); elapsed > 2500*time.Millisecond {
		t.Fatalf("Close took %s, want ≤ 2.5s", elapsed)
	}
}

func TestListDuringSlowWrite(t *testing.T) {
	l, err := Open(filepath.Join(t.TempDir(), "history.json"), time.Now)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(l.Close)
	entered := make(chan struct{})
	var once sync.Once
	l.SetWriteFuncForTest(func(string, any) error {
		once.Do(func() { close(entered) })
		time.Sleep(3 * time.Second)
		return nil
	})
	l.settle = time.Millisecond
	l.Start(context.Background())
	l.Record(recAt("visible", time.Now()))
	select {
	case <-entered:
	case <-time.After(2 * time.Second):
		t.Fatal("slow write did not start")
	}
	start := time.Now()
	got := l.List(10)
	if elapsed := time.Since(start); elapsed >= 100*time.Millisecond {
		t.Fatalf("List took %s", elapsed)
	}
	if len(got) != 1 || got[0].Program != "visible" {
		t.Fatalf("%+v", programs(got))
	}
}

func TestSlowFirstWriteKeepsNewestSnapshot(t *testing.T) {
	path := filepath.Join(t.TempDir(), "history.json")
	base := time.Date(2026, 9, 22, 8, 0, 0, 0, time.UTC)
	l, err := Open(path, func() time.Time { return base })
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	var n atomic.Int32
	l.SetWriteFuncForTest(func(p string, v any) error {
		if n.Add(1) == 1 {
			time.Sleep(300 * time.Millisecond)
		}
		return store.AtomicWriteJSON(p, v)
	})
	var wg sync.WaitGroup
	wg.Add(2)
	go func() {
		defer wg.Done()
		l.Append(recAt("first", base))
	}()
	go func() {
		defer wg.Done()
		l.Append(recAt("second", base.Add(time.Minute)))
	}()
	wg.Wait()
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	var doc diskFile
	if err := json.Unmarshal(b, &doc); err != nil {
		t.Fatal(err)
	}
	if len(doc.Entries) != 2 {
		t.Fatalf("file len %d %+v", len(doc.Entries), programs(doc.Entries))
	}
	got := map[string]bool{}
	for _, e := range doc.Entries {
		got[e.Program] = true
	}
	if !got["first"] || !got["second"] {
		t.Fatalf("file missing an entry: %+v", programs(doc.Entries))
	}
	listed := l.List(10)
	if len(listed) != 2 {
		t.Fatalf("memory %+v", programs(listed))
	}
}
