package api

import (
	"net/http"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/rain"
	"github.com/PixnBits/zanjerito/internal/schedule"
	"github.com/PixnBits/zanjerito/internal/soil"
	"github.com/PixnBits/zanjerito/internal/store"
)

// kioskFire is one scheduled fire. skipped_by_pause is about this fire and
// the active pause: true when the pause is indefinite or at is before until.
// A fire exactly at until is not skipped.
type kioskFire struct {
	ScheduleID     string `json:"schedule_id"`
	Name           string `json:"name"`
	At             string `json:"at"`
	EndsAt         string `json:"ends_at"`
	TotalMin       int    `json:"total_min"`
	SkippedByPause bool   `json:"skipped_by_pause"`
}

type kioskPause struct {
	Paused     bool    `json:"paused"`
	Until      *string `json:"until"`
	Label      string  `json:"label"`
	Reason     string  `json:"reason"`
	Source     string  `json:"source"`
	RainInches float64 `json:"rain_inches"`
	LastRainAt *string `json:"last_rain_at"`
}

type kioskRain struct {
	Enabled     bool    `json:"enabled"`
	Unavailable bool    `json:"unavailable"`
	Total24     float64 `json:"total_24h_inches"`
	Total72     float64 `json:"total_72h_inches"`
	HaveTotals  bool    `json:"have_totals"`
}

type kioskRunStep struct {
	StationID    string `json:"station_id"`
	Title        string `json:"title"`
	PlannedSec   int    `json:"planned_sec"`
	ElapsedSec   int    `json:"elapsed_sec"`
	RemainingSec int    `json:"remaining_sec"`
	State        string `json:"state"`
}

type kioskRun struct {
	Kind            string         `json:"kind"`
	ProgramID       string         `json:"program_id"`
	Program         string         `json:"program"`
	StartedAt       string         `json:"started_at"`
	StepIndex       int            `json:"step_index"`
	StepCount       int            `json:"step_count"`
	CurrentStation  string         `json:"current_station"`
	NextStation     string         `json:"next_station"`
	StepElapsedSec  int            `json:"step_elapsed_sec"`
	StepRemaining   int            `json:"step_remaining_sec"`
	RunRemainingSec int            `json:"run_remaining_sec"`
	RunTotalSec     int            `json:"run_total_sec"`
	Steps           []kioskRunStep `json:"steps"`
}

type kioskStation struct {
	ID              string `json:"id"`
	Title           string `json:"title"`
	Color           string `json:"color"`
	On              bool   `json:"on"`
	State           string `json:"state"`
	RainPauseExempt bool   `json:"rain_pause_exempt"`
	SoilPercent     *int   `json:"soil_percent"`
}

type kioskSoil struct {
	Enabled   bool    `json:"enabled"`
	ETKnown   bool    `json:"et_known"`
	ETStale   bool    `json:"et_stale"`
	ShowBars  bool    `json:"show_bars"`
	UpdatedAt *string `json:"updated_at"`
}

type kioskBody struct {
	Now            string         `json:"now"`
	Timezone       string         `json:"timezone"`
	Phase          engine.Phase   `json:"phase"`
	Lockout        bool           `json:"lockout"`
	LastError      string         `json:"last_error"`
	CurrentStation string         `json:"current_station"`
	StationsOn     []string       `json:"stations_on"`
	Pause          kioskPause     `json:"pause"`
	RainStrip      rain.RainStrip `json:"rain_strip"`
	Rain           kioskRain      `json:"rain"`
	NextRun        *kioskFire     `json:"next_run"`
	NextEffective  *kioskFire     `json:"next_effective_run"`
	Run            *kioskRun      `json:"run"`
	Stations       []kioskStation `json:"stations"`
	Soil           kioskSoil      `json:"soil"`
}

