package rain

import (
	"context"
	"fmt"
	"log"
	"strings"
	"sync"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/store"
)

// Status is the public rain-feed health. It never includes a gauge id or URL.
type Status struct {
	Enabled     bool
	Unavailable bool
	LastError   string
	LastOK      *time.Time
	Since       *time.Time
	LastTotal   float64
	HaveTotal   bool
}

const (
	badFetch       = "fetch"
	badStale       = "stale"
	badImplausible = "implausible"
	badEmpty       = "empty"
	badOther       = "other"
)

// negRowKey is one negative increment the poller has already logged.
// unix is the sample instant; cents is the increment in hundredths.
type negRowKey struct {
	unix  int64
	cents int64
}

// Poller fetches on an interval and applies Decide to the engine.
// It does not call engine.Stop. A cancelled ctx ends Loop.
//
// mu guards engine edits, status, negLogged, and the snapshot version. Disk
// writes take writeMu only, after mu is released, so Status never waits on fsync.
// Do's callback must not call Poll, Do, ManualPause, or ManualClear.
type Poller struct {
	Source Source
	Eng    *engine.Engine
	Path   string
	Cfg    Config
	Now    func() time.Time
	Log    *log.Logger

	// save persists one snapshot. Nil means store.SavePause.
	// Called outside mu. Tests replace it via SetSaveFuncForTest.
	save func(path string, ps store.PauseState) error

	mu     sync.Mutex
	st     Status
	badCat string
	seq    uint64 // bumped under mu for each snapshot

	// lastGood is the newest fetch Decide treated as usable (not unavailable).
	// DailyRain copies it and skips negative increments.
	lastGood []Sample
	haveGood bool

	// negLogged remembers in-window negative rows already logged so each row
	// is reported once. Entries outside the trigger window are pruned.
	negLogged map[negRowKey]struct{}

	// writeMu serializes pause.json writes. Status never takes it.
	writeMu sync.Mutex
	written uint64 // highest version successfully persisted; writeMu only
}

// Start loads rain config beside configPath. A missing or disabled file
// returns nil, nil. A malformed file returns an error and does not poll.
// On success it polls once in the background loop (including at startup)
// until ctx is cancelled. Logs omit the gauge id and URL.
func Start(ctx context.Context, eng *engine.Engine, configPath string) (*Poller, error) {
	cfg, err := Load(configPath)
	if err != nil {
		return nil, err
	}
	if !cfg.Enabled {
		return nil, nil
	}
	loc, err := time.LoadLocation(phoenixTZ)
	if err != nil {
		loc = time.FixedZone("MST", -7*3600)
	}
	p := &Poller{
		Source: NewFCDMC(cfg, loc),
		Eng:    eng,
		Path:   configPath,
		Cfg:    cfg,
		Log:    log.Default(),
	}
	log.Printf("rain: polling every %s", cfg.PollInterval())
	go p.Loop(ctx)
	return p, nil
}

// Loop polls immediately, then on each interval, until ctx is cancelled.
func (p *Poller) Loop(ctx context.Context) {
	if p == nil {
		return
	}
	p.Poll(ctx)
	iv := p.Cfg.PollInterval()
	t := time.NewTicker(iv)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			p.Poll(ctx)
		}
	}
}

