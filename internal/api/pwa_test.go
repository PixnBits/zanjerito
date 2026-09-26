package api

import (
	"bytes"
	"encoding/json"
	"image/png"
	"net/http"
	"strings"
	"testing"
)

func TestPWAManifest(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/manifest.webmanifest", nil)
	if rr.Code != 200 {
		t.Fatalf("manifest %d %s", rr.Code, rr.Body.String())
	}
	ct := rr.Header().Get("Content-Type")
	if ct != "application/manifest+json" && !strings.HasPrefix(ct, "application/manifest+json") {
		t.Fatalf("manifest Content-Type %q", ct)
	}
	var man map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &man); err != nil {
		t.Fatalf("manifest json: %v", err)
	}
	if man["name"] != "Zanjerito" {
		t.Fatalf("name %v", man["name"])
	}
	short, _ := man["short_name"].(string)
	if short == "" {
		t.Fatal("short_name empty")
	}
	if man["start_url"] != "/" {
		t.Fatalf("start_url %v", man["start_url"])
	}
	if man["scope"] != "/" {
		t.Fatalf("scope %v", man["scope"])
	}
	if man["display"] != "standalone" {
		t.Fatalf("display %v", man["display"])
	}
	for _, key := range []string{"theme_color", "background_color"} {
		v, _ := man[key].(string)
		if v == "" || !strings.HasPrefix(v, "#") {
			t.Fatalf("%s %q", key, v)
		}
	}
	icons, _ := man["icons"].([]any)
	var saw192, saw512any, saw512mask bool
	for _, raw := range icons {
		ic, _ := raw.(map[string]any)
		src, _ := ic["src"].(string)
		sizes, _ := ic["sizes"].(string)
		typ, _ := ic["type"].(string)
		purpose, _ := ic["purpose"].(string)
		if src == "" {
			t.Fatal("icon missing src")
		}
		if sizes == "192x192" && strings.Contains(typ, "png") {
			saw192 = true
			assertPNGSize(t, s, src, 192)
		}
		if sizes == "512x512" && strings.Contains(typ, "png") {
			if purpose == "any" || strings.Contains(purpose, "any") {
				saw512any = true
				assertPNGSize(t, s, src, 512)
			}
			if strings.Contains(purpose, "maskable") {
				saw512mask = true
				assertPNGSize(t, s, src, 512)
			}
		}
	}
	if !saw192 || !saw512any || !saw512mask {
		t.Fatalf("icons 192=%v 512any=%v 512maskable=%v", saw192, saw512any, saw512mask)
	}
}

func TestPWAIcons(t *testing.T) {
	s := newTestServer(t)
	assertPNGSize(t, s, "/icons/apple-touch-icon.png", 180)
	rr := doJSON(t, s, http.MethodGet, "/icons/icon.svg", nil)
	if rr.Code != 200 {
		t.Fatalf("svg %d %s", rr.Code, rr.Body.String())
	}
	ct := rr.Header().Get("Content-Type")
	if !strings.HasPrefix(ct, "image/svg+xml") {
		t.Fatalf("svg Content-Type %q", ct)
	}
	rr = doJSON(t, s, http.MethodGet, "/icons/nope.png", nil)
	if rr.Code != 404 {
		t.Fatalf("unknown icon want 404, got %d", rr.Code)
	}
}

func TestPWAServiceWorker(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/sw.js", nil)
	if rr.Code != 200 {
		t.Fatalf("sw %d %s", rr.Code, rr.Body.String())
	}
	ct := rr.Header().Get("Content-Type")
	if !strings.HasPrefix(ct, "text/javascript") {
		t.Fatalf("sw Content-Type %q", ct)
	}
	cc := rr.Header().Get("Cache-Control")
	if !strings.Contains(cc, "no-cache") {
		t.Fatalf("sw Cache-Control %q", cc)
	}
	body := rr.Body.String()
	if !strings.Contains(body, `"/api/"`) {
		t.Fatal("sw must bypass /api/")
	}
	if !strings.Contains(body, `req.method !== "GET"`) {
		t.Fatal("sw must ignore non-GET")
	}
	if strings.Contains(body, `addEventListener("sync"`) ||
		strings.Contains(body, `addEventListener('sync'`) ||
		strings.Contains(body, ".sync.register") {
		t.Fatal("sw must not register background sync")
	}
}

func TestPWAHomeHTML(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/", nil)
	if rr.Code != 200 {
		t.Fatalf("GET / %d", rr.Code)
	}
	body := rr.Body.String()
	for _, need := range []string{
		`rel="manifest"`,
		`href="/manifest.webmanifest"`,
		`name="theme-color"`,
		`apple-touch-icon`,
		`apple-mobile-web-app-capable`,
		`mobile-web-app-capable`,
		`apple-mobile-web-app-title`,
		`apple-mobile-web-app-status-bar-style`,
		`viewport-fit=cover`,
		`safe-area-inset-top`,
		`safe-area-inset-bottom`,
		`serviceWorker`,
		`isSecureContext`,
		"Can't reach the controller",
	} {
		if !strings.Contains(body, need) {
			t.Fatalf("ui missing %q", need)
		}
	}
}

func TestPWAStatusUnchanged(t *testing.T) {
	s := newTestServer(t)
	rr := doJSON(t, s, http.MethodGet, "/api/status", nil)
	if rr.Code != 200 {
		t.Fatalf("status %d %s", rr.Code, rr.Body.String())
	}
	ct := rr.Header().Get("Content-Type")
	if !strings.Contains(ct, "application/json") {
		t.Fatalf("status Content-Type %q", ct)
	}
	var got map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	if got["phase"] != "Idle" {
		t.Fatalf("phase %v", got["phase"])
	}
}

func assertPNGSize(t *testing.T, s *Server, path string, want int) {
	t.Helper()
	rr := doJSON(t, s, http.MethodGet, path, nil)
	if rr.Code != 200 {
		t.Fatalf("%s %d %s", path, rr.Code, rr.Body.String())
	}
	ct := rr.Header().Get("Content-Type")
	if !strings.HasPrefix(ct, "image/png") {
		t.Fatalf("%s Content-Type %q", path, ct)
	}
	cfg, err := png.DecodeConfig(bytes.NewReader(rr.Body.Bytes()))
	if err != nil {
		t.Fatalf("%s png.DecodeConfig: %v", path, err)
	}
	if cfg.Width != want || cfg.Height != want {
		t.Fatalf("%s size %dx%d want %d", path, cfg.Width, cfg.Height, want)
	}
}