// handleKiosk is the read-only snapshot for the native kiosk and Home.
// It does not sync an expired pause to disk and does not start a goroutine.
func (s *Server) handleKiosk(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Cache-Control", "no-store")
	f, err := s.load()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err)
		return
	}
	now := time.Now()
	loc, fields, detail := s.pauseView(now)
	st := s.Eng.Status()
	prog, running := s.Eng.RunProgress(now)
	view := s.soilSnapshot(now, loc, f.Stations)
	on := st.StationsOn
	if on == nil {
		on = []string{}
	}
	next := scheduledFire(f.Schedules, now, loc, detail)
	body := kioskBody{
		Now:            now.In(loc).Format(time.RFC3339),
		Timezone:       loc.String(),
		Phase:          st.Phase,
		Lockout:        false,
		LastError:      st.LastError,
		CurrentStation: st.CurrentStation,
		StationsOn:     on,
		Pause:          pauseFromFields(fields),
		RainStrip:      stripFromFields(fields),
		Rain:           rainFromFields(fields),
		NextRun:        next,
		NextEffective:  effectiveFire(f.Schedules, loc, detail, next),
		Stations:       kioskStations(f, st, prog, running, view),
		Soil:           soilFromView(view),
	}
	if running {
		body.Run = runFromProgress(prog, loc, f.Stations)
	}
	writeJSON(w, http.StatusOK, body)
}

// pauseView is pauseFields plus the snap, so kiosk can compare instants
// without parsing the formatted until string.
func (s *Server) pauseView(now time.Time) (loc *time.Location, fields map[string]any, detail engine.PauseSnap) {
	loc, fields = s.pauseFields(now)
	detail = s.Eng.PauseDetail(now)
	return loc, fields, detail
}

func pauseFromFields(fields map[string]any) kioskPause {
	p := kioskPause{
		Paused: fields["paused"] == true,
		Label:  fieldString(fields["paused_label"]),
		Reason: fieldString(fields["reason"]),
		Source: fieldString(fields["pause_source"]),
	}
	if v, ok := fields["rain_inches"].(float64); ok {
		p.RainInches = v
	}
	p.Until = fieldStringPtr(fields["paused_until"])
	p.LastRainAt = fieldStringPtr(fields["last_rain_at"])
	return p
}

func stripFromFields(fields map[string]any) rain.RainStrip {
	m, _ := fields["rain_strip"].(rain.RainStrip)
	return m
}

func rainFromFields(fields map[string]any) kioskRain {
	m, _ := fields["rain"].(map[string]any)
	out := kioskRain{
		Enabled:     m["enabled"] == true,
		Unavailable: m["unavailable"] == true,
	}
	v24, ok24 := m["total_24h_inches"].(float64)
	v72, ok72 := m["total_72h_inches"].(float64)
	if ok24 && ok72 {
		out.Total24 = v24
		out.Total72 = v72
		out.HaveTotals = true
	}
	return out
}

func fieldString(v any) string {
	s, _ := v.(string)
	return s
}

func fieldStringPtr(v any) *string {
	s, ok := v.(string)
	if !ok || s == "" {
		return nil
	}
	return &s
}

// scheduledFire is the next fire ignoring the pause. skipped_by_pause is set
// when that fire would not start because of the active pause.
func scheduledFire(schedules []store.Schedule, now time.Time, loc *time.Location, detail engine.PauseSnap) *kioskFire {
	info, ok := schedule.NextRun(schedules, now, loc)
	if !ok {
		return nil
	}
	return fireJSON(info, loc, skippedByPause(detail, info.At))
}

// effectiveFire is the first fire that still runs. An indefinite pause has
// none. When nothing is paused it is the same fire as next.
func effectiveFire(schedules []store.Schedule, loc *time.Location, detail engine.PauseSnap, next *kioskFire) *kioskFire {
	if !detail.Paused {
		if next == nil {
			return nil
		}
		cp := *next
		return &cp
	}
	if detail.Until == nil {
		return nil
	}
	info, ok := schedule.NextRunAfterPause(schedules, *detail.Until, loc)
	if !ok {
		return nil
	}
	return fireJSON(info, loc, false)
}

func skippedByPause(detail engine.PauseSnap, at time.Time) bool {
	if !detail.Paused {
		return false
	}
	if detail.Until == nil {
		return true
	}
	return at.Before(*detail.Until)
}

func fireJSON(info schedule.NextRunInfo, loc *time.Location, skipped bool) *kioskFire {
	if loc == nil {
		loc = time.UTC
	}
	return &kioskFire{
		ScheduleID:     info.ScheduleID,
		Name:           info.Name,
		At:             info.At.In(loc).Format(time.RFC3339),
		EndsAt:         info.EndsAt.In(loc).Format(time.RFC3339),
		TotalMin:       info.TotalMin,
		SkippedByPause: skipped,
	}
}

