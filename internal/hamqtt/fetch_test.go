package hamqtt

import (
	"bytes"
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestHTTPFetcherOK(t *testing.T) {
	want := []byte(`{"phase":"Idle"}`)
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			t.Errorf("method %s", r.Method)
		}
		if r.URL.Path != "/api/ha" {
			t.Errorf("path %s", r.URL.Path)
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write(want)
	}))
	defer srv.Close()

	fetch := NewHTTPFetcher(srv.URL+"/api/ha", time.Second)
	got, err := fetch(context.Background())
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(got, want) {
		t.Fatalf("got %s", got)
	}
}

func TestHTTPFetcherNon2xx(t *testing.T) {
	for _, code := range []int{http.StatusNotFound, http.StatusBadGateway, http.StatusInternalServerError} {
		srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			http.Error(w, "nope", code)
		}))
		fetch := NewHTTPFetcher(srv.URL, time.Second)
		_, err := fetch(context.Background())
		srv.Close()
		if err == nil {
			t.Fatalf("HTTP %d: want error", code)
		}
		if !strings.Contains(err.Error(), "HTTP") {
			t.Fatalf("HTTP %d: %v", code, err)
		}
	}
}

func TestHTTPFetcherBodyCap(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		_, _ = io.Copy(w, bytes.NewReader(make([]byte, maxBody+1)))
	}))
	defer srv.Close()
	fetch := NewHTTPFetcher(srv.URL, time.Second)
	if _, err := fetch(context.Background()); err == nil {
		t.Fatal("want body too large")
	}
}
