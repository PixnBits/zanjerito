package api

import (
	"context"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"math"
	"net/http"
	"strconv"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/history"
	"github.com/PixnBits/zanjerito/internal/rain"
	"github.com/PixnBits/zanjerito/internal/schedule"
	"github.com/PixnBits/zanjerito/internal/soil"
	"github.com/PixnBits/zanjerito/internal/store"
)

//go:embed ui/*
var uiFS embed.FS

// Server is the LAN REST+JSON+SSE surface (D5/D7). No GraphQL.
type Server struct {
	Eng     *engine.Engine
	Path    string
	History *history.Log
	// Rain is nil when automatic rain pause is disabled.
	Rain *rain.Poller
	// Soil is nil when the display-only soil estimate is disabled or the file is invalid.
	Soil *soil.Poller
	// SoilConfigErr is a short parse message when soil.local.json exists but is invalid.
	// Empty when the file is missing, disabled, or the estimate is running.
	// It must not contain a station id, URL, or filesystem path.
	SoilConfigErr string
	mux           *http.ServeMux
}

func New(e *engine.Engine, path string) *Server {
	s := &Server{Eng: e, Path: path, mux: http.NewServeMux()}
	s.mux.HandleFunc("GET /api/status", s.handleStatus)
	s.mux.HandleFunc("GET /api/stations", s.handleStationsList)
	s.mux.HandleFunc("GET /api/stations/{id}", s.handleStationGet)
	s.mux.HandleFunc("PATCH /api/stations/{id}", s.handleStationPatch)
	s.mux.HandleFunc("POST /api/stations/{id}/run", s.handleStationRun)
	s.mux.HandleFunc("POST /api/run/cancel", s.handleCancel)
	s.mux.HandleFunc("POST /api/pause", s.handlePauseSet)
	s.mux.HandleFunc("DELETE /api/pause", s.handlePauseClear)
	s.mux.HandleFunc("POST /api/pause/resume", s.handlePauseClear)
	s.mux.HandleFunc("GET /api/history", s.handleHistory)
	s.mux.HandleFunc("GET /api/soil", s.handleSoil)
	s.mux.HandleFunc("GET /api/schedules", s.handleSchedulesList)
	s.mux.HandleFunc("GET /api/schedules/{id}", s.handleScheduleGet)
	s.mux.HandleFunc("PUT /api/schedules/{id}", s.handleSchedulePut)
	s.mux.HandleFunc("GET /api/events", s.handleEvents)
	mountUI(s.mux)
	return s
}

func mountUI(mux *http.ServeMux) {
	sub, err := fs.Sub(uiFS, "ui")
	if err != nil {
		return
	}
	h := http.FileServer(http.FS(sub))
	mux.Handle("GET /{$}", h)
	mux.Handle("GET /index.html", h)
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	s.mux.ServeHTTP(w, r)
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(v)
}

func writeErr(w http.ResponseWriter, code int, err error) {
	writeJSON(w, code, map[string]string{"error": err.Error()})
}

func busyCode(err error) int {
	if errors.Is(err, engine.ErrBusy) {
		return http.StatusConflict
	}
	if errors.Is(err, engine.ErrPaused) {
		return http.StatusConflict
	}
	return http.StatusBadRequest
}

func (s *Server) load() (store.File, error) {
	return store.Load(s.Path)
}

// location is the config timezone, then Phoenix, then UTC.
func (s *Server) location() *time.Location {
	tz := schedule.Phoenix
	if f, err := store.Load(s.Path); err == nil && f.Timezone != "" {
		tz = f.Timezone
	}
	loc, err := time.LoadLocation(tz)
	if err != nil {
		loc, err = time.LoadLocation(schedule.Phoenix)
		if err != nil {
			return time.UTC
		}
	}
	return loc
}

