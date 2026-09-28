package soil

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io/fs"
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
	if !errors.Is(err, ErrNoConfig) || !errors.Is(err, fs.ErrNotExist) {
		t.Fatalf("missing: %v", err)
	}
	if cfg.Enabled {
		t.Fatal("missing file should disable")
	}
	if PublicConfigError(err) != "" {
		t.Fatalf("config_error %q", PublicConfigError(err))
	}
}

func TestLoadExample(t *testing.T) {
	t.Setenv(envConfig, "")
	path := filepath.Join("..", "..", "config", "soil.local.example.json")
	raw, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	cfg, err := parseConfig(raw)
	if err != nil {
		t.Fatal(err)
	}
	if !cfg.Enabled || cfg.Station != "azXX" {
		t.Fatalf("%+v", cfg)
	}
	if cfg.URL != DefaultURL {
		t.Fatalf("url %s", cfg.URL)
	}
	if cfg.Timeout != 15*time.Second || cfg.PollInterval() != 6*time.Hour {
		t.Fatalf("timing timeout %s interval %s", cfg.Timeout, cfg.PollInterval())
	}
	if cfg.WindowDays != 14 || cfg.MaxDailyET != 0.6 || cfg.CropFactor != 0.6 || cfg.Capacity != 1.0 {
		t.Fatalf("defaults %+v", cfg)
	}
	z := cfg.ZoneFor("front-north")
	if z.InchesPerHour != nil || z.CropFactor != 0.6 || z.Capacity != 1.0 {
		t.Fatalf("zone %+v", z)
	}
}