func runFromProgress(snap engine.RunProgressSnap, loc *time.Location, stations []engine.StationConfig) *kioskRun {
	if loc == nil {
		loc = time.UTC
	}
	titles := stationTitles(stations)
	steps := make([]kioskRunStep, len(snap.Steps))
	var total, remain, stepE, stepR int
	cur, next := "", ""
	for i, st := range snap.Steps {
		eSec, rSec, pSec := splitSec(st.Elapsed, st.Planned)
		title := titles[st.StationID]
		if title == "" {
			title = st.StationID
		}
		steps[i] = kioskRunStep{
			StationID:    st.StationID,
			Title:        title,
			PlannedSec:   pSec,
			ElapsedSec:   eSec,
			RemainingSec: rSec,
			State:        st.State,
		}
		total += pSec
		remain += rSec
		if i == snap.StepIndex {
			stepE, stepR = eSec, rSec
			cur = st.StationID
			if i+1 < len(snap.Steps) {
				next = snap.Steps[i+1].StationID
			}
		}
	}
	if snap.StepIndex < 0 && len(snap.Steps) > 0 {
		next = snap.Steps[0].StationID
	}
	if steps == nil {
		steps = []kioskRunStep{}
	}
	return &kioskRun{
		Kind:            snap.Kind,
		ProgramID:       snap.ProgramID,
		Program:         snap.Program,
		StartedAt:       snap.Started.In(loc).Format(time.RFC3339),
		StepIndex:       snap.StepIndex,
		StepCount:       len(snap.Steps),
		CurrentStation:  cur,
		NextStation:     next,
		StepElapsedSec:  stepE,
		StepRemaining:   stepR,
		RunRemainingSec: remain,
		RunTotalSec:     total,
		Steps:           steps,
	}
}

// splitSec truncates to whole seconds and keeps elapsed+remaining == planned.
func splitSec(elapsed, planned time.Duration) (eSec, rSec, pSec int) {
	if planned < 0 {
		planned = 0
	}
	if elapsed < 0 {
		elapsed = 0
	}
	if elapsed > planned {
		elapsed = planned
	}
	pSec = int(planned / time.Second)
	eSec = int(elapsed / time.Second)
	if eSec > pSec {
		eSec = pSec
	}
	rSec = pSec - eSec
	return eSec, rSec, pSec
}

func kioskStations(f store.File, st engine.Status, prog engine.RunProgressSnap, running bool, view soil.View) []kioskStation {
	on := map[string]bool{}
	for _, id := range st.StationsOn {
		on[id] = true
	}
	pending := map[string]bool{}
	if running {
		for _, step := range prog.Steps {
			if step.State == engine.StepPending {
				pending[step.StationID] = true
			}
		}
	}
	show := soilShowBars(view)
	pct := map[string]int{}
	if show {
		for _, z := range view.Zones {
			if z.Percent != nil {
				pct[z.StationID] = *z.Percent
			}
		}
	}
	out := make([]kioskStation, 0, len(f.Stations))
	for _, stn := range f.Stations {
		title := stn.Title
		if title == "" {
			title = stn.ID
		}
		state := "idle"
		if on[stn.ID] {
			state = "running"
		} else if pending[stn.ID] {
			state = "queued"
		}
		var soilPct *int
		if show {
			if n, ok := pct[stn.ID]; ok {
				n := n
				soilPct = &n
			}
		}
		out = append(out, kioskStation{
			ID:              stn.ID,
			Title:           title,
			Color:           stn.Color,
			On:              on[stn.ID],
			State:           state,
			RainPauseExempt: stn.RainPauseExempt,
			SoilPercent:     soilPct,
		})
	}
	return out
}

func stationTitles(stations []engine.StationConfig) map[string]string {
	out := make(map[string]string, len(stations))
	for _, st := range stations {
		out[st.ID] = st.Title
	}
	return out
}

func soilFromView(v soil.View) kioskSoil {
	return kioskSoil{
		Enabled:   v.Enabled,
		ETKnown:   v.ETKnown,
		ETStale:   v.ETStale,
		ShowBars:  soilShowBars(v),
		UpdatedAt: v.UpdatedAt,
	}
}

// soilShowBars is true only when the estimate is on, ET is known and fresh,
// and at least one zone has a percent. Percents are hidden when this is false.
func soilShowBars(v soil.View) bool {
	if !v.Enabled || !v.ETKnown || v.ETStale {
		return false
	}
	for _, z := range v.Zones {
		if z.Percent != nil {
			return true
		}
	}
	return false
}