// pauseFields is the pause, rain, and rain_strip object shared by status, SSE,
// and pause writes. Existing keys (paused, paused_until, paused_label, reason) stay.
func (s *Server) pauseFields(now time.Time) (loc *time.Location, fields map[string]any) {
	loc = s.location()
	d := s.Eng.PauseDetail(now)
	label := schedule.PauseLabel(d.Paused, d.Until, now, loc)
	var untilStr any
	if d.Paused && d.Until != nil {
		untilStr = d.Until.In(loc).Format(time.RFC3339)
	}
	var lastRain any
	if d.Paused && d.LastRainAt != nil {
		lastRain = d.LastRainAt.In(loc).Format(time.RFC3339)
	}
	src := ""
	inches := 0.0
	if d.Paused {
		src = d.Source
		if src == "" {
			src = engine.PauseSourceManual
		}
		inches = rain.RoundInches(d.RainInches)
	}
	if d.AutoRain() {
		label = schedule.RainPauseLabel(inches, d.Until, now, loc)
	}
	rainMap, strip := s.rainBody(now, loc, d.AutoRain())
	fields = map[string]any{
		"paused":       d.Paused,
		"paused_until": untilStr,
		"paused_label": label,
		"reason":       d.Reason,
		"pause_source": src,
		"rain_inches":  inches,
		"last_rain_at": lastRain,
		"rain":         rainMap,
		"rain_strip":   strip,
	}
	return loc, fields
}

func (s *Server) rainBody(now time.Time, loc *time.Location, rainPaused bool) (map[string]any, rain.RainStrip) {
	out := map[string]any{"enabled": false, "unavailable": false}
	if s.Rain == nil {
		return out, rain.HomeStrip(false, false, rainPaused, false, 0, 0)
	}
	st := s.Rain.Status()
	out["enabled"] = st.Enabled
	out["unavailable"] = st.Unavailable
	if st.LastError != "" {
		out["last_error"] = st.LastError
	}
	if st.LastOK != nil {
		if loc == nil {
			loc = time.UTC
		}
		out["last_ok_at"] = st.LastOK.In(loc).Format(time.RFC3339)
	}
	if st.HaveTotal {
		out["last_total_inches"] = rain.RoundInches(st.LastTotal)
	}
	in72, ok := s.Rain.TotalSince(72*time.Hour, now)
	var in24 float64
	if ok {
		var ok24 bool
		in24, ok24 = s.Rain.TotalSince(24*time.Hour, now)
		ok = ok24
	}
	if ok {
		out["total_72h_inches"] = rain.RoundInches(in72)
		out["total_24h_inches"] = rain.RoundInches(in24)
	}
	return out, rain.HomeStrip(st.Enabled, st.Unavailable, rainPaused, ok, in24, in72)
}

func (s *Server) handleStatus(w http.ResponseWriter, _ *http.Request) {
	st := s.Eng.Status()
	now := time.Now()
	loc, fields := s.pauseFields(now)
	// Persist auto-expire so disk matches memory after timed pause ends.
	// Keep rain clear memory; do not wipe rain_cleared_at.
	// A rain poller writes through its own lock and must not stall this handler.
	if fields["paused"] != true {
		if s.Rain != nil {
			s.Rain.SyncExpiredPause(now)
		} else if p, err := store.LoadPause(s.Path, now); err == nil && p.Active && !s.Eng.IsPaused(now) {
			_ = store.SavePause(s.Path, p.WithoutActive())
		}
	}
	out := map[string]any{
		"now":             now.In(loc).Format(time.RFC3339),
		"timezone":        loc.String(),
		"phase":           st.Phase,
		"current_station": st.CurrentStation,
		"stations_on":     st.StationsOn,
		"last_error":      st.LastError,
		"lockout":         false,
	}
	for k, v := range fields {
		out[k] = v
	}
	writeJSON(w, http.StatusOK, out)
}

// historyStationJSON is one step in GET /api/history.
// planned_min and actual_min are whole minutes: seconds/60, rounded half away from zero.
type historyStationJSON struct {
	StationID  string `json:"station_id"`
	PlannedSec int    `json:"planned_sec"`
	ActualSec  int    `json:"actual_sec"`
	PlannedMin int    `json:"planned_min"`
	ActualMin  int    `json:"actual_min"`
}

