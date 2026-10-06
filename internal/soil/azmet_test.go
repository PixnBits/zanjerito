package soil

import (
	"bytes"
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"
	"time"
)

func TestAZMETParseMMToInchesAndFallback(t *testing.T) {
	raw, err := os.ReadFile("testdata/azmet_daily.json")
	if err != nil {
		t.Fatal(err)
	}
	days, err := parseAZMET(bytes.NewReader(raw))
	if err != nil {
		t.Fatal(err)
	}
	if len(days) != 3 {
		t.Fatalf("days %d", len(days))
	}
	// 5.0 mm / 25.4
	want := 5.0 / 25.4
	if days[0].Date != "2026-09-24" || days[0].ETInches < want-1e-9 || days[0].ETInches > want+1e-9 {
		t.Fatalf("mm row %+v want %v", days[0], want)
	}
	want2 := 6.35 / 25.4
	if days[1].ETInches < want2-1e-9 || days[1].ETInches > want2+1e-9 {
		t.Fatalf("second %+v", days[1])
	}
	if days[2].ETInches != 0.18 || !days[2].NeedsReview {
		t.Fatalf("fallback/review %+v", days[2])
	}
}

func TestAZMETToleratesNumericFields(t *testing.T) {
	body := []byte(`{"data":[{"datetime":"2026-09-24","eto_azmet":5.08,"meta_needs_review":0}]}`)
	days, err := parseAZMET(bytes.NewReader(body))
	if err != nil {
		t.Fatal(err)
	}
	want := 5.08 / 25.4
	if len(days) != 1 || days[0].ETInches < want-1e-9 || days[0].ETInches > want+1e-9 {
		t.Fatalf("%+v", days)
	}
}

func TestAZMETSkipsUnparseableRows(t *testing.T) {
	body := []byte(`{"data":[
		{"datetime":"nope","eto_azmet":"5.0"},
		{"datetime":"2026-09-24","eto_azmet":"n/a","eto_azmet_in":""},
		{"datetime":"2026-09-25","eto_azmet":"2.54"}
	]}`)
	days, err := parseAZMET(bytes.NewReader(body))
	if err != nil {
		t.Fatal(err)
	}
	if len(days) != 1 || days[0].Date != "2026-09-25" {
		t.Fatalf("%+v", days)
	}
}

func TestAZMETFetchPathAndSuccess(t *testing.T) {
	var gotPath string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotPath = r.URL.Path
		http.ServeFile(w, r, "testdata/azmet_daily.json")
	}))
	defer srv.Close()
	src := NewAZMET(Config{
		Station: "azXX",
		URL:     srv.URL,
		Timeout: time.Second,
	})
	loc, err := time.LoadLocation("America/Phoenix")
	if err != nil {
		t.Fatal(err)
	}
	start := time.Date(2026, 9, 24, 0, 0, 0, 0, loc)
	days, err := src.Fetch(context.Background(), start, 2)
	if err != nil {
		t.Fatal(err)
	}
	if len(days) != 3 {
		t.Fatalf("days %d", len(days))
	}
	if !strings.Contains(gotPath, "/azXX/") {
		t.Fatalf("path %s", gotPath)
	}
	if !strings.Contains(gotPath, "P2D") {
		t.Fatalf("interval %s", gotPath)
	}
	if !strings.Contains(gotPath, "2026-09-24T00:00") {
		t.Fatalf("start %s", gotPath)
	}
	if src.HTTP.Timeout != time.Second {
		t.Fatalf("timeout %s", src.HTTP.Timeout)
	}
}

func TestAZMETHTTP500(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "azXX missing", http.StatusInternalServerError)
	}))
	defer srv.Close()
	src := &AZMET{
		BaseURL:   srv.URL,
		StationID: "azXX",
		HTTP:      &http.Client{Timeout: time.Second},
	}
	_, err := src.Fetch(context.Background(), time.Now(), 13)
	if err == nil {
		t.Fatal("want error")
	}
	if !strings.Contains(err.Error(), "HTTP 500") {
		t.Fatalf("err %v", err)
	}
	if strings.Contains(err.Error(), "azXX") || strings.Contains(strings.ToLower(err.Error()), "http://") {
		t.Fatalf("error leaks: %v", err)
	}
}

func TestAZMETTimeout(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		select {
		case <-r.Context().Done():
		case <-time.After(2 * time.Second):
			w.WriteHeader(http.StatusOK)
		}
	}))
	defer srv.Close()
	src := &AZMET{
		BaseURL:   srv.URL,
		StationID: "azXX",
		HTTP:      &http.Client{Timeout: 80 * time.Millisecond},
	}
	start := time.Now()
	_, err := src.Fetch(context.Background(), time.Now(), 1)
	if err == nil {
		t.Fatal("want timeout")
	}
	if time.Since(start) > time.Second {
		t.Fatalf("timeout not honoured, took %s (%v)", time.Since(start), err)
	}
	if strings.Contains(err.Error(), "azXX") || strings.Contains(strings.ToLower(err.Error()), "http://") {
		t.Fatalf("error leaks: %v", err)
	}
}

func TestAZMETClosedServerRedacts(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(http.ResponseWriter, *http.Request) {}))
	rawURL := srv.URL + "/azXX"
	host := srv.Listener.Addr().String()
	srv.Close()
	src := &AZMET{
		BaseURL:   rawURL,
		StationID: "azXX",
		HTTP:      &http.Client{Timeout: time.Second},
	}
	_, err := src.Fetch(context.Background(), time.Now(), 1)
	if err == nil {
		t.Fatal("want error")
	}
	msg := err.Error()
	for _, leak := range []string{"azXX", rawURL, host, "127.0.0.1"} {
		if strings.Contains(msg, leak) {
			t.Fatalf("leak %q in %q", leak, msg)
		}
	}
}

func TestAZMETBadJSON(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, "<html>nope")
	}))
	defer srv.Close()
	src := &AZMET{BaseURL: srv.URL, StationID: "azXX", HTTP: &http.Client{Timeout: time.Second}}
	_, err := src.Fetch(context.Background(), time.Now(), 1)
	if err == nil || !strings.Contains(err.Error(), "bad JSON") {
		t.Fatalf("got %v", err)
	}
}
