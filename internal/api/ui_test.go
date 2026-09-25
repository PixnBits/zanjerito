package api

import (
	"net/http"
	"strings"
	"testing"
)

func TestUIHomeEmbedded(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/", nil)
	if rr.Code != 200 {
		t.Fatalf("GET / %d %s", rr.Code, rr.Body.String())
	}
	ct := rr.Header().Get("Content-Type")
	if !strings.Contains(ct, "text/html") && !strings.Contains(rr.Body.String(), "<!DOCTYPE html>") {
		t.Fatalf("want html, ct=%q", ct)
	}
	body := rr.Body.String()
	for _, need := range []string{
		"STOP", "Stations", "Schedules", "[1, 5, 10]", "America/Phoenix", "Start anyway", "esc(",
		"Out of season", "Year-round", "starts_on", "ends_on", "Duplicate", "sched-dlg", "collide-warn",
		// Polish v1 Style A chrome
		"wordmark", "--sand", "--terracotta", "--teal", "station-tile", "desert-art", "+ Add program", "stop-bar",
		"Pause for rain", "pause-dlg", "pause-resume", "--plum",
		"Until tomorrow morning", "2 days", "1 week", "Until further notice", "Pick days…",
		"pause-stepper", "pause-days-minus", "pause-days-plus",
		"PAUSE_MAX_DAYS = 14", "PAUSE_MIN_DAYS = 1", "tomorrow_morning", "paused_label",
	} {
		if !strings.Contains(body, need) {
			t.Fatalf("ui missing %q", need)
		}
	}
	for _, leak := range []string{"graphql", "GraphiQL", "PT3M", "cron"} {
		if strings.Contains(strings.ToLower(body), strings.ToLower(leak)) {
			t.Fatalf("ui must not contain %q", leak)
		}
	}
	for _, old := range []string{"5 min", "30 min"} {
		if strings.Contains(body, old) {
			t.Fatalf("old pause chip %q must be gone", old)
		}
	}
	if strings.Contains(body, "datetime-local") {
		t.Fatal("datetime-local input not allowed")
	}
	start := strings.Index(body, `id="pause-dlg"`)
	end := strings.Index(body, `id="paused-block-dlg"`)
	if start < 0 || end <= start {
		t.Fatal("pause dialog bounds")
	}
	pauseHTML := body[start:end]
	for _, bad := range []string{`type="date"`, `type="time"`, "datetime-local"} {
		if strings.Contains(pauseHTML, bad) {
			t.Fatalf("pause sheet must not contain %q", bad)
		}
	}
}

func TestUIKioskStub(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/?mode=kiosk", nil)
	if rr.Code != 200 {
		t.Fatalf("kiosk %d", rr.Code)
	}
	body := rr.Body.String()
	if !strings.Contains(body, "mode") && !strings.Contains(body, "kiosk") {
		t.Fatal("kiosk stub missing")
	}
	if !strings.Contains(body, ".kiosk #page-schedules") {
		t.Fatal("kiosk must still hide schedules page")
	}
}

func TestAPIStillOnSameMux(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/api/status", nil)
	if rr.Code != 200 || !strings.Contains(rr.Body.String(), "Idle") {
		t.Fatalf("status %d %s", rr.Code, rr.Body.String())
	}
}
