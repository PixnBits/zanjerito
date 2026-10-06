package api

import (
	"fmt"
	"net/http"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
)

// haZone is one configured station in config order.
// id is zone_N, 1-based. The station id is not included.
type haZone struct {
	ID string `json:"id"`
	On bool   `json:"on"`
}

// haBody is GET /api/ha. Every field is always present.
// Times are RFC3339 in the config timezone, or null.
// Totals are 0 when the feed is off or the numbers are not known.
type haBody struct {
	Phase         string   `json:"phase"`
	Paused        bool     `json:"paused"`
	PauseSource   string   `json:"pause_source"`
	PausedUntil   *string  `json:"paused_until"`
	Lockout       bool     `json:"lockout"`
	HasError      bool     `json:"has_error"`
	ActiveZone    *string  `json:"active_zone"`
	ZonesOn       int      `json:"zones_on"`
	StepRemaining int      `json:"step_remaining_sec"`
	RunRemaining  int      `json:"run_remaining_sec"`
	NextRunAt     *string  `json:"next_run_at"`
	NextRunEndsAt *string  `json:"next_run_ends_at"`
	NextRunMin    int      `json:"next_run_total_min"`
	NextSkipped   bool     `json:"next_run_skipped_by_pause"`
	RainEnabled   bool     `json:"rain_enabled"`
	RainUnavail   bool     `json:"rain_unavailable"`
	RainLastOK    *string  `json:"rain_last_ok_at"`
	Rain24        float64  `json:"rain_24h_in"`
	Rain72        float64  `json:"rain_72h_in"`
	LastRunKind   *string  `json:"last_run_kind"`
	LastRunOut    *string  `json:"last_run_outcome"`
	LastRunStart  *string  `json:"last_run_started_at"`
	LastRunEnd    *string  `json:"last_run_ended_at"`
	Zones         []haZone `json:"zones"`
}

// handleHA is the read-only Home Assistant snapshot.
// It uses the same load, pause, rain, next-run, and progress helpers as
// /api/kiosk, but it does not call PauseDetail: that expires a timed pause
// in memory. An elapsed until is reported as not paused and left stored.
// It does not write config, history, or pause.json, and it does not start a run.
// Registered for every method so the mux does not answer 405 itself.
// Anything but GET, including HEAD, is 405 with Allow exactly GET.
func (s *Server) handleHA(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", http.MethodGet)
		http.Error(w, http.StatusText(http.StatusMethodNotAllowed), http.StatusMethodNotAllowed)
		return
	}
	w.Header().Set("Cache-Control", "no-store")
	f, err := s.load()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err)
		return
	}
	now := s.haClock()
	loc := s.location()
	detail := s.pauseSnapReadOnly(now)
	st := s.Eng.Status()
	body := haBody{
		Phase:       string(st.Phase),
		Paused:      detail.Paused,
		PauseSource: pauseSourceOf(detail),
		PausedUntil: pauseUntilOf(detail, loc),
		Lockout:     false,
		HasError:    st.LastError != "",
		Zones:       haZones(f.Stations, st),
	}
	body.ZonesOn, body.ActiveZone = haActive(f.Stations, st)
	if prog, ok := s.Eng.RunProgress(now); ok {
		run := runFromProgress(prog, loc, f.Stations)
		body.StepRemaining = run.StepRemaining
		body.RunRemaining = run.RunRemainingSec
	}
	if next := scheduledFire(f.Schedules, now, loc, detail); next != nil {
		body.NextRunAt = &next.At
		body.NextRunEndsAt = &next.EndsAt
		body.NextRunMin = next.TotalMin
		body.NextSkipped = next.SkippedByPause
	}
	body.RainEnabled, body.RainUnavail, body.RainLastOK, body.Rain24, body.Rain72 = haRain(s, now, loc, detail)
	body.LastRunKind, body.LastRunOut, body.LastRunStart, body.LastRunEnd = haLastRun(s, loc)
	writeJSON(w, http.StatusOK, body)
}

