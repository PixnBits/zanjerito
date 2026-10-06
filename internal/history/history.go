// Package history is the household run log (D10).
//
// On disk, history.json (beside the config file) is:
//
//	{"entries":[ ... ]}
//
// Entries are stored oldest-first. List returns a newest-first copy.
// A missing file starts empty. A file that cannot be decoded is renamed to
// history.json.corrupt-<unix-nano> and the log starts empty (the process does
// not crash and does not delete the bytes). If that rename fails, the next
// successful append overwrites the path.
//
// Prune drops entries whose ended_at (or started_at when ended_at is zero)
// is strictly older than MaxAge, then keeps the newest MaxEntries.
// Open prunes in memory; the file shrinks on the next append.
// Open also removes leftover atomic-write temps in the same directory
// (".<file>.tmp-*", the store.AtomicWriteJSON pattern; best effort).
//
// Open does not start the writer. Call Start, then Close to flush and stop.
// Record never blocks: it sends on a buffered channel and drops when full.
//
// Append copies entries under the log lock and persists that snapshot outside
// the lock, serialized by a separate write mutex. List only takes the log lock,
// so it stays responsive while a write is in flight. Close starts its 2s bound
// immediately and does not wait out a stuck write.
package history

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"sync"
	"sync/atomic"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/store"
)

const (
	MaxEntries = 200
	MaxAge     = 60 * 24 * time.Hour

	recordBuf     = 64
	defaultSettle = 400 * time.Millisecond
)

// life is the writer lifecycle. Own block so iota starts at 0, matching
// atomic.Int32's zero value (not started).
const (
	lifeIdle int32 = iota
	lifeRunning
	lifeClosed
)

// Station is one step in the stored and served log.
type Station struct {
	StationID  string `json:"station_id"`
	PlannedSec int    `json:"planned_sec"`
	ActualSec  int    `json:"actual_sec"`
}

// Entry is one stored run. Times are absolute instants.
type Entry struct {
	ID        string    `json:"id"`
	ProgramID string    `json:"program_id"`
	Program   string    `json:"program"`
	Kind      string    `json:"kind"`
	Stations  []Station `json:"stations"`
	StartedAt time.Time `json:"started_at"`
	EndedAt   time.Time `json:"ended_at"`
	Outcome   string    `json:"outcome"`
	Error     string    `json:"error,omitempty"`
	Reason    string    `json:"reason,omitempty"`
}

type diskFile struct {
	Entries []Entry `json:"entries"`
}

// Log is the in-memory run log plus its writer.
type Log struct {
	mu      sync.Mutex
	path    string
	now     func() time.Time
	entries []Entry // oldest-first
	seq     uint64  // bumped on each append; also the snapshot version

	// writeMu serializes disk writes. List and Close never acquire it.
	writeMu sync.Mutex
	written uint64 // highest seq successfully persisted; writeMu only
	// write persists one snapshot. Nil means store.AtomicWriteJSON.
	write func(path string, v any) error

	ch   chan engine.RunRecord
	stop chan struct{}
	done chan struct{}

	// life is idle, running, or closed-without-start. Touched only via atomics
	// so Close can bound itself without taking mu or writeMu.
	life atomic.Int32

	startOnce sync.Once
	closeOnce sync.Once

	// settle is how long Close waits for a record that has not been sent yet
	// (the run goroutine emits after Stop returns). Zero means defaultSettle.
	settle time.Duration
}

var _ engine.RunRecorder = (*Log)(nil)

// Open loads path. now may be nil (time.Now).
// A missing file yields an empty log. A corrupt file is quarantined and yields
// an empty log with a nil error. Other read errors return an empty log and the error.
func Open(path string, now func() time.Time) (*Log, error) {
	if now == nil {
		now = time.Now
	}
	l := &Log{
		path:    path,
		now:     now,
		entries: []Entry{},
		ch:      make(chan engine.RunRecord, recordBuf),
		stop:    make(chan struct{}),
		done:    make(chan struct{}),
		write:   store.AtomicWriteJSON,
	}
	removeStaleTemps(path)
	b, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return l, nil
		}
		log.Printf("history: read %s: %v (starting empty)", path, err)
		return l, err
	}
	var doc diskFile
	if err := json.Unmarshal(b, &doc); err != nil {
		log.Printf("history: corrupt %s: %v (starting empty; renaming aside)", path, err)
		renameCorrupt(path, now())
		return l, nil
	}
	if doc.Entries != nil {
		l.entries = doc.Entries
	}
	l.prune(l.clock())
	return l, nil
}