type historyEntryJSON struct {
	ID        string               `json:"id"`
	ProgramID string               `json:"program_id"`
	Program   string               `json:"program"`
	Kind      string               `json:"kind"`
	Stations  []historyStationJSON `json:"stations"`
	StartedAt string               `json:"started_at"`
	EndedAt   string               `json:"ended_at"`
	Outcome   string               `json:"outcome"` // completed, stopped, skipped, refused, or error
	Error     string               `json:"error,omitempty"`
	Reason    string               `json:"reason,omitempty"`
}

func roundedMin(sec int) int {
	if sec <= 0 {
		return 0
	}
	return int(math.Round(float64(sec) / 60))
}

// handleHistory returns newest-first runs. Times are RFC3339 in the config timezone,
// same as /api/status. Default limit is 50; values above history.MaxEntries are clamped.
// limit is validated before the nil-log shortcut, so a server with no history log
// still rejects a non-integer or non-positive limit. Outcomes are completed,
// stopped, skipped, refused, or error.
func (s *Server) handleHistory(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Cache-Control", "no-store")
	limit, err := historyLimit(r.URL.Query().Get("limit"))
	if err != nil {
		writeErr(w, http.StatusBadRequest, err)
		return
	}
	if s.History == nil {
		writeJSON(w, http.StatusOK, map[string]any{"entries": []historyEntryJSON{}})
		return
	}
	loc := s.location()
	src := s.History.List(limit)
	entries := make([]historyEntryJSON, 0, len(src))
	for _, e := range src {
		stations := make([]historyStationJSON, 0, len(e.Stations))
		for _, st := range e.Stations {
			stations = append(stations, historyStationJSON{
				StationID:  st.StationID,
				PlannedSec: st.PlannedSec,
				ActualSec:  st.ActualSec,
				PlannedMin: roundedMin(st.PlannedSec),
				ActualMin:  roundedMin(st.ActualSec),
			})
		}
		entries = append(entries, historyEntryJSON{
			ID:        e.ID,
			ProgramID: e.ProgramID,
			Program:   e.Program,
			Kind:      e.Kind,
			Stations:  stations,
			StartedAt: e.StartedAt.In(loc).Format(time.RFC3339),
			EndedAt:   e.EndedAt.In(loc).Format(time.RFC3339),
			Outcome:   e.Outcome,
			Error:     e.Error,
			Reason:    e.Reason,
		})
	}
	writeJSON(w, http.StatusOK, map[string]any{"entries": entries})
}

// NoteSoil records a soil.Start result. A missing file leaves Soil nil and
// SoilConfigErr empty. An invalid file keeps Soil nil and sets SoilConfigErr.
func (s *Server) NoteSoil(p *soil.Poller, err error) {
	if s == nil {
		return
	}
	s.Soil = nil
	s.SoilConfigErr = ""
	if err == nil {
		s.Soil = p
		return
	}
	s.SoilConfigErr = soil.PublicConfigError(err)
}

func (s *Server) handleSoil(w http.ResponseWriter, _ *http.Request) {
	w.Header().Set("Cache-Control", "no-store")
	loc := s.location()
	now := time.Now()
	if s.Soil != nil && s.Soil.Now != nil {
		now = s.Soil.Now()
	}
	if s.Soil == nil {
		v := soil.DisabledView("no soil.local.json", now, loc)
		if msg := soil.SanitizeConfigMessage(s.SoilConfigErr); msg != "" {
			v.Reason = "soil.local.json invalid"
			v.ConfigError = msg
		}
		writeJSON(w, http.StatusOK, v)
		return
	}
	var stations []engine.StationConfig
	if f, err := s.load(); err == nil {
		stations = f.Stations
	}
	hist := s.History.List(0)
	rainDays, rainOK := s.Rain.DailyRain(loc)
	writeJSON(w, http.StatusOK, s.Soil.View(stations, hist, rainDays, rainOK, now, loc))
}

