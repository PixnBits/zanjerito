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
	for _, need := range []string{"STOP", "Stations", "Schedules", "[1, 5, 10]", "America/Phoenix", "Start anyway"} {
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

func TestUIKioskStub(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/?mode=kiosk", nil)
	if rr.Code != 200 {
		t.Fatalf("kiosk %d", rr.Code)
	}
	if !strings.Contains(rr.Body.String(), "mode") && !strings.Contains(rr.Body.String(), "kiosk") {
		t.Fatal("kiosk stub missing")
	}
}

func TestAPIStillOnSameMux(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/api/status", nil)
	if rr.Code != 200 || !strings.Contains(rr.Body.String(), "Idle") {
		t.Fatalf("status %d %s", rr.Code, rr.Body.String())
	}
}
