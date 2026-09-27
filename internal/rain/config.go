package rain

import (
	"encoding/json"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const envConfig = "ZANJERITO_RAIN_CONFIG"

// Config is the enabled rain-pause policy. Zero numeric fields are replaced
// by defaults in normalized (a present file defaults Enabled to true).
type Config struct {
	Enabled       bool
	GaugeID       string
	URL           string
	Method        string
	Body          string
	Timeout       time.Duration
	PollMinutes   float64
	PollSeconds   float64
	TriggerInches float64
	WindowHours   float64
	DryDays       float64
	HeavyInches   float64
	HeavyDryDays  float64
	StaleHours    float64
}

type fileConfig struct {
	Enabled        *bool   `json:"enabled"`
	GaugeID        string  `json:"gauge_id"`
	URL            string  `json:"url"`
	Method         string  `json:"method"`
	Body           string  `json:"body"`
	TimeoutSeconds float64 `json:"timeout_seconds"`
	PollMinutes    float64 `json:"poll_minutes"`
	PollSeconds    float64 `json:"poll_seconds"`
	TriggerInches  float64 `json:"trigger_inches"`
	WindowHours    float64 `json:"window_hours"`
	DryDays        float64 `json:"dry_days"`
	HeavyInches    float64 `json:"heavy_inches"`
	HeavyDryDays   float64 `json:"heavy_dry_days"`
	StaleHours     float64 `json:"stale_hours"`
}

// Path is rain.local.json beside the config file, or ZANJERITO_RAIN_CONFIG.
func Path(configPath string) string {
	if p := strings.TrimSpace(os.Getenv(envConfig)); p != "" {
		return p
	}
	return filepath.Join(filepath.Dir(configPath), "rain.local.json")
}

// Load reads the rain file. A missing file returns a disabled config and a nil
// error. enabled:false is disabled with a nil error. Malformed JSON, or an
// enabled file without gauge_id and a valid url, returns an error; callers
// must not poll. Error text does not include the gauge id or URL.
func Load(configPath string) (Config, error) {
	b, err := os.ReadFile(Path(configPath))
	if err != nil {
		if os.IsNotExist(err) {
			return Config{}, nil
		}
		return Config{}, fmt.Errorf("rain: config: %w", err)
	}
	return parseConfig(b)
}

func parseConfig(b []byte) (Config, error) {
	var f fileConfig
	if err := json.Unmarshal(b, &f); err != nil {
		return Config{}, fmt.Errorf("rain: config: %w", err)
	}
	if f.Enabled != nil && !*f.Enabled {
		return Config{}, nil
	}
	gauge := strings.TrimSpace(f.GaugeID)
	rawURL := strings.TrimSpace(f.URL)
	if gauge == "" {
		return Config{}, fmt.Errorf("rain: gauge_id is required")
	}
	if rawURL == "" {
		return Config{}, fmt.Errorf("rain: url is required")
	}
	u, err := url.Parse(rawURL)
	if err != nil || u.Host == "" || (u.Scheme != "http" && u.Scheme != "https") {
		return Config{}, fmt.Errorf("rain: url is invalid")
	}
	cfg := Config{
		Enabled:       true,
		GaugeID:       gauge,
		URL:           rawURL,
		Method:        strings.ToUpper(strings.TrimSpace(f.Method)),
		Body:          f.Body,
		Timeout:       time.Duration(f.TimeoutSeconds * float64(time.Second)),
		PollMinutes:   f.PollMinutes,
		PollSeconds:   f.PollSeconds,
		TriggerInches: f.TriggerInches,
		WindowHours:   f.WindowHours,
		DryDays:       f.DryDays,
		HeavyInches:   f.HeavyInches,
		HeavyDryDays:  f.HeavyDryDays,
		StaleHours:    f.StaleHours,
	}
	return cfg.normalized(), nil
}

func (c Config) normalized() Config {
	if c.Method == "" {
		c.Method = "POST"
	}
	if strings.TrimSpace(c.Body) == "" {
		c.Body = DefaultBody
	}
	if c.Timeout <= 0 {
		c.Timeout = DefaultTimeout
	}
	if c.PollMinutes <= 0 {
		c.PollMinutes = 30
	}
	if c.TriggerInches <= 0 {
		c.TriggerInches = 0.25
	}
	if c.WindowHours <= 0 {
		c.WindowHours = 24
	}
	if c.DryDays <= 0 {
		c.DryDays = 2
	}
	if c.HeavyInches <= 0 {
		c.HeavyInches = 1
	}
	if c.HeavyDryDays <= 0 {
		c.HeavyDryDays = 4
	}
	if c.StaleHours <= 0 {
		c.StaleHours = 7
	}
	return c
}

// PollInterval is poll_seconds when set, otherwise poll_minutes (default 30).
func (c Config) PollInterval() time.Duration {
	c = c.normalized()
	if c.PollSeconds > 0 {
		d := time.Duration(c.PollSeconds * float64(time.Second))
		if d <= 0 {
			return time.Second
		}
		return d
	}
	d := time.Duration(c.PollMinutes * float64(time.Minute))
	if d <= 0 {
		return 30 * time.Minute
	}
	return d
}