func historyLimit(raw string) (int, error) {
	if raw == "" {
		return 50, nil
	}
	n, err := strconv.Atoi(raw)
	if err != nil || n < 1 {
		return 0, fmt.Errorf("limit must be a positive integer")
	}
	if n > history.MaxEntries {
		n = history.MaxEntries
	}
	return n, nil
}

func (s *Server) handleStationsList(w http.ResponseWriter, _ *http.Request) {
	f, err := s.load()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, map[string]any{"stations": f.Stations})
}

func (s *Server) handleStationGet(w http.ResponseWriter, r *http.Request) {
	f, err := s.load()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err)
		return
	}
	id := r.PathValue("id")
	for _, st := range f.Stations {
		if st.ID == id {
			writeJSON(w, http.StatusOK, st)
			return
		}
	}
	writeErr(w, http.StatusNotFound, fmt.Errorf("station %s not found", id))
}

func (s *Server) handleStationPatch(w http.ResponseWriter, r *http.Request) {
	var patch engine.StationConfig
	if err := json.NewDecoder(r.Body).Decode(&patch); err != nil {
		writeErr(w, http.StatusBadRequest, err)
		return
	}
	f, err := s.load()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err)
		return
	}
	id := r.PathValue("id")
	found := false
	for i := range f.Stations {
		if f.Stations[i].ID != id {
			continue
		}
		found = true
		if patch.Title != "" {
			f.Stations[i].Title = patch.Title
		}
		if patch.Color != "" {
			f.Stations[i].Color = patch.Color
		}
		if patch.BCM != 0 {
			f.Stations[i].BCM = patch.BCM
		}
		break
	}
	if !found {
		writeErr(w, http.StatusNotFound, fmt.Errorf("station %s not found", id))
		return
	}
	if err := store.ApplyAndSave(s.Eng, s.Path, f.Config); err != nil {
		writeErr(w, busyCode(err), err)
		return
	}
	s.handleStationGet(w, r)
}

func (s *Server) handleStationRun(w http.ResponseWriter, r *http.Request) {
	var body struct {
		DurationSec int  `json:"durationSec"`
		Preempt     bool `json:"preempt"`
	}
	_ = json.NewDecoder(r.Body).Decode(&body)
	if body.DurationSec <= 0 {
		writeErr(w, http.StatusBadRequest, fmt.Errorf("durationSec required"))
		return
	}
	id := r.PathValue("id")
	f, err := s.load()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err)
		return
	}
	known := false
	for _, st := range f.Stations {
		if st.ID == id {
			known = true
			break
		}
	}
	if !known {
		writeErr(w, http.StatusNotFound, fmt.Errorf("station %s not found", id))
		return
	}
	if s.Eng.IsPaused(time.Now()) {
		d := s.Eng.PauseDetail(time.Now())
		if !(d.AutoRain() && s.Eng.StationRainExempt(id)) {
			writeErr(w, http.StatusConflict, fmt.Errorf("%w: pause for rain — resume before running a station", engine.ErrPaused))
			return
		}
	}
	st := s.Eng.Status()
	if st.Phase != engine.PhaseIdle && !body.Preempt {
		writeErr(w, http.StatusConflict, fmt.Errorf("%w: already watering", engine.ErrBusy))
		return
	}
	if body.Preempt && st.Phase != engine.PhaseIdle {
		_ = s.Eng.Stop()
	}
	step := engine.Step{StationID: id, Duration: time.Duration(body.DurationSec) * time.Second}
	if err := s.startRun(step, body.Preempt); err != nil {
		writeErr(w, busyCode(err), err)
		return
	}
	writeJSON(w, http.StatusAccepted, s.Eng.Status())
}

// startRun launches RunItinerary and returns only after it is accepted
// (or fails). retryBusy waits out a just-stopped itinerary still holding running.
func (s *Server) startRun(step engine.Step, retryBusy bool) error {
	deadline := time.Now().Add(2 * time.Second)
	var last error
	for {
		last = s.tryStart(step)
		if last == nil || !retryBusy || !errors.Is(last, engine.ErrBusy) {
			return last
		}
		if !time.Now().Before(deadline) {
			return last
		}
		time.Sleep(10 * time.Millisecond)
	}
}

