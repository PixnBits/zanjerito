package soil

import (
	"context"
	"encoding/json"
	"log"
	"os"
	"strings"
	"sync"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/history"
	"github.com/PixnBits/zanjerito/internal/store"
)

const (
	badFetch       = "fetch"
	badImplausible = "implausible"
	badRate        = "rate"
	badEmpty       = "empty"
	badOther       = "other"

	etStaleAfter = 48 * time.Hour
)

// Status is the public ET-feed health. It never includes an AZMET station id or URL.
type Status struct {
	Enabled     bool
	Unavailable bool
	Since       *time.Time
	LastOK      *time.Time
	LastError   string
}

// ETStatus is the et object in GET /api/soil.
type ETStatus struct {
	Unavailable bool    `json:"unavailable"`
	Since       *string `json:"since"`
	LastOKAt    *string `json:"last_ok_at"`
}

// View is GET /api/soil.
// A missing file uses Reason "no soil.local.json". An invalid file uses
// Reason "soil.local.json invalid" plus ConfigError.
// UpdatedAt is the last successful ET fetch, or null when there has not been one.
// ETKnown is true only after a successful fetch that includes a day in the window.
type View struct {
	Enabled     bool       `json:"enabled"`
	Reason      string     `json:"reason,omitempty"`
	ConfigError string     `json:"config_error,omitempty"`
	ETKnown     bool       `json:"et_known"`
	ETStale     bool       `json:"et_stale"`
	ETReason    string     `json:"et_reason,omitempty"`
	UpdatedAt   *string    `json:"updated_at"`
	WindowDays  int        `json:"window_days,omitempty"`
	ET          ETStatus   `json:"et"`
	RainKnown   bool       `json:"rain_known"`
	Zones       []ZoneView `json:"zones"`
}

type diskET struct {
	UpdatedAt time.Time `json:"updated_at"`
	Days      []DayET   `json:"days"`
}

// Poller fetches AZMET on an interval and caches the last good daily ET.
// The soil model is recomputed on demand from that cache; Poller.View does
// no network I/O.
//
// mu guards status, the ET cache, and the snapshot version. Disk writes take
// writeMu only, after mu is released, so View and Status never wait on fsync.
type Poller struct {
	Source Source
	Path   string
	Cfg    Config
	Now    func() time.Time
	Log    *log.Logger
	Loc    *time.Location

	save func(path string, v any) error

	mu           sync.Mutex
	st           Status
	badCat       string
	seq          uint64
	days         []DayET
	have         bool
	loggedReview bool
	tried        bool

	writeMu sync.Mutex
	written uint64
}

