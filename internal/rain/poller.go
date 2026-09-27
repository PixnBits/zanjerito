package rain

import (
	"context"
	"log"
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
	LastTotal   float64
	HaveTotal   bool
}

// Poller fetches on an interval and applies Decide to the engine.
// It does not call engine.Stop. A cancelled ctx ends Loop.
// Do serializes manual pause edits with Poll so a clear cannot be overwritten
// by an in-flight decision. Do's callback must not call Poll or Do.
type Poller struct {
	Source Source
	Eng    *engine.Engine
	Path   string
	Cfg    Config
	Now    func() time.Time
	Log    *log.Logger

	mu sync.Mutex
	st Status
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

// Status returns a copy of the feed health. Enabled is true when a poller exists.
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
	return s
}

// Poll fetches once and applies the decision. Fetch errors and stale samples
// do not pause or clear. A cancelled ctx does not mark the feed unavailable.
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
	p.mu.Lock()
	defer p.mu.Unlock()
	now := p.now()
	if fetchErr != nil {
		msg := redact(fetchErr.Error(), p.Cfg.GaugeID)
		p.markBad(msg)
		p.logf("rain: fetch failed: %s", msg)
		return
	}
	detail := p.Eng.PauseRaw()
	action := Decide(samples, now, p.Cfg, viewFrom(detail), memoryFrom(detail))
	p.apply(now, action)
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
	p.st.LastTotal = total
	p.st.HaveTotal = true
}

func (p *Poller) apply(now time.Time, action Action) {
	switch action.Kind {
	case ActionUnavailable:
		msg := action.Error
		if msg == "" {
			msg = "rain data unavailable"
		}
		p.markBad(msg)
		p.logf("rain: %s", msg)
		return
	case ActionClear:
		p.noteFresh(now, action.Total)
		if !action.LastRain.IsZero() {
			p.Eng.NoteEventRain(action.LastRain)
		}
		p.Eng.ClearPause()
		if err := p.saveFromEngine(now); err != nil {
			p.logf("rain: save pause: %v", err)
		}
		p.logf("rain: auto pause ended")
		return
	case ActionPause:
		p.noteFresh(now, action.Total)
		until := action.Until
		event := action.EventAt
		last := action.LastRain
		p.Eng.SetPauseMeta(&until, "rain", engine.PauseMeta{
			Source:      engine.PauseSourceAuto,
			RainInches:  action.Inches,
			RainEventAt: &event,
			LastRainAt:  &last,
		})
		if err := p.saveFromEngine(now); err != nil {
			p.logf("rain: save pause: %v", err)
		}
		p.logf("rain: auto pause")
		return
	default:
		p.noteFresh(now, action.Total)
		detail := p.Eng.PauseRaw()
		if !detail.Paused {
			if _, err := store.LoadPause(p.Path, now); err != nil {
				p.logf("rain: pause load: %v", err)
			}
		}
	}
}

func (p *Poller) saveFromEngine(now time.Time) error {
	d := p.Eng.PauseDetail(now)
	ps := store.PauseState{RainClearedAt: d.RainClearedAt}
	if d.Paused {
		ps.Active = true
		ps.Until = d.Until
		ps.Reason = d.Reason
		ps.Source = d.Source
		if ps.Source == "" {
			ps.Source = engine.PauseSourceManual
		}
		ps.RainInches = d.RainInches
		ps.RainEventAt = d.RainEventAt
		ps.LastRainAt = d.LastRainAt
		return store.SavePause(p.Path, ps)
	}
	ps.LastRainAt = d.EventLastRain
	return store.SavePause(p.Path, ps)
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
