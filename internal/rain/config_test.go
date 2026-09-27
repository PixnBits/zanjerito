package rain

import (
	"bytes"
	"log"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestLoadMissingDisabled(t *testing.T) {
	t.Setenv(envConfig, "")
	cfg, err := Load(filepath.Join(t.TempDir(), "config.json"))
	if err != nil {
		t.Fatal(err)
	}
	if cfg.Enabled {
		t.Fatal("missing file should disable")
	}
}

func TestLoadExample(t *testing.T) {
	t.Setenv(envConfig, "")
	path := filepath.Join("..", "..", "config", "rain.local.example.json")
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	cfg, err := parseConfig(raw)
	if err != nil {
		t.Fatal(err)
	}
	if !cfg.Enabled || cfg.GaugeID != "<GAUGE_ID>" {
		t.Fatalf("%+v", cfg)
	}
	if cfg.URL != "https://alert.fcd.maricopa.gov/php/showdata2.php" {
		t.Fatalf("url %s", cfg.URL)
	}
	if cfg.Body != DefaultBody || cfg.Method != "POST" {
		t.Fatalf("method %s body %s", cfg.Method, cfg.Body)
	}
	if cfg.TriggerInches != 0.25 || cfg.WindowHours != 24 || cfg.DryDays != 2 {
		t.Fatalf("thresholds %+v", cfg)
	}
	if cfg.HeavyInches != 1 || cfg.HeavyDryDays != 4 || cfg.StaleHours != 7 {
		t.Fatalf("heavy/stale %+v", cfg)
	}
	if cfg.MaxIncrementInches != 2 || cfg.MaxWindowInches != 6 {
		t.Fatalf("plausibility caps %+v", cfg)
	}
	if cfg.Timeout != 15*time.Second || cfg.PollInterval() != 30*time.Minute {
		t.Fatalf("timing timeout %s interval %s", cfg.Timeout, cfg.PollInterval())
	}
}

func TestLoadDefaultsAndEnv(t *testing.T) {
	dir := t.TempDir()
	minimal := filepath.Join(dir, "only.json")
	body := []byte(`{"gauge_id":"TEST-GAUGE","url":"https://example.test/rain"}`)
	if err := os.WriteFile(minimal, body, 0o644); err != nil {
		t.Fatal(err)
	}
	t.Setenv(envConfig, minimal)
	cfg, err := Load(filepath.Join(dir, "ignored.json"))
	if err != nil {
		t.Fatal(err)
	}
	if !cfg.Enabled || cfg.GaugeID != "TEST-GAUGE" {
		t.Fatalf("%+v", cfg)
	}
	if cfg.Method != "POST" || cfg.Body != DefaultBody {
		t.Fatalf("defaults method %s body %s", cfg.Method, cfg.Body)
	}
	if cfg.Timeout != DefaultTimeout || cfg.PollMinutes != 30 || cfg.PollInterval() != 30*time.Minute {
		t.Fatalf("poll %+v interval %s", cfg, cfg.PollInterval())
	}
	if cfg.TriggerInches != 0.25 || cfg.StaleHours != 7 || cfg.DryDays != 2 || cfg.HeavyDryDays != 4 {
		t.Fatalf("numeric defaults %+v", cfg)
	}
	if cfg.MaxIncrementInches != 2 || cfg.MaxWindowInches != 6 {
		t.Fatalf("plausibility defaults %+v", cfg)
	}

	fast := filepath.Join(dir, "fast.json")
	fastBody := []byte(`{"gauge_id":"TEST-GAUGE","url":"https://example.test/rain","poll_minutes":30,"poll_seconds":2}`)
	if err := os.WriteFile(fast, fastBody, 0o644); err != nil {
		t.Fatal(err)
	}
	t.Setenv(envConfig, fast)
	cfg, err = Load(filepath.Join(dir, "other.json"))
	if err != nil {
		t.Fatal(err)
	}
	if cfg.PollInterval() != 2*time.Second {
		t.Fatalf("poll_seconds override %s", cfg.PollInterval())
	}

	frac := filepath.Join(dir, "frac.json")
	if err := os.WriteFile(frac, []byte(`{"gauge_id":"TEST-GAUGE","url":"https://example.test/rain","poll_minutes":0.5}`), 0o644); err != nil {
		t.Fatal(err)
	}
	t.Setenv(envConfig, frac)
	cfg, err = Load(filepath.Join(dir, "other.json"))
	if err != nil {
		t.Fatal(err)
	}
	if cfg.PollInterval() != 30*time.Second {
		t.Fatalf("fractional poll_minutes %s", cfg.PollInterval())
	}
}

func TestLoadDisabledAndMalformed(t *testing.T) {
	dir := t.TempDir()
	off := filepath.Join(dir, "off.json")
	if err := os.WriteFile(off, []byte(`{"enabled":false}`), 0o644); err != nil {
		t.Fatal(err)
	}
	t.Setenv(envConfig, off)
	cfg, err := Load(filepath.Join(dir, "config.json"))
	if err != nil || cfg.Enabled {
		t.Fatalf("enabled false: %+v %v", cfg, err)
	}

	bad := filepath.Join(dir, "bad.json")
	if err := os.WriteFile(bad, []byte(`{`), 0o644); err != nil {
		t.Fatal(err)
	}
	t.Setenv(envConfig, bad)
	cfg, err = Load(filepath.Join(dir, "config.json"))
	if err == nil || cfg.Enabled {
		t.Fatalf("malformed should error and disable, %+v %v", cfg, err)
	}
	if strings.Contains(err.Error(), "TEST-GAUGE") {
		t.Fatalf("error leaked id: %v", err)
	}

	nog := filepath.Join(dir, "nog.json")
	if err := os.WriteFile(nog, []byte(`{"url":"https://example.test/rain"}`), 0o644); err != nil {
		t.Fatal(err)
	}
	t.Setenv(envConfig, nog)
	_, err = Load(filepath.Join(dir, "config.json"))
	if err == nil || !strings.Contains(err.Error(), "gauge_id") {
		t.Fatalf("got %v", err)
	}
}

func TestLoadPlausibilityAndDryDayCap(t *testing.T) {
	var buf bytes.Buffer
	prev := configLog
	configLog = log.New(&buf, "", 0)
	t.Cleanup(func() { configLog = prev })

	parse := func(body string) Config {
		t.Helper()
		cfg, err := parseConfig([]byte(body))
		if err != nil {
			t.Fatal(err)
		}
		return cfg
	}
	base := `{"gauge_id":"TEST-GAUGE","url":"https://example.test/rain"`
	cfg := parse(base + `,"max_increment_inches":0,"max_window_inches":-1}`)
	if cfg.MaxIncrementInches != 2 || cfg.MaxWindowInches != 6 {
		t.Fatalf("non-positive caps %+v", cfg)
	}
	cfg = parse(base + `,"max_increment_inches":1.5,"max_window_inches":4}`)
	if cfg.MaxIncrementInches != 1.5 || cfg.MaxWindowInches != 4 {
		t.Fatalf("explicit caps %+v", cfg)
	}

	cfg = parse(base + `,"dry_days":30,"heavy_dry_days":90}`)
	if cfg.DryDays != 14 || cfg.HeavyDryDays != 14 {
		t.Fatalf("clamp %+v", cfg)
	}
	if strings.Count(buf.String(), "capped at 14") != 1 {
		t.Fatalf("log %q", buf.String())
	}
	if strings.Contains(buf.String(), "TEST-GAUGE") || strings.Contains(buf.String(), "example.test") {
		t.Fatalf("log leaked config %q", buf.String())
	}

	buf.Reset()
	cfg = parse(base + `,"dry_days":3,"heavy_dry_days":3}`)
	if cfg.DryDays != 3 || cfg.HeavyDryDays != 3 {
		t.Fatalf("under cap %+v", cfg)
	}
	if buf.Len() != 0 {
		t.Fatalf("unexpected log %q", buf.String())
	}
	cfg = parse(base + `,"dry_days":30,"heavy_dry_days":3}`)
	if cfg.DryDays != 14 || cfg.HeavyDryDays != 3 {
		t.Fatalf("mixed %+v", cfg)
	}
	// normalized clamps too, but only the loader logs.
	buf.Reset()
	got := Config{DryDays: 30, HeavyDryDays: 90}.normalized()
	if got.DryDays != 14 || got.HeavyDryDays != 14 {
		t.Fatalf("normalized %+v", got)
	}
	if buf.Len() != 0 {
		t.Fatalf("normalized logged %q", buf.String())
	}
}

func TestGitignoreRainLocal(t *testing.T) {
	b, err := os.ReadFile(filepath.Join("..", "..", ".gitignore"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(b), "rain.local.json") {
		t.Fatal(".gitignore missing rain.local.json")
	}
}
