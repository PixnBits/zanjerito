package main

import (
	"bytes"
	"context"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/hamqtt"
)

func TestRedactTextHidesHAURLPassword(t *testing.T) {
	const pass = "s3cretpw"
	ha := "http://user:" + pass + "@host:8080/api/ha"
	cfg := hamqtt.Config{HAURL: ha}
	secrets := hamqtt.URLPasswords(ha)
	if len(secrets) == 0 || secrets[0] != pass {
		t.Fatalf("URLPasswords did not return the HA URL password (%d)", len(secrets))
	}
	msg := "Get \"" + ha + "\": dial failed " + pass
	for _, s := range secrets {
		if !strings.Contains(msg, s) {
			msg += " " + s
		}
	}
	got := redactText(cfg, msg)
	for _, s := range secrets {
		if s != "" && strings.Contains(got, s) {
			t.Fatal("redactText left the HA URL password in the string")
		}
	}
}

func TestStartupLineAndFetchOmitHAPassword(t *testing.T) {
	const pass = "s3cretpw"
	ha := "http://user:" + pass + "@host:8080/api/ha"
	cfg := hamqtt.Config{
		URL:             "tcp://127.0.0.1:1883",
		HAURL:           ha,
		ClientID:        "zanjerito-mqtt",
		PollInterval:    2 * time.Second,
		RefreshInterval: 5 * time.Minute,
	}
	if strings.Contains(hamqtt.StartupLine(cfg, "dev"), pass) {
		t.Fatal("StartupLine contains the HA URL password")
	}

	srv := httptest.NewServer(http.HandlerFunc(func(http.ResponseWriter, *http.Request) {}))
	u, err := url.Parse(srv.URL)
	if err != nil {
		t.Fatal(err)
	}
	srv.Close()
	fetchURL := "http://user:" + pass + "@" + u.Host + "/api/ha"
	fetch := hamqtt.NewHTTPFetcher(fetchURL, 200*time.Millisecond)
	_, err = fetch(context.Background())
	if err == nil {
		t.Fatal("expected fetch error")
	}
	if strings.Contains(err.Error(), pass) {
		t.Fatal("fetch error contains the HA URL password")
	}

	bad := "http://user:" + pass + "@host/%zz"
	_, err = hamqtt.NewHTTPFetcher(bad, 200*time.Millisecond)(context.Background())
	if err == nil {
		t.Fatal("expected invalid URL error")
	}
	if strings.Contains(err.Error(), pass) {
		t.Fatal("invalid URL error contains the HA URL password")
	}
}

func TestDispatchHAStatusDrops65(t *testing.T) {
	payload := bytes.Repeat([]byte("z"), 65)
	called := make(chan struct{}, 1)
	ok := dispatchHAStatus(payload, func([]byte) {
		called <- struct{}{}
	})
	if ok {
		t.Fatal("65-byte payload returned true")
	}
	select {
	case <-called:
		t.Fatal("on called for 65-byte payload")
	case <-time.After(100 * time.Millisecond):
	}
}

func TestDispatchHAStatusOnlineCopy(t *testing.T) {
	payload := []byte("online")
	entered := make(chan struct{})
	release := make(chan struct{})
	got := make(chan []byte, 1)
	ok := dispatchHAStatus(payload, func(b []byte) {
		entered <- struct{}{}
		<-release
		got <- append([]byte(nil), b...)
	})
	if !ok {
		t.Fatal("online not dispatched")
	}
	select {
	case <-entered:
	case <-time.After(time.Second):
		t.Fatal("timeout waiting for on")
	}
	payload[0] = 'X'
	close(release)
	select {
	case b := <-got:
		if string(b) != "online" {
			t.Fatalf("on saw %q", b)
		}
	case <-time.After(time.Second):
		t.Fatal("timeout waiting for copy")
	}
}

func TestDispatchHAStatusExact64(t *testing.T) {
	payload := bytes.Repeat([]byte("b"), 64)
	got := make(chan []byte, 1)
	ok := dispatchHAStatus(payload, func(b []byte) {
		got <- append([]byte(nil), b...)
	})
	if !ok {
		t.Fatal("64-byte payload not dispatched")
	}
	select {
	case b := <-got:
		if len(b) != 64 || !bytes.Equal(b, bytes.Repeat([]byte("b"), 64)) {
			t.Fatalf("on saw %d bytes", len(b))
		}
	case <-time.After(time.Second):
		t.Fatal("timeout waiting for on")
	}
}
