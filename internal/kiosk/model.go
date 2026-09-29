// Package kiosk is a read-only touchscreen client for the zanjerito HTTP API.
// The daemon stays the source of truth. Screens are drawn for an 800x480 panel.
package kiosk

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

const (
	// StatusPoll is the GET /api/status interval.
	StatusPoll = 2 * time.Second
	// CatalogPoll is the GET /api/stations and GET /api/schedules interval.
	// Both are also fetched once at start via Load.
	CatalogPoll = 60 * time.Second
	// ClientTimeout bounds each daemon request.
	ClientTimeout = 2 * time.Second
)

// PollIntervals is how often Poll refetches status vs stations/schedules.
type PollIntervals struct {
	Status  time.Duration
	Catalog time.Duration
}

// DefaultPoll is status every 2s and the station/schedule catalog every 60s.
func DefaultPoll() PollIntervals {
	return PollIntervals{Status: StatusPoll, Catalog: CatalogPoll}
}

// Rain is the rain object on GET /api/status.
type Rain struct {
	Enabled        bool    `json:"enabled"`
	Unavailable    bool    `json:"unavailable"`
	Total24hInches float64 `json:"total_24h_inches"`
	Total72hInches float64 `json:"total_72h_inches"`
}

// RainStrip is the header chip. It is shown only when Show is true.
type RainStrip struct {
	Show   bool    `json:"show"`
	Inches float64 `json:"inches"`
	Hours  int     `json:"hours"`
}

// Status is GET /api/status.
type Status struct {
	Phase          string     `json:"phase"`
	StationsOn     []string   `json:"stations_on"`
	CurrentStation string     `json:"current_station"`
	LastError      string     `json:"last_error"`
	Lockout        bool       `json:"lockout"`
	Paused         bool       `json:"paused"`
	PausedUntil    *time.Time `json:"paused_until"`
	PausedLabel    string     `json:"paused_label"`
	PauseSource    string     `json:"pause_source"`
	Reason         string     `json:"reason"`
	Rain           Rain       `json:"rain"`
	RainInches     float64    `json:"rain_inches"`
	RainStrip      RainStrip  `json:"rain_strip"`
	Now            string     `json:"now"`
	Timezone       string     `json:"timezone"`
}

// StationOn reports whether id is the running station.
func (s Status) StationOn(id string) bool {
	if id == "" {
		return false
	}
	if id == s.CurrentStation {
		return true
	}
	for _, on := range s.StationsOn {
		if on == id {
			return true
		}
	}
	return false
}

// Location loads Timezone, or UTC when it is empty or unknown.
func (s Status) Location() *time.Location {
	if s.Timezone == "" {
		return time.UTC
	}
	loc, err := time.LoadLocation(s.Timezone)
	if err != nil {
		return time.UTC
	}
	return loc
}

// NowTime parses Status.Now as RFC3339.
func (s Status) NowTime() (time.Time, error) {
	if s.Now == "" {
		return time.Time{}, fmt.Errorf("status now is empty")
	}
	return time.Parse(time.RFC3339, s.Now)
}

// Station is one entry from GET /api/stations.
type Station struct {
	ID    string `json:"id"`
	Title string `json:"title"`
	Color string `json:"color"`
}

// Step is one station run inside a schedule.
type Step struct {
	StationID string `json:"station_id"`
	Minutes   int    `json:"minutes"`
}

// Schedule is one entry from GET /api/schedules.
type Schedule struct {
	ID       string   `json:"id"`
	Enabled  bool     `json:"enabled"`
	Note     string   `json:"note,omitempty"`
	Weekdays []string `json:"weekdays,omitempty"`
	Start    string   `json:"start,omitempty"`
	Steps    []Step   `json:"steps,omitempty"`
	StartsOn string   `json:"starts_on,omitempty"`
	EndsOn   string   `json:"ends_on,omitempty"`
}

// Snapshot is the client's copy of the three GET payloads.
type Snapshot struct {
	Status    Status
	Stations  []Station
	Schedules []Schedule
}

type stationsDoc struct {
	Stations []Station `json:"stations"`
}

type schedulesDoc struct {
	Schedules []Schedule `json:"schedules"`
}

// Client fetches the daemon. HTTP requests time out after ClientTimeout.
type Client struct {
	Base string
	HTTP *http.Client
}

// NewClient returns a client for baseURL with a 2s timeout.
func NewClient(baseURL string) *Client {
	return &Client{
		Base: strings.TrimRight(baseURL, "/"),
		HTTP: &http.Client{Timeout: ClientTimeout},
	}
}