// removeStaleTemps drops temps left when a write was interrupted after
// CreateTemp and before rename. Matches store.AtomicWriteJSON:
// "." + base + ".tmp-*". Best effort.
func removeStaleTemps(path string) {
	dir := filepath.Dir(path)
	pattern := filepath.Join(dir, "."+filepath.Base(path)+".tmp-*")
	matches, err := filepath.Glob(pattern)
	if err != nil {
		log.Printf("history: temp glob %s: %v", pattern, err)
		return
	}
	for _, name := range matches {
		if err := os.Remove(name); err != nil && !os.IsNotExist(err) {
			log.Printf("history: remove stale temp %s: %v", name, err)
		}
	}
}

func renameCorrupt(path string, now time.Time) {
	dest := fmt.Sprintf("%s.corrupt-%d", path, now.UnixNano())
	if err := os.Rename(path, dest); err != nil {
		log.Printf("history: could not rename corrupt file %s: %v (next save overwrites)", path, err)
	}
}

func (l *Log) clock() time.Time {
	if l == nil || l.now == nil {
		return time.Now()
	}
	return l.now()
}

// SetWriteFuncForTest installs fn as the persistence function used by Append.
// It must be called before Start. nil restores store.AtomicWriteJSON.
// Tests use it to simulate a slow or failing disk without changing List or Close.
func (l *Log) SetWriteFuncForTest(fn func(path string, v any) error) {
	if l == nil {
		return
	}
	if fn == nil {
		fn = store.AtomicWriteJSON
	}
	l.mu.Lock()
	l.write = fn
	l.mu.Unlock()
}

// Start launches the single writer. It is safe to call once.
// Cancel ctx or call Close to stop. Main passes a context that outlives Stop
// so Close can flush the final record.
func (l *Log) Start(ctx context.Context) {
	if l == nil {
		return
	}
	if ctx == nil {
		ctx = context.Background()
	}
	l.startOnce.Do(func() {
		if !l.life.CompareAndSwap(lifeIdle, lifeRunning) {
			return
		}
		go l.loop(ctx)
	})
}

// Close stops the writer and flushes queued records. Safe to call once.
// If Start was not called, Close returns immediately.
// The wait is bounded by 2s from entry, including a short grace for a record
// emitted just after engine.Stop returns. A disk write still in flight does
// not extend that bound; it may be cut off on process exit.
func (l *Log) Close() {
	if l == nil {
		return
	}
	l.closeOnce.Do(func() {
		timer := time.NewTimer(2 * time.Second)
		defer timer.Stop()
		if l.life.CompareAndSwap(lifeIdle, lifeClosed) {
			return
		}
		close(l.stop)
		select {
		case <-l.done:
		case <-timer.C:
			log.Printf("history: close timed out after 2s")
		}
	})
}

// Record implements engine.RunRecorder. It never blocks.
// If the buffer is full the record is dropped and a warning is logged.
func (l *Log) Record(rec engine.RunRecord) {
	if l == nil || l.ch == nil {
		return
	}
	select {
	case l.ch <- rec:
	default:
		log.Printf("history: dropping run record (buffer full)")
	}
}

func (l *Log) loop(ctx context.Context) {
	defer close(l.done)
	for {
		select {
		case <-ctx.Done():
			l.finish()
			return
		case <-l.stop:
			l.finish()
			return
		case rec := <-l.ch:
			l.Append(rec)
		}
	}
}