func (s *Server) tryStart(step engine.Step) error {
	errc := make(chan error, 1)
	go func() {
		errc <- s.Eng.RunItinerary(context.Background(), []engine.Step{step})
	}()
	// ErrBusy / validation return immediately. Do not treat leftover
	// Phase!=Idle from the prior run as acceptance.
	timer := time.NewTimer(50 * time.Millisecond)
	defer timer.Stop()
	select {
	case err := <-errc:
		return err
	case <-timer.C:
		if s.Eng.Status().Phase != engine.PhaseIdle {
			return nil
		}
		select {
		case err := <-errc:
			return err
		case <-time.After(200 * time.Millisecond):
			if s.Eng.Status().Phase != engine.PhaseIdle {
				return nil
			}
			return fmt.Errorf("%w: run did not start", engine.ErrBusy)
		}
	}
}

func (s *Server) handleCancel(w http.ResponseWriter, _ *http.Request) {
	if err := s.Eng.Stop(); err != nil {
		writeErr(w, http.StatusInternalServerError, err)
		return
	}
	writeJSON(w, http.StatusOK, s.Eng.Status())
}

func (s *Server) handleSchedulesList(w http.ResponseWriter, _ *http.Request) {
	f, err := s.load()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err)
		return
	}
	if f.Schedules == nil {
		f.Schedules = []store.Schedule{}
	}
	writeJSON(w, http.StatusOK, map[string]any{"schedules": f.Schedules})
}

func (s *Server) handleScheduleGet(w http.ResponseWriter, r *http.Request) {
	f, err := s.load()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err)
		return
	}
	id := r.PathValue("id")
	for _, sch := range f.Schedules {
		if sch.ID == id {
			writeJSON(w, http.StatusOK, sch)
			return
		}
	}
	writeErr(w, http.StatusNotFound, fmt.Errorf("schedule %s not found", id))
}

func (s *Server) handleSchedulePut(w http.ResponseWriter, r *http.Request) {
	if s.Eng.Status().Phase != engine.PhaseIdle {
		writeErr(w, http.StatusConflict, fmt.Errorf("%w: schedule writes deferred until Idle", engine.ErrBusy))
		return
	}
	var sch store.Schedule
	if err := json.NewDecoder(r.Body).Decode(&sch); err != nil {
		writeErr(w, http.StatusBadRequest, err)
		return
	}
	sch.ID = r.PathValue("id")
	if sch.ID == "" {
		writeErr(w, http.StatusBadRequest, fmt.Errorf("schedule id required"))
		return
	}
	f, err := s.load()
	if err != nil {
		writeErr(w, http.StatusInternalServerError, err)
		return
	}
	replaced := false
	for i := range f.Schedules {
		if f.Schedules[i].ID == sch.ID {
			f.Schedules[i] = sch
			replaced = true
			break
		}
	}
	if !replaced {
		f.Schedules = append(f.Schedules, sch)
	}
	if err := store.Save(s.Path, f); err != nil {
		writeErr(w, http.StatusBadRequest, err)
		return
	}
	writeJSON(w, http.StatusOK, sch)
}

func (s *Server) handleEvents(w http.ResponseWriter, r *http.Request) {
	fl, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "sse unsupported", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")
	writeStatusEvent(w, fl, s)
	tick := time.NewTicker(time.Second)
	defer tick.Stop()
	for {
		select {
		case <-r.Context().Done():
			return
		case <-tick.C:
			writeStatusEvent(w, fl, s)
		}
	}
}

