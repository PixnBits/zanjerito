package api

import (
	"context"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"net/http"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/schedule"
	"github.com/PixnBits/zanjerito/internal/store"
)

//go:embed ui/*
var uiFS embed.FS

// Server is the LAN REST+JSON+SSE surface (D5/D7). No GraphQL.
type Server struct {
	Eng  *engine.Engine
	Path string
	mux  *http.ServeMux
}

func New(e *engine.Engine, path string) *Server {
	s := &Server{Eng: e, Path: path, mux: http.NewServeMux()}
	s.mux.HandleFunc("GET /api/status", s.handleStatus)
	s.mux.HandleFunc("GET /api/stations", s.handleStationsList)
	s.mux.HandleFunc("GET /api/stations/{id}", s.handleStationGet)
	s.mux.HandleFunc("PATCH /api/stations/{id}", s.handleStationPatch)
	s.mux.HandleFunc("POST /api/stations/{id}/run", s.handleStationRun)
	s.mux.HandleFunc("POST /api/run/cancel", s.handleCancel)
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
	return http.StatusBadRequest
}

func (s *Server) load() (store.File, error) {
	return store.Load(s.Path)
}

func (s *Server) handleStatus(w http.ResponseWriter, _ *http.Request) {
	st := s.Eng.Status()
	loc, err := time.LoadLocation(schedule.Phoenix)
	if err != nil {
		loc = time.UTC
	}
	writeJSON(w, http.StatusOK, map[string]any{
		"now":             time.Now().In(loc).Format(time.RFC3339),
		"timezone":        schedule.Phoenix,
		"phase":           st.Phase,
		"current_station": st.CurrentStation,
		"stations_on":     st.StationsOn,
		"last_error":      st.LastError,
		"lockout":         false,
	})
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
	writeStatusEvent(w, fl, s.Eng.Status())
	tick := time.NewTicker(time.Second)
	defer tick.Stop()
	for {
		select {
		case <-r.Context().Done():
			return
		case <-tick.C:
			writeStatusEvent(w, fl, s.Eng.Status())
		}
	}
}

func writeStatusEvent(w http.ResponseWriter, fl http.Flusher, st engine.Status) {
	b, err := json.Marshal(st)
	if err != nil {
		return
	}
	fmt.Fprintf(w, "event: status\ndata: %s\n\n", b)
	fl.Flush()
}