// Start loads soil config beside configPath. A missing file returns
// ErrNoConfig (also fs.ErrNotExist). A disabled file returns nil, nil.
// A malformed file returns an error and does not poll.
// On success it polls once in the background loop until ctx is cancelled.
// Logs omit the AZMET station id and URL.
func Start(ctx context.Context, configPath string, loc *time.Location) (*Poller, error) {
	cfg, err := Load(configPath)
	if err != nil {
		return nil, err
	}
	if !cfg.Enabled {
		return nil, nil
	}
	if loc == nil {
		var err error
		loc, err = time.LoadLocation("America/Phoenix")
		if err != nil {
			loc = time.FixedZone("MST", -7*3600)
		}
	}
	p := &Poller{
		Source: NewAZMET(cfg),
		Path:   configPath,
		Cfg:    cfg,
		Log:    log.Default(),
		Loc:    loc,
		save:   store.AtomicWriteJSON,
	}
	p.loadCache()
	log.Printf("soil: polling every %s", cfg.PollInterval())
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

func (p *Poller) now() time.Time {
	if p != nil && p.Now != nil {
		return p.Now()
	}
	return time.Now()
}

func (p *Poller) loc() *time.Location {
	if p != nil && p.Loc != nil {
		return p.Loc
	}
	loc, err := time.LoadLocation("America/Phoenix")
	if err != nil {
		return time.FixedZone("MST", -7*3600)
	}
	return loc
}

func (p *Poller) logf(format string, args ...any) {
	lg := log.Default()
	if p != nil && p.Log != nil {
		lg = p.Log
	}
	lg.Printf(format, args...)
}

// Status returns a copy of the ET-feed health. Enabled is true when a poller exists.
// It takes only mu, never writeMu.
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

// SeedCacheForTest installs a last-good ET series without fetching.
func (p *Poller) SeedCacheForTest(days []DayET, at time.Time) {
	if p == nil {
		return
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	p.days = cloneDays(days)
	p.have = true
	t := at.UTC()
	p.st.Enabled = true
	p.st.Unavailable = false
	p.st.LastOK = &t
	p.st.Since = nil
	p.st.LastError = ""
	p.badCat = ""
	p.tried = true
}

// DaysForTest returns a copy of the last-good ET cache.
func (p *Poller) DaysForTest() []DayET {
	if p == nil {
		return nil
	}
	p.mu.Lock()
	defer p.mu.Unlock()
	return cloneDays(p.days)
}

// SetSaveFuncForTest installs fn as the ET-cache persistence function.
// Nil restores store.AtomicWriteJSON. The function runs outside mu.
func (p *Poller) SetSaveFuncForTest(fn func(path string, v any) error) {
	if p == nil {
		return
	}
	p.mu.Lock()
	if fn == nil {
		fn = store.AtomicWriteJSON
	}
	p.save = fn
	p.mu.Unlock()
}

// Poll fetches once. Fetch errors and implausible rows do not replace the
// last good cache. A cancelled ctx does not mark the feed unavailable.
// soil-et.json is written after mu is released.
func (p *Poller) Poll(ctx context.Context) {
	if p == nil || p.Source == nil {
		return
	}
	if ctx.Err() != nil {
		return
	}
	cfg := p.Cfg.normalized()
	start := windowStart(p.now(), p.loc(), cfg.WindowDays)
	days, fetchErr := p.Source.Fetch(ctx, start, cfg.WindowDays)
	if ctx.Err() != nil {
		return
	}

	var (
		logs   []string
		snap   []DayET
		ver    uint64
		path   string
		save   func(string, any) error
		at     time.Time
		doSave bool
	)

	p.mu.Lock()
	now := p.now()
	if fetchErr != nil {
		msg := redactErr(fetchErr, cfg.Station)
		if line := p.observeBad(msg, badCategory(msg), now); line != "" {
			logs = append(logs, line)
		}
	} else if implausibleET(days, cfg.MaxDailyET) {
		if line := p.observeBad("implausible ET reading", badImplausible, now); line != "" {
			logs = append(logs, line)
		}
	} else {
		if line := p.observeGood(now); line != "" {
			logs = append(logs, line)
		}
		if !p.loggedReview {
			for _, d := range days {
				if d.NeedsReview {
					logs = append(logs, "soil: AZMET row needs review (kept)")
					p.loggedReview = true
					break
				}
			}
		}
		p.days = cloneDays(days)
		p.have = true
		p.seq++
		ver = p.seq
		snap = cloneDays(p.days)
		path = CachePath(p.Path)
		save = p.save
		at = now
		doSave = p.Path != ""
	}
	p.mu.Unlock()

	for _, line := range logs {
		p.logf("%s", line)
	}
	if doSave {
		if save == nil {
			save = store.AtomicWriteJSON
		}
		if err := p.persist(path, diskET{UpdatedAt: at.UTC(), Days: snap}, ver, save); err != nil {
			p.logf("soil: save ET cache: %v", err)
		}
	}
}

func implausibleET(days []DayET, max float64) bool {
	if max <= 0 {
		max = DefaultMaxDailyET
	}
	for _, d := range days {
		if d.ETInches < 0 || d.ETInches > max {
			return true
		}
	}
	return false
}

func badCategory(msg string) string {
	switch {
	case strings.Contains(msg, "implausible"):
		return badImplausible
	case strings.Contains(msg, "rate-limited"):
		return badRate
	case strings.Contains(msg, "no observations"):
		return badEmpty
	case strings.Contains(msg, "HTTP"):
		return badFetch
	default:
		return badOther
	}
}

func (p *Poller) observeBad(msg, cat string, now time.Time) string {
	p.tried = true
	changed := !p.st.Unavailable || p.badCat != cat
	if !p.st.Unavailable {
		t := now.UTC()
		p.st.Since = &t
	}
	p.st.Enabled = true
	p.st.Unavailable = true
	p.st.LastError = msg
	p.badCat = cat
	if !changed {
		return ""
	}
	switch cat {
	case badFetch:
		return "soil: fetch failed: " + msg
	case badRate:
		return "soil: rate-limited"
	case badImplausible:
		return "soil: implausible ET reading"
	default:
		return "soil: " + msg
	}
}

func (p *Poller) observeGood(now time.Time) string {
	wasBad := p.st.Unavailable
	t := now.UTC()
	p.tried = true
	p.st.Enabled = true
	p.st.Unavailable = false
	p.st.LastError = ""
	p.st.LastOK = &t
	p.st.Since = nil
	p.badCat = ""
	if !wasBad {
		return ""
	}
	return "soil: data available again"
}

func (p *Poller) persist(path string, doc diskET, ver uint64, save func(string, any) error) error {
	if save == nil {
		save = store.AtomicWriteJSON
	}
	p.writeMu.Lock()
	defer p.writeMu.Unlock()
	if ver <= p.written {
		return nil
	}
	if err := save(path, doc); err != nil {
		return err
	}
	p.written = ver
	return nil
}

func (p *Poller) loadCache() {
	if p == nil || p.Path == "" {
		return
	}
	b, err := os.ReadFile(CachePath(p.Path))
	if err != nil {
		return
	}
	var doc diskET
	if err := json.Unmarshal(b, &doc); err != nil {
		p.logf("soil: ET cache ignored")
		return
	}
	cfg := p.Cfg.normalized()
	if implausibleET(doc.Days, cfg.MaxDailyET) || len(doc.Days) == 0 {
		return
	}
	p.mu.Lock()
	p.days = cloneDays(doc.Days)
	p.have = true
	p.tried = true
	if !doc.UpdatedAt.IsZero() {
		t := doc.UpdatedAt.UTC()
		p.st.LastOK = &t
	}
	p.st.Enabled = true
	p.mu.Unlock()
}

func cloneDays(in []DayET) []DayET {
	if in == nil {
		return []DayET{}
	}
	out := make([]DayET, len(in))
	copy(out, in)
	return out
}

func rfc3339Ptr(t *time.Time, loc *time.Location) *string {
	if t == nil {
		return nil
	}
	if loc == nil {
		loc = time.UTC
	}
	s := t.In(loc).Format(time.RFC3339)
	return &s
}

// DisabledView is GET /api/soil when the feature is off.
func DisabledView(reason string, _ time.Time, loc *time.Location) View {
	if loc == nil {
		loc = time.UTC
	}
	if reason == "" {
		reason = "no soil.local.json"
	}
	return View{
		Enabled:   false,
		Reason:    reason,
		UpdatedAt: nil,
		ET:        ETStatus{},
		Zones:     []ZoneView{},
	}
}

// View recomputes the estimate from the last-good ET cache plus caller-supplied
// rain, history, and engine stations. It does no network I/O.
func (p *Poller) View(stations []engine.StationConfig, hist []history.Entry, rainByDate map[string]float64, rainKnown bool, now time.Time, loc *time.Location) View {
	if p == nil {
		return DisabledView("no soil.local.json", now, loc)
	}
	if loc == nil {
		loc = p.loc()
	}
	p.mu.Lock()
	cfg := p.Cfg
	days := cloneDays(p.days)
	tried := p.tried
	st := p.st
	if st.LastOK != nil {
		t := *st.LastOK
		st.LastOK = &t
	}
	if st.Since != nil {
		t := *st.Since
		st.Since = &t
	}
	p.mu.Unlock()

	cfg = cfg.normalized()
	dates := windowDates(now, loc, cfg.WindowDays)
	etKnown := st.LastOK != nil && etDayInWindow(days, dates)
	etStale := st.LastOK != nil && now.Sub(*st.LastOK) > etStaleAfter
	zones := Estimate(cfg, stations, days, rainByDate, rainKnown, hist, now, loc)
	if zones == nil {
		zones = []ZoneView{}
	}
	if !etKnown {
		for i := range zones {
			zones[i].Percent = nil
			zones[i].BalanceInches = nil
		}
	}
	return View{
		Enabled:    true,
		ETKnown:    etKnown,
		ETStale:    etStale,
		ETReason:   etReason(tried, st, etKnown),
		UpdatedAt:  rfc3339Ptr(st.LastOK, loc),
		WindowDays: cfg.WindowDays,
		ET: ETStatus{
			Unavailable: st.Unavailable,
			Since:       rfc3339Ptr(st.Since, loc),
			LastOKAt:    rfc3339Ptr(st.LastOK, loc),
		},
		RainKnown: rainKnown,
		Zones:     zones,
	}
}

func etDayInWindow(days []DayET, dates []string) bool {
	if len(days) == 0 || len(dates) == 0 {
		return false
	}
	want := make(map[string]struct{}, len(dates))
	for _, d := range dates {
		want[d] = struct{}{}
	}
	for _, d := range days {
		if d.Date == "" {
			continue
		}
		if _, ok := want[d.Date]; ok {
			return true
		}
	}
	return false
}

func etReason(tried bool, st Status, known bool) string {
	if known {
		return ""
	}
	if st.LastOK == nil && !tried && !st.Unavailable {
		return "waiting for first ET fetch"
	}
	reason := "no ET data yet"
	if st.Unavailable || st.LastError != "" {
		reason += " (" + etErrorClass(st.LastError) + ")"
	}
	return reason
}

func etErrorClass(msg string) string {
	switch {
	case strings.Contains(msg, "rate-limited"):
		return "rate-limited"
	case strings.Contains(msg, "implausible"):
		return "implausible ET"
	case strings.Contains(msg, "no observations"):
		return "no observations"
	default:
		return "AZMET unavailable"
	}
}