func (c *Client) httpClient() *http.Client {
	if c.HTTP != nil {
		return c.HTTP
	}
	return &http.Client{Timeout: ClientTimeout}
}

func (c *Client) url(path string) string {
	return strings.TrimRight(c.Base, "/") + path
}

func (c *Client) get(ctx context.Context, path string, dst any) error {
	if ctx == nil {
		ctx = context.Background()
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, c.url(path), nil)
	if err != nil {
		return err
	}
	resp, err := c.httpClient().Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		_, _ = io.Copy(io.Discard, io.LimitReader(resp.Body, 4096))
		return fmt.Errorf("GET %s: %s", path, resp.Status)
	}
	return json.NewDecoder(io.LimitReader(resp.Body, 1<<20)).Decode(dst)
}

// Status fetches GET /api/status.
func (c *Client) Status(ctx context.Context) (Status, error) {
	var st Status
	err := c.get(ctx, "/api/status", &st)
	return st, err
}

// Stations fetches GET /api/stations.
func (c *Client) Stations(ctx context.Context) ([]Station, error) {
	var doc stationsDoc
	if err := c.get(ctx, "/api/stations", &doc); err != nil {
		return nil, err
	}
	return doc.Stations, nil
}

// Schedules fetches GET /api/schedules.
func (c *Client) Schedules(ctx context.Context) ([]Schedule, error) {
	var doc schedulesDoc
	if err := c.get(ctx, "/api/schedules", &doc); err != nil {
		return nil, err
	}
	return doc.Schedules, nil
}

// Load fetches status, stations, and schedules once.
func (c *Client) Load(ctx context.Context) (Snapshot, error) {
	var snap Snapshot
	var err error
	if snap.Status, err = c.Status(ctx); err != nil {
		return snap, err
	}
	if snap.Stations, err = c.Stations(ctx); err != nil {
		return snap, err
	}
	snap.Schedules, err = c.Schedules(ctx)
	return snap, err
}

// Poll refetches status on iv.Status and the catalog on iv.Catalog.
// It does not fetch on entry; call Load first. emit may be nil.
func (c *Client) Poll(ctx context.Context, iv PollIntervals, snap Snapshot, emit func(Snapshot)) error {
	if ctx == nil {
		ctx = context.Background()
	}
	if iv.Status <= 0 {
		iv.Status = StatusPoll
	}
	if iv.Catalog <= 0 {
		iv.Catalog = CatalogPoll
	}
	statusTick := time.NewTicker(iv.Status)
	catalogTick := time.NewTicker(iv.Catalog)
	defer statusTick.Stop()
	defer catalogTick.Stop()
	for {
		select {
		case <-ctx.Done():
			return ctx.Err()
		case <-catalogTick.C:
			if stations, err := c.Stations(ctx); err == nil {
				snap.Stations = stations
			}
			if schedules, err := c.Schedules(ctx); err == nil {
				snap.Schedules = schedules
			}
			if ctx.Err() != nil {
				return ctx.Err()
			}
			if emit != nil {
				emit(snap)
			}
		case <-statusTick.C:
			st, err := c.Status(ctx)
			if err != nil {
				if ctx.Err() != nil {
					return ctx.Err()
				}
				continue
			}
			snap.Status = st
			if emit != nil {
				emit(snap)
			}
		}
	}
}

// LoadFixture reads status_<which>.json plus stations.json and schedules.json.
// which is idle, running, or paused.
func LoadFixture(dir, which string) (Snapshot, error) {
	var name string
	switch strings.ToLower(strings.TrimSpace(which)) {
	case "", "idle":
		name = "status_idle.json"
	case "running":
		name = "status_running.json"
	case "paused":
		name = "status_paused.json"
	default:
		return Snapshot{}, fmt.Errorf("fixture-status must be idle, running, or paused")
	}
	var snap Snapshot
	if err := readJSON(filepath.Join(dir, name), &snap.Status); err != nil {
		return snap, err
	}
	var stations stationsDoc
	if err := readJSON(filepath.Join(dir, "stations.json"), &stations); err != nil {
		return snap, err
	}
	snap.Stations = stations.Stations
	var schedules schedulesDoc
	if err := readJSON(filepath.Join(dir, "schedules.json"), &schedules); err != nil {
		return snap, err
	}
	snap.Schedules = schedules.Schedules
	return snap, nil
}

func readJSON(path string, dst any) error {
	b, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	if err := json.Unmarshal(b, dst); err != nil {
		return fmt.Errorf("%s: %w", path, err)
	}
	return nil
}

// Upcoming is the next enabled schedule start in loc.
type Upcoming struct {
	At         time.Time
	Label      string // "Today 08:23", "Tomorrow 08:23", or "Thu 07:31"
	Summary    string
	ScheduleID string
	Found      bool
}

