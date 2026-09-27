package api

import (
	"net/http"
	"strings"
	"testing"
)

func TestUIHistoryHooks(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/", nil)
	if rr.Code != 200 {
		t.Fatalf("GET / %d", rr.Code)
	}
	body := rr.Body.String()
	for _, need := range []string{
		"Recent runs", "See all", "page-history", "loadHistory",
		"historyLoaded", "historyInFlight", "No runs yet.",
	} {
		if !strings.Contains(body, need) {
			t.Fatalf("ui missing %q", need)
		}
	}
	if strings.Contains(body, "setInterval(loadHistory") {
		t.Fatal("ui must not poll history on its own interval")
	}
}
