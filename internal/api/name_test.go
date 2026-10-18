package api

import (
	"context"
	"encoding/json"
	"net/http"
	"strings"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
)

func assertNoNotesKey(t *testing.T, raw string) {
	t.Helper()
	if strings.Contains(raw, "NOTELEAK-9f3a") {
		t.Fatalf("notes text leaked: %s", raw)
	}
	var v any
	if err := json.Unmarshal([]byte(raw), &v); err != nil {
		t.Fatal(err)
	}
	var walk func(any)
	walk = func(x any) {
		switch n := x.(type) {
		case map[string]any:
			if _, ok := n["notes"]; ok {
				t.Fatalf("notes key present: %s", raw)
			}
			for _, child := range n {
				walk(child)
			}
		case []any:
			for _, child := range n {
				walk(child)
			}
		}
	}
	walk(v)
}

func TestScheduleNameLimitAndNotesHidden(t *testing.T) {
	s := newTestServer(t)
	name24 := strings.Repeat("a", 23) + "·"
	if len([]rune(name24)) != 24 {
		t.Fatalf("rune count %d", len([]rune(name24)))
	}
	body := map[string]any{
		"id": "lawn", "enabled": true, "name": name24,
		"notes": "NOTELEAK-9f3a", "note": "legacy note",
		"start": "00:00",
		"steps": []map[string]any{{"station_id": "front-north", "minutes": 6}},
	}
	rr := doJSON(t, s, http.MethodPut, "/api/schedules/lawn", body)
	if rr.Code != http.StatusOK {
		t.Fatalf("24-rune name %d %s", rr.Code, rr.Body.String())
	}
	var saved map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &saved); err != nil {
		t.Fatal(err)
	}
	if saved["name"] != name24 {
		t.Fatalf("name %v", saved["name"])
	}

	body["name"] = name24 + "b"
	rr = doJSON(t, s, http.MethodPut, "/api/schedules/lawn", body)
	if rr.Code != http.StatusBadRequest {
		t.Fatalf("25-rune name want 400, got %d %s", rr.Code, rr.Body.String())
	}

	rr = doJSON(t, s, http.MethodGet, "/api/kiosk", nil)
	if rr.Code != 200 {
		t.Fatalf("kiosk %d %s", rr.Code, rr.Body.String())
	}
	kioskBody := rr.Body.String()
	assertNoNotesKey(t, kioskBody)
	var kiosk map[string]any
	if err := json.Unmarshal([]byte(kioskBody), &kiosk); err != nil {
		t.Fatal(err)
	}
	next, _ := kiosk["next_run"].(map[string]any)
	if next == nil || next["name"] != name24 {
		t.Fatalf("next_run name %v", kiosk["next_run"])
	}
	eff, _ := kiosk["next_effective_run"].(map[string]any)
	if eff == nil || eff["name"] != name24 {
		t.Fatalf("effective name %v", kiosk["next_effective_run"])
	}

	rr = doJSON(t, s, http.MethodGet, "/api/ha", nil)
	if rr.Code != 200 {
		t.Fatalf("ha %d %s", rr.Code, rr.Body.String())
	}
	assertNoNotesKey(t, rr.Body.String())

	done := make(chan error, 1)
	go func() {
		done <- s.Eng.RunProgram(context.Background(), "lawn", name24, []engine.Step{
			{StationID: "front-north", Duration: time.Second},
		})
	}()
	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) && s.Eng.Status().Phase == engine.PhaseIdle {
		time.Sleep(5 * time.Millisecond)
	}
	rr = doJSON(t, s, http.MethodGet, "/api/kiosk", nil)
	assertNoNotesKey(t, rr.Body.String())
	if err := json.Unmarshal(rr.Body.Bytes(), &kiosk); err != nil {
		t.Fatal(err)
	}
	run, _ := kiosk["run"].(map[string]any)
	if run == nil || run["program"] != name24 {
		t.Fatalf("run.program %v", kiosk["run"])
	}
	_ = s.Eng.Stop()
	<-done
}

func TestScheduleEmptyNameRejectedAndLegacyNoteKept(t *testing.T) {
	s := newTestServer(t)
	steps := []map[string]any{{"station_id": "front-north", "minutes": 6}}
	rr := doJSON(t, s, http.MethodPut, "/api/schedules/lawn", map[string]any{
		"id": "lawn", "enabled": true, "note": "legacy note", "start": "06:00", "steps": steps,
	})
	if rr.Code != http.StatusOK {
		t.Fatalf("absent name %d %s", rr.Code, rr.Body.String())
	}
	saved := decodeMap(t, rr)
	if saved["note"] != "legacy note" {
		t.Fatalf("note %v", saved["note"])
	}
	if name, ok := saved["name"]; ok && name != "" {
		t.Fatalf("absent name stored %v", name)
	}

	rr = doJSON(t, s, http.MethodPut, "/api/schedules/lawn", map[string]any{
		"id": "lawn", "enabled": false, "start": "07:15", "steps": steps,
	})
	if rr.Code != http.StatusOK {
		t.Fatalf("second absent name %d %s", rr.Code, rr.Body.String())
	}
	saved = decodeMap(t, rr)
	if saved["note"] != "legacy note" || saved["start"] != "07:15" || saved["enabled"] != false {
		t.Fatalf("note not preserved %+v", saved)
	}
	rr = doJSON(t, s, http.MethodGet, "/api/schedules/lawn", nil)
	saved = decodeMap(t, rr)
	if saved["note"] != "legacy note" {
		t.Fatalf("loaded note %v", saved["note"])
	}

	for _, name := range []string{"", " ", "\t", " \n "} {
		rr = doJSON(t, s, http.MethodPut, "/api/schedules/lawn", map[string]any{
			"id": "lawn", "enabled": true, "name": name, "note": "legacy note",
			"start": "06:00", "steps": steps,
		})
		if rr.Code != http.StatusBadRequest || !strings.Contains(rr.Body.String(), "name is required") {
			t.Fatalf("name %q want 400 name is required, got %d %s", name, rr.Code, rr.Body.String())
		}
	}
	rr = doJSON(t, s, http.MethodGet, "/api/schedules/lawn", nil)
	saved = decodeMap(t, rr)
	if saved["note"] != "legacy note" {
		t.Fatalf("rejected put changed note %v", saved["note"])
	}
}