// NextRun returns the soonest enabled start strictly after now.
// Weekdays, starts_on, and ends_on are inclusive local dates.
// Empty weekdays means every day. Labels use loc's calendar.
func NextRun(schedules []Schedule, stations []Station, now time.Time, loc *time.Location) Upcoming {
	if loc == nil {
		loc = time.UTC
	}
	now = now.In(loc)
	var best Upcoming
	for _, sch := range schedules {
		at, ok := nextStart(sch, now, loc)
		if !ok {
			continue
		}
		if best.Found && !at.Before(best.At) {
			continue
		}
		best = Upcoming{
			At:         at,
			Label:      runLabel(now, at),
			Summary:    stepSummary(sch.Steps, stations),
			ScheduleID: sch.ID,
			Found:      true,
		}
	}
	return best
}

func nextStart(sch Schedule, now time.Time, loc *time.Location) (time.Time, bool) {
	if !sch.Enabled {
		return time.Time{}, false
	}
	hh, mm, ok := parseHHMM(sch.Start)
	if !ok {
		return time.Time{}, false
	}
	startKey, hasStart, okStart := dayKey(sch.StartsOn, loc)
	if !okStart {
		return time.Time{}, false
	}
	endKey, hasEnd, okEnd := dayKey(sch.EndsOn, loc)
	if !okEnd {
		return time.Time{}, false
	}
	y, m, d := now.Date()
	day0 := time.Date(y, m, d, 0, 0, 0, 0, loc)
	for i := 0; i <= 370; i++ {
		day := day0.AddDate(0, 0, i)
		key := ymd(day)
		if hasStart && key < startKey {
			continue
		}
		if hasEnd && key > endKey {
			return time.Time{}, false
		}
		if !weekdayOK(sch.Weekdays, day.Weekday()) {
			continue
		}
		cand := time.Date(day.Year(), day.Month(), day.Day(), hh, mm, 0, 0, loc)
		if cand.After(now) {
			return cand, true
		}
	}
	return time.Time{}, false
}

// dayKey parses YYYY-MM-DD. An empty string is "no bound". A bad string fails closed.
func dayKey(s string, loc *time.Location) (key int, present, ok bool) {
	if strings.TrimSpace(s) == "" {
		return 0, false, true
	}
	t, err := time.ParseInLocation("2006-01-02", s, loc)
	if err != nil {
		return 0, false, false
	}
	return ymd(t), true, true
}

func ymd(t time.Time) int {
	y, m, d := t.Date()
	return y*10000 + int(m)*100 + d
}

func parseHHMM(s string) (int, int, bool) {
	parts := strings.Split(s, ":")
	if len(parts) != 2 {
		return 0, 0, false
	}
	hh, err1 := strconv.Atoi(parts[0])
	mm, err2 := strconv.Atoi(parts[1])
	if err1 != nil || err2 != nil || hh < 0 || hh > 23 || mm < 0 || mm > 59 {
		return 0, 0, false
	}
	return hh, mm, true
}

func weekdayOK(days []string, wd time.Weekday) bool {
	if len(days) == 0 {
		return true
	}
	full := strings.ToLower(wd.String())
	short := full
	if len(short) > 3 {
		short = short[:3]
	}
	for _, d := range days {
		d = strings.ToLower(strings.TrimSpace(d))
		if d == full || d == short || (len(d) >= 3 && d[:3] == short) {
			return true
		}
	}
	return false
}

func runLabel(now, at time.Time) string {
	tod := at.Format("15:04")
	switch dayDelta(now, at) {
	case 0:
		return "Today " + tod
	case 1:
		return "Tomorrow " + tod
	default:
		return at.Format("Mon") + " " + tod
	}
}

func dayDelta(from, to time.Time) int {
	fy, fm, fd := from.Date()
	ty, tm, td := to.Date()
	a := time.Date(fy, fm, fd, 12, 0, 0, 0, time.UTC)
	b := time.Date(ty, tm, td, 12, 0, 0, 0, time.UTC)
	return int(b.Sub(a).Hours() / 24)
}

func stepSummary(steps []Step, stations []Station) string {
	names := make(map[string]string, len(stations))
	for _, st := range stations {
		if st.Title != "" {
			names[st.ID] = st.Title
		}
	}
	var b strings.Builder
	for i, st := range steps {
		if i > 0 {
			b.WriteString(", ")
		}
		name := names[st.StationID]
		if name == "" {
			name = st.StationID
		}
		fmt.Fprintf(&b, "%s %dm", name, st.Minutes)
	}
	return b.String()
}