func TestLoadDefaultsAndEnv(t *testing.T) {
	dir := t.TempDir()
	minimal := filepath.Join(dir, "only.json")
	body := []byte(`{"azmet_station":"azXX"}`)
	if err := os.WriteFile(minimal, body, 0o644); err != nil {
		t.Fatal(err)
	}
	t.Setenv(envConfig, minimal)
	cfg, err := Load(filepath.Join(dir, "ignored.json"))
	if err != nil {
		t.Fatal(err)
	}
	if !cfg.Enabled || cfg.Station != "azXX" {
		t.Fatalf("%+v", cfg)
	}
	if cfg.URL != DefaultURL || cfg.Timeout != DefaultTimeout {
		t.Fatalf("url/timeout %+v", cfg)
	}
	if cfg.PollHours != DefaultPollHours || cfg.PollInterval() != 6*time.Hour {
		t.Fatalf("poll %+v interval %s", cfg, cfg.PollInterval())
	}
	if cfg.WindowDays != 14 || cfg.MaxDailyET != 0.6 || cfg.CropFactor != 0.6 || cfg.Capacity != 1.0 {
		t.Fatalf("numeric defaults %+v", cfg)
	}
	unknown := cfg.ZoneFor("front-south")
	if unknown.InchesPerHour != nil {
		t.Fatal("unlisted station must not guess a rate")
	}

	fast := filepath.Join(dir, "fast.json")
	if err := os.WriteFile(fast, []byte(`{"azmet_station":"azXX","poll_hours":6,"poll_seconds":2}`), 0o644); err != nil {
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

	nost := filepath.Join(dir, "nost.json")
	if err := os.WriteFile(nost, []byte(`{"azmet_url":"https://example.test/v1"}`), 0o644); err != nil {
		t.Fatal(err)
	}
	t.Setenv(envConfig, nost)
	_, err = Load(filepath.Join(dir, "config.json"))
	if err == nil || !strings.Contains(err.Error(), "azmet_station") {
		t.Fatalf("got %v", err)
	}

	badURL := filepath.Join(dir, "badurl.json")
	if err := os.WriteFile(badURL, []byte(`{"azmet_station":"azXX","azmet_url":"not-a-url"}`), 0o644); err != nil {
		t.Fatal(err)
	}
	t.Setenv(envConfig, badURL)
	_, err = Load(filepath.Join(dir, "config.json"))
	if err == nil || strings.Contains(err.Error(), "azXX") || strings.Contains(err.Error(), "not-a-url") {
		t.Fatalf("got %v", err)
	}
}

func TestLoadInvalidValues(t *testing.T) {
	var buf bytes.Buffer
	prev := configLog
	configLog = log.New(&buf, "", 0)
	t.Cleanup(func() { configLog = prev })

	cfg, err := parseConfig([]byte(`{
		"azmet_station":"azXX",
		"crop_factor": 9,
		"capacity_inches": 0,
		"max_daily_et_inches": -1,
		"window_days": 0,
		"zones": {
			"front-north": {"inches_per_hour": 99, "crop_factor": 0, "capacity_inches": 20},
			"front-south": {"inches_per_hour": 0.4}
		}
	}`))
	if err != nil {
		t.Fatal(err)
	}
	if cfg.CropFactor != DefaultCropFactor || cfg.Capacity != DefaultCapacity || cfg.MaxDailyET != DefaultMaxDailyET {
		t.Fatalf("top-level defaults %+v", cfg)
	}
	if cfg.WindowDays != DefaultWindowDays {
		t.Fatalf("window %d", cfg.WindowDays)
	}
	n := cfg.ZoneFor("front-north")
	if n.InchesPerHour != nil || n.CropFactor != DefaultCropFactor || n.Capacity != DefaultCapacity {
		t.Fatalf("invalid zone %+v", n)
	}
	s := cfg.ZoneFor("front-south")
	if s.InchesPerHour == nil || *s.InchesPerHour != 0.4 {
		t.Fatalf("valid rate %+v", s)
	}
	logText := buf.String()
	if !strings.Contains(logText, "invalid") {
		t.Fatalf("want invalid log, got %q", logText)
	}
	if strings.Contains(logText, "https://") {
		t.Fatalf("log leaked url %q", logText)
	}
}

func TestStartMissingDisabledAndInvalid(t *testing.T) {
	t.Setenv(envConfig, "")
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	p, err := Start(ctx, filepath.Join(t.TempDir(), "config.json"), time.UTC)
	if p != nil || !errors.Is(err, ErrNoConfig) || !errors.Is(err, fs.ErrNotExist) {
		t.Fatalf("missing p=%v err=%v", p, err)
	}

	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "soil.local.json"), []byte(`{"enabled":false}`), 0o644); err != nil {
		t.Fatal(err)
	}
	p, err = Start(ctx, filepath.Join(dir, "config.json"), time.UTC)
	if p != nil || err != nil {
		t.Fatalf("disabled p=%v err=%v", p, err)
	}

	cases := []struct {
		name string
		body string
		want string
	}{
		{name: "malformed", body: `{`, want: "unexpected end of JSON input"},
		{name: "station type", body: `{"azmet_station":123}`, want: "azmet_station: must be a string"},
		{
			name: "rate type",
			body: `{"azmet_station":"azXX","azmet_url":"https://example.test/v1","zones":{"front-north":{"inches_per_hour":"fast"}}}`,
			want: "inches_per_hour: must be a number",
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			d := t.TempDir()
			if err := os.WriteFile(filepath.Join(d, "soil.local.json"), []byte(tc.body), 0o644); err != nil {
				t.Fatal(err)
			}
			p, err := Start(ctx, filepath.Join(d, "config.json"), time.UTC)
			if p != nil || err == nil || errors.Is(err, ErrNoConfig) || errors.Is(err, fs.ErrNotExist) {
				t.Fatalf("p=%v err=%v", p, err)
			}
			if strings.Contains(err.Error(), "azXX") || strings.Contains(err.Error(), "example.test") || strings.Contains(err.Error(), "fast") {
				t.Fatalf("logged error leaked: %v", err)
			}
			msg := PublicConfigError(err)
			if msg != tc.want {
				t.Fatalf("config_error %q want %q (err %v)", msg, tc.want, err)
			}
			if strings.Contains(msg, "/") || strings.Contains(msg, "azXX") {
				t.Fatalf("config_error %q", msg)
			}
		})
	}

	pathErr := fmt.Errorf("soil: config: open /tmp/zanjerito/soil.local.json: permission denied")
	if msg := PublicConfigError(pathErr); msg != "soil.local.json invalid" || strings.Contains(msg, "/") || strings.Contains(msg, "tmp") {
		t.Fatalf("path msg %q", msg)
	}
}

func TestGitignoreSoilLocal(t *testing.T) {
	b, err := os.ReadFile(filepath.Join("..", "..", ".gitignore"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(b), "soil.local.json") {
		t.Fatal(".gitignore missing soil.local.json")
	}
}