func writeStatusEvent(w http.ResponseWriter, fl http.Flusher, s *Server) {
	st := s.Eng.Status()
	now := time.Now()
	_, fields := s.pauseFields(now)
	payload := map[string]any{
		"Phase":          st.Phase,
		"CurrentStation": st.CurrentStation,
		"StationsOn":     st.StationsOn,
		"LastError":      st.LastError,
	}
	for k, v := range fields {
		payload[k] = v
	}
	b, err := json.Marshal(payload)
	if err != nil {
		return
	}
	fmt.Fprintf(w, "event: status\ndata: %s\n\n", b)
	fl.Flush()
}

func (s *Server) handlePauseSet(w http.ResponseWriter, r *http.Request) {
	var body struct {
		DurationSec *int   `json:"duration_sec"`
		Days        *int   `json:"days"`
		Until       string `json:"until"`
		Indefinite  bool   `json:"indefinite"`
		Reason      string `json:"reason"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		// empty body → indefinite pause
		if !errors.Is(err, io.EOF) {
			writeErr(w, http.StatusBadRequest, err)
			return
		}
	}
	n := 0
	if body.DurationSec != nil {
		n++
	}
	if body.Days != nil {
		n++
	}
	if body.Until != "" {
		n++
	}
	if body.Indefinite {
		n++
	}
	if n > 1 {
		writeErr(w, http.StatusBadRequest, fmt.Errorf("choose one of duration_sec, days, until, indefinite"))
		return
	}

	now := time.Now()
	loc := s.location()
	var schedules []store.Schedule
	if f, err := store.Load(s.Path); err == nil {
		schedules = f.Schedules
	}

	var until *time.Time
	switch {
	case body.DurationSec != nil:
		if *body.DurationSec <= 0 {
			writeErr(w, http.StatusBadRequest, fmt.Errorf("duration_sec must be positive"))
			return
		}
		u := now.Add(time.Duration(*body.DurationSec) * time.Second).In(loc)
		until = &u
	case body.Days != nil:
		if *body.Days < schedule.MinPauseDays || *body.Days > schedule.MaxPauseDays {
			writeErr(w, http.StatusBadRequest, fmt.Errorf("days must be between 1 and 14"))
			return
		}
		u := schedule.PauseUntilDays(schedules, now, loc, *body.Days)
		until = &u
	case body.Until != "":
		if body.Until == "tomorrow_morning" {
			u := schedule.PauseUntilDays(schedules, now, loc, 1)
			until = &u
		} else {
			u, err := time.Parse(time.RFC3339, body.Until)
			if err != nil {
				writeErr(w, http.StatusBadRequest, fmt.Errorf("until must be RFC3339: %w", err))
				return
			}
			if !u.After(now) {
				writeErr(w, http.StatusBadRequest, fmt.Errorf("until must be in the future"))
				return
			}
			u = u.In(loc)
			until = &u
		}
	default:
		until = nil
	}
	reason := body.Reason
	if reason == "" {
		reason = "rain"
	}
	// Cancel any active run; valves off. STOP remains separate (cancel-only).
	_ = s.Eng.Stop()
	var saveErr error
	if s.Rain != nil {
		saveErr = s.Rain.ManualPause(until, reason, now)
	} else {
		saveErr = rain.RememberManualPause(s.Eng, s.Path, until, reason, now)
	}
	if saveErr != nil {
		writeErr(w, http.StatusInternalServerError, saveErr)
		return
	}
	s.writePauseStatus(w)
}

func (s *Server) handlePauseClear(w http.ResponseWriter, _ *http.Request) {
	now := time.Now()
	var saveErr error
	if s.Rain != nil {
		saveErr = s.Rain.ManualClear(now)
	} else {
		saveErr = rain.RememberManualClear(s.Eng, s.Path, now)
	}
	if saveErr != nil {
		writeErr(w, http.StatusInternalServerError, saveErr)
		return
	}
	s.writePauseStatus(w)
}

func (s *Server) writePauseStatus(w http.ResponseWriter) {
	now := time.Now()
	_, fields := s.pauseFields(now)
	out := map[string]any{
		"phase": s.Eng.Status().Phase,
	}
	for k, v := range fields {
		out[k] = v
	}
	writeJSON(w, http.StatusOK, out)
}
