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
}

func TestUIKioskDensity(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/?mode=kiosk", nil)
	if rr.Code != 200 {
		t.Fatalf("kiosk %d", rr.Code)
	}
	body := rr.Body.String()
	for _, need := range []string{
		`get("mode") === "kiosk"`,
		"document.documentElement.classList.add(\"kiosk\")",
		".kiosk .edit, .kiosk #page-schedules",
		".kiosk .status .phase",
		".kiosk .stop-bar .stop",
		".kiosk .station-tile",
		"min-height: 88px",
		"min-height: 168px",
		"isPausedStatus",
		`return "Paused"`,
		`status").classList.toggle("paused"`,
		"direction-d style-a",
	} {
		if !strings.Contains(body, need) {
			t.Fatalf("kiosk density missing marker %q", need)
		}
	}
	// Household path still must not leak cron / GraphiQL / ISO durations
	for _, leak := range []string{"graphql", "GraphiQL", "PT5M", "PT3M"} {
		if strings.Contains(body, leak) {
			t.Fatalf("kiosk ui must not contain %q", leak)
		}
	}
}

func TestAPIStillOnSameMux(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/api/status", nil)
	if rr.Code != 200 || !strings.Contains(rr.Body.String(), "Idle") {
		t.Fatalf("status %d %s", rr.Code, rr.Body.String())
	}
}
