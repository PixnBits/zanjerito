package rain

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestFCDMCPostAndParse(t *testing.T) {
	var gotMethod, gotBody, gotCT string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotMethod = r.Method
		gotCT = r.Header.Get("Content-Type")
		b, _ := io.ReadAll(r.Body)
		gotBody = string(b)
		http.ServeFile(w, r, "testdata/fcdmc_sample.html")
	}))
	defer srv.Close()

	src := NewFCDMC(Config{
		GaugeID: "TEST-GAUGE",
		URL:     srv.URL,
		Body:    DefaultBody,
	}, nil)
	samples, err := src.Fetch(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if gotMethod != http.MethodPost {
		t.Fatalf("method %s", gotMethod)
	}
	if gotCT != "application/x-www-form-urlencoded" {
		t.Fatalf("content-type %s", gotCT)
	}
	if gotBody != "ID1=TEST-GAUGE&ST=rain&NM=200" {
		t.Fatalf("body %q", gotBody)
	}
	if len(samples) != 7 {
		t.Fatalf("samples %d", len(samples))
	}
	if src.HTTP.Timeout != DefaultTimeout {
		t.Fatalf("default timeout %s", src.HTTP.Timeout)
	}
}

func TestFCDMCGaugeEscapeAndNM(t *testing.T) {
	var gotBody string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		b, _ := io.ReadAll(r.Body)
		gotBody = string(b)
		w.Write([]byte("09/01/2026 12:00:00  1.00  0.04\n"))
	}))
	defer srv.Close()
	src := &FCDMC{
		URL:     srv.URL,
		Method:  "",
		Body:    "ID1={gauge}&ST=rain&NM=50",
		GaugeID: "TEST/GAUGE",
		HTTP:    &http.Client{Timeout: time.Second},
	}
	if _, err := src.Fetch(context.Background()); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(gotBody, "ID1=TEST%2FGAUGE") || !strings.Contains(gotBody, "NM=50") {
		t.Fatalf("body %q", gotBody)
	}
}

func TestFCDMCStatus500Redacts(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Error(w, "TEST-GAUGE missing", http.StatusInternalServerError)
	}))
	defer srv.Close()
	src := &FCDMC{
		URL:     srv.URL,
		Method:  http.MethodPost,
		Body:    DefaultBody,
		GaugeID: "TEST-GAUGE",
		HTTP:    &http.Client{Timeout: time.Second},
	}
	_, err := src.Fetch(context.Background())
	if err == nil {
		t.Fatal("want error")
	}
	if !strings.Contains(err.Error(), "HTTP 500") {
		t.Fatalf("err %v", err)
	}
	if strings.Contains(err.Error(), "TEST-GAUGE") || strings.Contains(err.Error(), "http") {
		t.Fatalf("error leaks request: %v", err)
	}
}

func TestFCDMCTimeout(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		select {
		case <-r.Context().Done():
		case <-time.After(2 * time.Second):
			w.WriteHeader(http.StatusOK)
		}
	}))
	defer srv.Close()
	src := &FCDMC{
		URL:     srv.URL,
		Method:  http.MethodPost,
		Body:    DefaultBody,
		GaugeID: "TEST-GAUGE",
		HTTP:    &http.Client{Timeout: 80 * time.Millisecond},
	}
	start := time.Now()
	_, err := src.Fetch(context.Background())
	if err == nil {
		t.Fatal("want timeout")
	}
	if time.Since(start) > time.Second {
		t.Fatalf("timeout not honoured, took %s (%v)", time.Since(start), err)
	}
	if strings.Contains(err.Error(), "TEST-GAUGE") || strings.Contains(strings.ToLower(err.Error()), "http://") || strings.Contains(strings.ToLower(err.Error()), "https://") {
		t.Fatalf("error leaks: %v", err)
	}
}

func TestFCDMCContextCancel(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		select {
		case <-r.Context().Done():
		case <-time.After(2 * time.Second):
			w.WriteHeader(http.StatusOK)
		}
	}))
	defer srv.Close()
	src := &FCDMC{
		URL:     srv.URL,
		Method:  http.MethodPost,
		Body:    DefaultBody,
		GaugeID: "TEST-GAUGE",
		HTTP:    &http.Client{Timeout: 5 * time.Second},
	}
	ctx, cancel := context.WithTimeout(context.Background(), 80*time.Millisecond)
	defer cancel()
	start := time.Now()
	_, err := src.Fetch(ctx)
	if err == nil {
		t.Fatal("want ctx error")
	}
	if time.Since(start) > time.Second {
		t.Fatalf("context not honoured, took %s (%v)", time.Since(start), err)
	}
}