func (l *Log) finish() {
	l.drain()
	wait := l.settle
	if wait <= 0 {
		wait = defaultSettle
	}
	deadline := time.Now().Add(2 * time.Second)
	timer := time.NewTimer(wait)
	defer timer.Stop()
	for {
		if !time.Now().Before(deadline) {
			l.drain()
			return
		}
		select {
		case rec := <-l.ch:
			l.Append(rec)
			l.drain()
			if !timer.Stop() {
				select {
				case <-timer.C:
				default:
				}
			}
			remain := time.Until(deadline)
			if remain <= 0 {
				return
			}
			next := 50 * time.Millisecond
			if wait < next {
				next = wait
			}
			if remain < next {
				next = remain
			}
			timer.Reset(next)
		case <-timer.C:
			l.drain()
			return
		}
	}
}

func (l *Log) drain() {
	for {
		select {
		case rec := <-l.ch:
			l.Append(rec)
		default:
			return
		}
	}
}

// Append adds rec, prunes, and persists. It may do file I/O outside the log lock.
// Call it from the writer goroutine or tests, not under the engine lock.
func (l *Log) Append(rec engine.RunRecord) {
	l.mu.Lock()
	start := rec.Start
	if start.IsZero() {
		start = l.clock()
	}
	end := rec.End
	if end.IsZero() {
		end = start
	}
	stations := make([]Station, 0, len(rec.Stations))
	for _, s := range rec.Stations {
		stations = append(stations, Station{
			StationID:  s.StationID,
			PlannedSec: s.PlannedSec,
			ActualSec:  s.ActualSec,
		})
	}
	l.seq++
	ver := l.seq
	l.entries = append(l.entries, Entry{
		ID:        fmt.Sprintf("%d-%d", start.UnixNano(), l.seq),
		ProgramID: rec.ProgramID,
		Program:   rec.Program,
		Kind:      rec.Kind,
		Stations:  stations,
		StartedAt: start,
		EndedAt:   end,
		Outcome:   rec.Outcome,
		Error:     rec.Error,
		Reason:    rec.Reason,
	})
	l.prune(l.clock())
	snap := cloneEntries(l.entries)
	path := l.path
	write := l.write
	l.mu.Unlock()

	l.persist(path, snap, ver, write)
}

func cloneEntries(in []Entry) []Entry {
	out := make([]Entry, len(in))
	for i, e := range in {
		if e.Stations != nil {
			e.Stations = append([]Station(nil), e.Stations...)
		}
		out[i] = e
	}
	return out
}

func (l *Log) persist(path string, snap []Entry, ver uint64, write func(string, any) error) {
	if write == nil {
		write = store.AtomicWriteJSON
	}
	l.writeMu.Lock()
	defer l.writeMu.Unlock()
	// A newer snapshot is already on disk. Writing this one would go backwards.
	if ver <= l.written {
		return
	}
	if err := write(path, diskFile{Entries: snap}); err != nil {
		log.Printf("history: write %s: %v", path, err)
		return
	}
	l.written = ver
}

func (l *Log) prune(now time.Time) {
	cutoff := now.Add(-MaxAge)
	kept := make([]Entry, 0, len(l.entries))
	for _, e := range l.entries {
		ts := e.EndedAt
		if ts.IsZero() {
			ts = e.StartedAt
		}
		if ts.Before(cutoff) {
			continue
		}
		kept = append(kept, e)
	}
	if len(kept) > MaxEntries {
		kept = append([]Entry(nil), kept[len(kept)-MaxEntries:]...)
	}
	l.entries = kept
}

// List returns up to limit entries, newest first. limit <= 0 returns all.
// The slice and each Stations slice are copies.
// List takes only the log lock, never the write mutex, so a slow disk write
// does not delay readers.
func (l *Log) List(limit int) []Entry {
	if l == nil {
		return []Entry{}
	}
	l.mu.Lock()
	defer l.mu.Unlock()
	n := len(l.entries)
	if limit > 0 && limit < n {
		n = limit
	}
	out := make([]Entry, 0, n)
	for i := len(l.entries) - 1; i >= 0 && len(out) < n; i-- {
		e := l.entries[i]
		if e.Stations == nil {
			e.Stations = []Station{}
		} else {
			e.Stations = append([]Station(nil), e.Stations...)
		}
		out = append(out, e)
	}
	return out
}
