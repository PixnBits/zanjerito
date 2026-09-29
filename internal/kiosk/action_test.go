package kiosk

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestStopHitPostsOnce(t *testing.T) {
	var posts []string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		b, _ := io.ReadAll(r.Body)
		posts = append(posts, r.Method+" "+r.URL.Path+" "+string(b))
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write([]byte(`{"phase":"Idle"}`))
	}))
	t.Cleanup(srv.Close)

	snap, err := LoadFixture("testdata", "running")
	if err != nil {
		t.Fatal(err)
	}
	lay := NewLayout(len(snap.Stations))
	x, y := mid(lay.Stop)
	h := lay.HitTest(x, y)
	if h != HitStop {
		t.Fatalf("center hit %s", h)
	}
	acts := HTTPActions{BaseURL: srv.URL, HTTP: srv.Client()}
	menu, err := Release(acts, h, h, false, snap.Status.Paused, time.Hour)
	if err != nil || menu {
		t.Fatal(err, menu)
	}
	if len(posts) != 1 || posts[0] != "POST /api/run/cancel " {
		t.Fatalf("posts %#v", posts)
	}

	// An open menu, a tile, and a slide-off must not POST.
	if _, err := Release(acts, HitStop, HitStop, true, false, time.Hour); err != nil {
		t.Fatal(err)
	}
	if _, err := Release(acts, HitTile(0), HitTile(0), false, false, time.Hour); err != nil {
		t.Fatal(err)
	}
	if _, err := Release(acts, HitStop, HitNone, false, false, time.Hour); err != nil {
		t.Fatal(err)
	}
	if len(posts) != 1 {
		t.Fatalf("extra posts %#v", posts)
	}

	var lines []string
	ro := ReadOnlyActions{Logf: func(f string, args ...any) {
		lines = append(lines, fmt.Sprintf(f, args...))
	}}
	if _, err := Release(ro, HitStop, HitStop, false, false, time.Hour); err != nil {
		t.Fatal(err)
	}
	if _, err := Release(ro, HitPause, HitPause, false, false, time.Hour); err != nil {
		t.Fatal(err)
	}
	if _, err := Release(ro, HitPause, HitPause, false, true, time.Hour); err != nil {
		t.Fatal(err)
	}
	if len(posts) != 1 {
		t.Fatalf("readonly reached the stub %#v", posts)
	}
	want := []string{
		"readonly: would POST /api/run/cancel",
		"readonly: would POST /api/pause",
		"readonly: would POST /api/pause/resume",
	}
	if strings.Join(lines, "\n") != strings.Join(want, "\n") {
		t.Fatalf("logs %#v", lines)
	}
}

func TestPauseResumeBody(t *testing.T) {
	type hit struct {
		path string
		body string
	}
	var got []hit
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		b, _ := io.ReadAll(r.Body)
		got = append(got, hit{r.URL.Path, string(b)})
		w.WriteHeader(http.StatusOK)
	}))
	t.Cleanup(srv.Close)
	a := HTTPActions{BaseURL: srv.URL, HTTP: srv.Client()}
	if err := a.Pause(time.Hour); err != nil {
		t.Fatal(err)
	}
	if err := a.Resume(); err != nil {
		t.Fatal(err)
	}
	if len(got) != 2 || got[0].path != "/api/pause" || got[1].path != "/api/pause/resume" {
		t.Fatalf("%+v", got)
	}
	var body struct {
		DurationSec int    `json:"duration_sec"`
		Reason      string `json:"reason"`
	}
	if err := json.Unmarshal([]byte(got[0].body), &body); err != nil {
		t.Fatal(err)
	}
	if body.DurationSec != 3600 || body.Reason != "kiosk" {
		t.Fatalf("%+v", body)
	}
	if got[1].body != "" {
		t.Fatalf("resume body %q", got[1].body)
	}

	bad := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "no", http.StatusInternalServerError)
	}))
	t.Cleanup(bad.Close)
	if err := (HTTPActions{BaseURL: bad.URL, HTTP: bad.Client()}).Stop(); err == nil {
		t.Fatal("expected status error")
	}
}