func (s *Server) haClock() time.Time {
	if s != nil && s.haNow != nil {
		return s.haNow()
	}
	return time.Now()
}

// pauseSnapReadOnly is PauseRaw, with an elapsed until shown as not paused.
// It does not clear the engine. PauseDetail would.
func (s *Server) pauseSnapReadOnly(now time.Time) engine.PauseSnap {
	d := s.Eng.PauseRaw()
	if d.Paused && d.Until != nil && !d.Until.After(now) {
		return engine.PauseSnap{}
	}
	return d
}

func pauseSourceOf(d engine.PauseSnap) string {
	if !d.Paused {
		return ""
	}
	if d.Source == "" {
		return engine.PauseSourceManual
	}
	return d.Source
}

func pauseUntilOf(d engine.PauseSnap, loc *time.Location) *string {
	if !d.Paused || d.Until == nil {
		return nil
	}
	if loc == nil {
		loc = time.UTC
	}
	s := d.Until.In(loc).Format(time.RFC3339)
	return &s
}

func haZones(stations []engine.StationConfig, st engine.Status) []haZone {
	on := map[string]bool{}
	for _, id := range st.StationsOn {
		on[id] = true
	}
	out := make([]haZone, 0, len(stations))
	for i, stn := range stations {
		out = append(out, haZone{
			ID: fmt.Sprintf("zone_%d", i+1),
			On: on[stn.ID],
		})
	}
	return out
}

// haActive maps the current station to zone_N. zones_on counts configured
// stations that are on, in the same order as haZones.
func haActive(stations []engine.StationConfig, st engine.Status) (int, *string) {
	on := map[string]bool{}
	for _, id := range st.StationsOn {
		on[id] = true
	}
	n := 0
	var active *string
	for i, stn := range stations {
		if !on[stn.ID] {
			continue
		}
		n++
		if stn.ID == st.CurrentStation {
			z := fmt.Sprintf("zone_%d", i+1)
			active = &z
		}
	}
	return n, active
}

func haRain(s *Server, now time.Time, loc *time.Location, detail engine.PauseSnap) (enabled, unavailable bool, lastOK *string, in24, in72 float64) {
	m, _ := s.rainBody(now, loc, detail.AutoRain())
	enabled = m["enabled"] == true
	unavailable = m["unavailable"] == true
	if v, ok := m["last_ok_at"].(string); ok && v != "" {
		lastOK = &v
	}
	if v, ok := m["total_24h_inches"].(float64); ok {
		in24 = v
	}
	if v, ok := m["total_72h_inches"].(float64); ok {
		in72 = v
	}
	return enabled, unavailable, lastOK, in24, in72
}

func haLastRun(s *Server, loc *time.Location) (kind, outcome, started, ended *string) {
	if s.History == nil {
		return nil, nil, nil, nil
	}
	entries := s.History.List(1)
	if len(entries) == 0 {
		return nil, nil, nil, nil
	}
	e := entries[0]
	if loc == nil {
		loc = time.UTC
	}
	k := haToken(e.Kind, engine.KindSchedule, engine.KindManual)
	o := haToken(e.Outcome,
		engine.OutcomeCompleted,
		engine.OutcomeStopped,
		engine.OutcomeSkipped,
		engine.OutcomeRefused,
		engine.OutcomeError,
	)
	a := e.StartedAt.In(loc).Format(time.RFC3339)
	b := e.EndedAt.In(loc).Format(time.RFC3339)
	return k, o, &a, &b
}

// haToken keeps a label the engine and scheduler actually store.
// Empty or any other value is "unknown".
// No history is null, decided by haLastRun before this is called.
func haToken(v string, allowed ...string) *string {
	for _, a := range allowed {
		if v == a {
			return &v
		}
	}
	v = "unknown"
	return &v
}