// Do runs fn while holding the poller lock.
// fn must not call Poll, Do, ManualPause, or ManualClear.
func (p *Poller) Do(fn func()) {
	if p == nil || fn == nil {
		if fn != nil {
			fn()
		}
		return
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	fn()
}

// SetSaveFuncForTest installs fn as the pause persistence function.
// Nil restores store.SavePause. The function runs outside mu.
func (p *Poller) SetSaveFuncForTest(fn func(path string, ps store.PauseState) error) {
	if p == nil {
		return
	}
	p.mu.Lock()
	p.save = fn
	p.mu.Unlock()
}

// DailyRain sums last-good incremental samples by local calendar date.
// ok is false when the poller is nil or no usable fetch has been cached.
// The map is a copy. Negative increments are skipped. loc nil uses America/Phoenix.
func (p *Poller) DailyRain(loc *time.Location) (map[string]float64, bool) {
	if p == nil {
		return nil, false
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	if !p.haveGood {
		return nil, false
	}
	if loc == nil {
		var err error
		loc, err = time.LoadLocation(phoenixTZ)
		if err != nil {
			loc = time.FixedZone("MST", -7*3600)
		}
	}
	out := make(map[string]float64, len(p.lastGood))
	for _, s := range p.lastGood {
		if s.Inches < 0 {
			continue
		}
		day := s.Time.In(loc).Format("2006-01-02")
		out[day] += s.Inches
	}
	return out, true
}

// Status returns a copy of the feed health. Enabled is true when a poller exists.
// It takes only mu, never writeMu, so an in-flight pause.json write does not block it.
func (p *Poller) Status() Status {
	if p == nil {
		return Status{}
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	s := p.st
	s.Enabled = true
	if s.LastOK != nil {
		t := *s.LastOK
		s.LastOK = &t
	}
	if s.Since != nil {
		t := *s.Since
		s.Since = &t
	}
	return s
}

// ManualPause arms a user pause and persists it after releasing mu.
// The snapshot is ordered with Poll so a later Resume is not overwritten by
// an earlier decision. The caller waits for the disk write; Status does not.
func (p *Poller) ManualPause(until *time.Time, reason string, now time.Time) error {
	if p == nil || p.Eng == nil {
		return fmt.Errorf("rain: no poller")
	}
	p.mu.Lock()
	ps := manualPauseState(p.Eng, until, reason, now)
	ver, path, save := p.bumpLocked()
	p.mu.Unlock()
	return p.persist(path, ps, ver, save)
}

// ManualClear resumes watering, records the rain event, and persists outside mu.
func (p *Poller) ManualClear(now time.Time) error {
	if p == nil || p.Eng == nil {
		return fmt.Errorf("rain: no poller")
	}
	p.mu.Lock()
	ps := manualClearState(p.Eng, now)
	ver, path, save := p.bumpLocked()
	p.mu.Unlock()
	return p.persist(path, ps, ver, save)
}

// SyncExpiredPause writes an engine-expired hold back to disk when no pause
// snapshot is already being saved. It returns immediately if a write is in
// progress so status does not wait on that fsync.
func (p *Poller) SyncExpiredPause(now time.Time) {
	if p == nil || p.Path == "" {
		return
	}
	if !p.writeMu.TryLock() {
		return
	}
	defer p.writeMu.Unlock()
	ps, err := store.LoadPause(p.Path, now)
	if err != nil || !ps.Active {
		return
	}
	if p.Eng != nil && p.Eng.IsPaused(now) {
		return
	}
	_ = store.SavePause(p.Path, ps.WithoutActive())
}

// Poll fetches once and applies the decision. Fetch errors and unavailable
// samples do not pause, extend, or clear. A cancelled ctx does not mark
// the feed unavailable. pause.json is written after mu is released.
func (p *Poller) Poll(ctx context.Context) {
	if p == nil || p.Source == nil || p.Eng == nil {
		return
	}
	if ctx.Err() != nil {
		return
	}
	samples, fetchErr := p.Source.Fetch(ctx)
	if ctx.Err() != nil {
		return
	}

	var (
		ps       store.PauseState
		ver      uint64
		doSave   bool
		loadFile bool
		logs     []string
		path     string
		save     func(string, store.PauseState) error
	)

	p.mu.Lock()
	now := p.now()
	if fetchErr != nil {
		msg := redactErr(fetchErr, p.Cfg.GaugeID)
		if line := p.observeBad(msg, badFetch, now); line != "" {
			logs = append(logs, line)
		}
	} else {
		logs = p.noteNegativeRows(samples, now)
		detail := p.Eng.PauseRaw()
		action := Decide(samples, now, p.Cfg, viewFrom(detail), memoryFrom(detail))
		if action.Kind != ActionUnavailable {
			p.lastGood = cloneSamples(samples)
			p.haveGood = true
		}
		var applyLogs []string
		ps, ver, doSave, loadFile, applyLogs = p.applyLocked(now, action)
		logs = append(logs, applyLogs...)
	}
	path = p.Path
	save = p.saveFuncLocked()
	p.mu.Unlock()

	for _, line := range logs {
		p.logf("%s", line)
	}
	if doSave {
		if err := p.persist(path, ps, ver, save); err != nil {
			p.logf("rain: save pause: %v", err)
		}
		return
	}
	if loadFile && path != "" {
		p.readPauseFile(path, now)
	}
}

func (p *Poller) now() time.Time {
	if p.Now != nil {
		return p.Now()
	}
	return time.Now()
}

func (p *Poller) logf(format string, args ...any) {
	lg := p.Log
	if lg == nil {
		lg = log.Default()
	}
	lg.Printf(format, args...)
}

func (p *Poller) saveFuncLocked() func(string, store.PauseState) error {
	if p.save != nil {
		return p.save
	}
	return store.SavePause
}

func (p *Poller) bumpLocked() (ver uint64, path string, save func(string, store.PauseState) error) {
	p.seq++
	return p.seq, p.Path, p.saveFuncLocked()
}

func (p *Poller) markBad(msg string) {
	p.st.Enabled = true
	p.st.Unavailable = true
	p.st.LastError = msg
}

func (p *Poller) noteFresh(now time.Time, total float64) {
	t := now.UTC()
	p.st.Enabled = true
	p.st.Unavailable = false
	p.st.LastError = ""
	p.st.LastOK = &t
	p.st.Since = nil
	p.st.LastTotal = RoundInches(total)
	p.st.HaveTotal = true
}

// noteNegativeRows returns one log line the first time each in-window
// negative row is seen. The line has no gauge id, URL, or host. Rows outside
// the trigger window are not logged. Keys outside the window are dropped.
func (p *Poller) noteNegativeRows(samples []Sample, now time.Time) []string {
	cfg := p.Cfg.normalized()
	p.pruneNegLogged(now, cfg)
	var lines []string
	for _, s := range samples {
		if s.Inches >= 0 || !inWindow(s, now, cfg) {
			continue
		}
		if p.negLogged == nil {
			p.negLogged = make(map[negRowKey]struct{})
		}
		key := negRowKey{unix: s.Time.UnixNano(), cents: hundredths(s.Inches)}
		if _, seen := p.negLogged[key]; seen {
			continue
		}
		p.negLogged[key] = struct{}{}
		lines = append(lines, fmt.Sprintf(
			"rain: ignoring feed: negative increment at %s (%.2f in)",
			s.Time.Format("15:04"), s.Inches,
		))
	}
	return lines
}

// pruneNegLogged drops remembered rows that are no longer inside the window.
func (p *Poller) pruneNegLogged(now time.Time, cfg Config) {
	for k := range p.negLogged {
		t := time.Unix(0, k.unix).UTC()
		if !inWindow(Sample{Time: t}, now, cfg) {
			delete(p.negLogged, k)
		}
	}
}

// observeBad records unavailable. It returns a log line only when the feed
// enters unavailable, or the reason category changes.
func (p *Poller) observeBad(msg, cat string, now time.Time) string {
	changed := !p.st.Unavailable || p.badCat != cat
	if !p.st.Unavailable {
		t := now.UTC()
		p.st.Since = &t
	}
	p.markBad(msg)
	p.badCat = cat
	if !changed {
		return ""
	}
	if cat == badFetch {
		return "rain: fetch failed: " + msg
	}
	return "rain: " + msg
}

// observeGood records a fresh read. It returns a log line only on recovery.
func (p *Poller) observeGood(now time.Time, total float64) string {
	wasBad := p.st.Unavailable
	p.noteFresh(now, total)
	p.badCat = ""
	if !wasBad {
		return ""
	}
	return "rain: data available again"
}

func (p *Poller) applyLocked(now time.Time, action Action) (ps store.PauseState, ver uint64, doSave, loadFile bool, logs []string) {
	switch action.Kind {
	case ActionUnavailable:
		msg := action.Error
		if msg == "" {
			msg = "rain data unavailable"
		}
		if line := p.observeBad(msg, badCategory(msg), now); line != "" {
			logs = append(logs, line)
		}
		return
	case ActionClear:
		if line := p.observeGood(now, action.Total); line != "" {
			logs = append(logs, line)
		}
		if !action.LastRain.IsZero() {
			p.Eng.NoteEventRain(action.LastRain)
		}
		p.Eng.ClearPause()
		ps = pauseStateFromEngine(p.Eng, now)
		ver, _, _ = p.bumpLocked()
		doSave = true
		logs = append(logs, "rain: auto pause ended")
		return
	case ActionPause:
		if line := p.observeGood(now, action.Total); line != "" {
			logs = append(logs, line)
		}
		until := action.Until
		event := action.EventAt
		last := action.LastRain
		p.Eng.SetPauseMeta(&until, "rain", engine.PauseMeta{
			Source:      engine.PauseSourceAuto,
			RainInches:  RoundInches(action.Inches),
			RainEventAt: &event,
			LastRainAt:  &last,
		})
		ps = pauseStateFromEngine(p.Eng, now)
		ver, _, _ = p.bumpLocked()
		doSave = true
		logs = append(logs, "rain: auto pause")
		return
	default:
		if line := p.observeGood(now, action.Total); line != "" {
			logs = append(logs, line)
		}
		loadFile = !p.Eng.PauseRaw().Paused
		return
	}
}

func badCategory(msg string) string {
	switch {
	case strings.Contains(msg, "implausible"):
		return badImplausible
	case strings.Contains(msg, "stale"):
		return badStale
	case strings.Contains(msg, "no samples"):
		return badEmpty
	default:
		return badOther
	}
}

// persist writes ps unless a newer snapshot is already on disk.
// writeMu is held across the write so an older snapshot cannot finish last.
func (p *Poller) persist(path string, ps store.PauseState, ver uint64, save func(string, store.PauseState) error) error {
	if save == nil {
		save = store.SavePause
	}
	p.writeMu.Lock()
	defer p.writeMu.Unlock()
	if ver <= p.written {
		return nil
	}
	if err := save(path, ps); err != nil {
		return err
	}
	p.written = ver
	return nil
}

func (p *Poller) readPauseFile(path string, now time.Time) {
	p.writeMu.Lock()
	_, err := store.LoadPause(path, now)
	p.writeMu.Unlock()
	if err != nil {
		p.logf("rain: pause load: %v", err)
	}
}

func viewFrom(d engine.PauseSnap) PauseView {
	return PauseView{
		Active:   d.Paused,
		Until:    d.Until,
		Reason:   d.Reason,
		Source:   d.Source,
		Inches:   d.RainInches,
		EventAt:  d.RainEventAt,
		LastRain: d.LastRainAt,
	}
}

func memoryFrom(d engine.PauseSnap) Memory {
	return Memory{ClearedAt: d.RainClearedAt, LastEventRain: d.EventLastRain}
}

func cloneSamples(in []Sample) []Sample {
	if in == nil {
		return []Sample{}
	}
	out := make([]Sample, len(in))
	copy(out, in)
	return out
}
