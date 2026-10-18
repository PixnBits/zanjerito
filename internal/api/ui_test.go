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
		"Out of season", "All year", "starts_on", "ends_on", "Duplicate", "sched-dlg", "collide-warn",
		// Polish v1 Style A chrome
		"wordmark", "--sand", "--terracotta", "--teal", "station-tile", "desert-art", "+ Add program", "stop-bar",
		"Pause for rain", "pause-dlg", "pause-resume", "--plum",
		"Until tomorrow morning", "2 days", "1 week", "Until further notice", "Pick days…",
		"pause-stepper", "pause-days-minus", "pause-days-plus",
		"PAUSE_MAX_DAYS = 14", "PAUSE_MIN_DAYS = 1", "tomorrow_morning", "paused_label",
		"Paused for rain", "Rain data unavailable", "pause_source", "rain_pause_exempt",
		"formatRainInches", "rain-hint",
		`sub.textContent = ""; // paused: the banner and the Next line already say it`,
		"Rain settings file has an error",
		"Rain data unavailable since",
		"stampPhrase",
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

// The Next label must fall back to the in-page scan when /api/kiosk is malformed or hangs.
// There is no JS test harness for index.html, so this pins the guards as UI strings.
func TestUIKioskNextGuards(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/", nil)
	body := rr.Body.String()
	for _, need := range []string{
		"function kioskNextFrom(d)",
		`hasOwnProperty.call(d, "next_run")`,
		"new AbortController()",
		"ctl.abort(), 3000",
		"signal: ctl.signal",
		"kioskNextBusy",
		"if (k.ok) {",
	} {
		if !strings.Contains(body, need) {
			t.Fatalf("loadKioskNext guard missing %q", need)
		}
	}
	if strings.Contains(body, "state.kioskNext = d ? d.next_run : null") {
		t.Fatal("unguarded kiosk next assignment must be gone")
	}
}

func TestUIHomeRedesignCopy(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/", nil)
	body := rr.Body.String()
	for _, need := range []string{
		"Show older runs",
		"Change end time",
		"Run ${name} anyway?",
		"#B5361A",
		"#7C2411",
		"+ Add program",
	} {
		if !strings.Contains(body, need) {
			t.Fatalf("ui missing %q", need)
		}
	}
	if strings.Contains(body, ">Idle<") || strings.Contains(body, `return "Idle"`) {
		t.Fatal("hero must not render Idle")
	}
	i := strings.Index(body, `<footer class="stop-bar"`)
	j := strings.Index(body, `</footer>`)
	if i < 0 || j < i {
		t.Fatal("stop bar missing")
	}
	if strings.Contains(body[i:j], "+ Add program") {
		t.Fatal("+ Add program must not sit in the bottom bar")
	}
}

func TestUINameRequiredAndRunDay(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/", nil)
	body := rr.Body.String()
	for _, need := range []string{
		"Name is required.",
		"ed-name-error",
		"function editorName(",
		"note: state.edit.note",
		`aria-label="${DAY_NAME[i]}"`,
		"const day = historyDay(d)",
		"Add station",
		"art-dune",
		"art-waves",
		"timer-text",
		"Show older runs ›",
		"run-chev",
		"time-pill",
	} {
		if !strings.Contains(body, need) {
			t.Fatalf("ui missing %q", need)
		}
	}
	if strings.Contains(body, "note: name") {
		t.Fatal("editor must not copy the name into the legacy note")
	}
}

func TestUIStopConfirm(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/", nil)
	if rr.Code != 200 {
		t.Fatalf("GET / %d", rr.Code)
	}
	body := rr.Body.String()
	for _, need := range []string{
		"Stop all watering?",
		"Stop watering",
		"Nothing is watering right now. This turns every valve off.",
		"and every other valve now.",
		`id="stop-sheet"`,
		`id="stop-confirm"`,
		`id="stop-cancel"`,
		`aria-modal="true"`,
		`aria-labelledby="stop-sheet-title"`,
		`role="dialog"`,
	} {
		if !strings.Contains(body, need) {
			t.Fatalf("ui missing %q", need)
		}
	}
	if strings.Contains(body, "<dialog") || strings.Contains(body, "</dialog>") {
		t.Fatal("native dialog element remains")
	}
	if strings.Contains(body, "showModal(") {
		t.Fatal("showModal remains")
	}
	if strings.Contains(body, "window.confirm") {
		t.Fatal("window.confirm remains")
	}
	if strings.Contains(body, "http://") || strings.Contains(body, "https://") {
		t.Fatal("external URL in served UI")
	}
	if strings.Count(body, "background: #B5361A") != 2 {
		t.Fatalf("filled #B5361A backgrounds = %d, want 2 (bottom-bar STOP and stop confirm)", strings.Count(body, "background: #B5361A"))
	}
	ruleAt := strings.Index(body, "#stop-confirm")
	if ruleAt < 0 {
		t.Fatal("stop confirm rule missing")
	}
	rule := body[ruleAt:]
	end := strings.Index(rule, "}")
	if end < 0 || !strings.Contains(rule[:end], "background: #B5361A") {
		t.Fatal("#stop-confirm is not filled #B5361A")
	}
	stopAt := strings.Index(body, `$("stop").addEventListener`)
	confAt := strings.Index(body, `$("stop-confirm").addEventListener`)
	if stopAt < 0 || confAt < stopAt {
		t.Fatal("stop listeners missing")
	}
	if strings.Contains(body[stopAt:confAt], "/api/run/cancel") {
		t.Fatal("STOP click handler posts /api/run/cancel")
	}
	tail := body[confAt:]
	next := strings.Index(tail, `$("pause-chips")`)
	if next < 0 {
		t.Fatal("confirm handler end missing")
	}
	if !strings.Contains(tail[:next], "/api/run/cancel") {
		t.Fatal("confirm button must POST /api/run/cancel")
	}
	if strings.Count(body, "/api/run/cancel") != 1 {
		t.Fatalf("/api/run/cancel count = %d, want 1", strings.Count(body, "/api/run/cancel"))
	}
}
