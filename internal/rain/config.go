package rain

import (
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"log"
	"net/url"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"time"
)

// maxDryDays is the cap for dry_days and heavy_dry_days. Longer holds are
// clamped at load. configLog receives that one line; normalized stays quiet.
const maxDryDays = 14

// configLog is the loader logger. Tests replace it. Nil uses the default logger.
var configLog = log.Default()

const envConfig = "ZANJERITO_RAIN_CONFIG"

// Config is the enabled rain-pause policy. Zero numeric fields are replaced
// by defaults in normalized (a present file defaults Enabled to true).
type Config struct {
	Enabled            bool
	GaugeID            string
	URL                string
	Method             string
	Body               string
	Timeout            time.Duration
	PollMinutes        float64
	PollSeconds        float64
	TriggerInches      float64
	WindowHours        float64
	DryDays            float64
	HeavyInches        float64
	HeavyDryDays       float64
	StaleHours         float64
	MaxIncrementInches float64
	MaxWindowInches    float64
}

type fileConfig struct {
	Enabled            *bool   `json:"enabled"`
	GaugeID            string  `json:"gauge_id"`
	URL                string  `json:"url"`
	Method             string  `json:"method"`
	Body               string  `json:"body"`
	TimeoutSeconds     float64 `json:"timeout_seconds"`
	PollMinutes        float64 `json:"poll_minutes"`
	PollSeconds        float64 `json:"poll_seconds"`
	TriggerInches      float64 `json:"trigger_inches"`
	WindowHours        float64 `json:"window_hours"`
	DryDays            float64 `json:"dry_days"`
	HeavyInches        float64 `json:"heavy_inches"`
	HeavyDryDays       float64 `json:"heavy_dry_days"`
	StaleHours         float64 `json:"stale_hours"`
	MaxIncrementInches float64 `json:"max_increment_inches"`
	MaxWindowInches    float64 `json:"max_window_inches"`
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

// PublicConfigError is the short config_error string for a Load or Start error.
// A missing file and a nil error yield "". The text has no gauge id, URL, or path.
func PublicConfigError(err error) string {
	if err == nil || errors.Is(err, fs.ErrNotExist) {
		return ""
	}
	var syn *json.SyntaxError
	if errors.As(err, &syn) {
		return SanitizeConfigMessage(syn.Error())
	}
	var ute *json.UnmarshalTypeError
	if errors.As(err, &ute) {
		return SanitizeConfigMessage(jsonTypeMessage(ute))
	}
	msg := err.Error()
	msg = strings.TrimPrefix(msg, "rain: config: ")
	return SanitizeConfigMessage(msg)
}

// SanitizeConfigMessage drops filesystem paths from a config error.
// A path collapses to "rain.local.json invalid" so the API cannot echo it.
func SanitizeConfigMessage(msg string) string {
	msg = strings.TrimSpace(msg)
	if msg == "" {
		return ""
	}
	if strings.Contains(msg, "/") || strings.Contains(msg, `\`) {
		return "rain.local.json invalid"
	}
	return msg
}

func jsonTypeMessage(e *json.UnmarshalTypeError) string {
	if e == nil {
		return "invalid JSON type"
	}
	field := knownConfigField(e.Field)
	if field == "" {
		return "invalid JSON type"
	}
	want := jsonWant(e.Type)
	switch want {
	case "array", "object":
		return field + ": must be an " + want
	default:
		return field + ": must be a " + want
	}
}

func knownConfigField(field string) string {
	known := []string{
		"max_increment_inches",
		"max_window_inches",
		"timeout_seconds",
		"trigger_inches",
		"heavy_dry_days",
		"poll_seconds",
		"poll_minutes",
		"window_hours",
		"heavy_inches",
		"stale_hours",
		"gauge_id",
		"dry_days",
		"enabled",
		"method",
		"body",
		"url",
	}
	best := ""
	for _, name := range known {
		if field == name || strings.HasSuffix(field, "."+name) {
			if len(name) > len(best) {
				best = name
			}
		}
	}
	return best
}

func jsonWant(t reflect.Type) string {
	if t == nil {
		return "value"
	}
	for t.Kind() == reflect.Pointer {
		t = t.Elem()
	}
	switch t.Kind() {
	case reflect.String:
		return "string"
	case reflect.Bool:
		return "bool"
	case reflect.Float32, reflect.Float64,
		reflect.Int, reflect.Int8, reflect.Int16, reflect.Int32, reflect.Int64,
		reflect.Uint, reflect.Uint8, reflect.Uint16, reflect.Uint32, reflect.Uint64:
		return "number"
	case reflect.Slice, reflect.Array:
		return "array"
	case reflect.Map, reflect.Struct:
		return "object"
	default:
		return "value"
	}
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
		Enabled:            true,
		GaugeID:            gauge,
		URL:                rawURL,
		Method:             strings.ToUpper(strings.TrimSpace(f.Method)),
		Body:               f.Body,
		Timeout:            time.Duration(f.TimeoutSeconds * float64(time.Second)),
		PollMinutes:        f.PollMinutes,
		PollSeconds:        f.PollSeconds,
		TriggerInches:      f.TriggerInches,
		WindowHours:        f.WindowHours,
		DryDays:            f.DryDays,
		HeavyInches:        f.HeavyInches,
		HeavyDryDays:       f.HeavyDryDays,
		StaleHours:         f.StaleHours,
		MaxIncrementInches: f.MaxIncrementInches,
		MaxWindowInches:    f.MaxWindowInches,
	}
	cfg = cfg.normalized()
	if f.DryDays > maxDryDays || f.HeavyDryDays > maxDryDays {
		lg := configLog
		if lg == nil {
			lg = log.Default()
		}
		lg.Printf("rain: dry_days and heavy_dry_days are capped at %d", maxDryDays)
	}
	return cfg, nil
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
	c.DryDays = capDays(c.DryDays, 2)
	if c.HeavyInches <= 0 {
		c.HeavyInches = 1
	}
	c.HeavyDryDays = capDays(c.HeavyDryDays, 4)
	if c.StaleHours <= 0 {
		c.StaleHours = 7
	}
	if c.MaxIncrementInches <= 0 {
		c.MaxIncrementInches = 2
	}
	if c.MaxWindowInches <= 0 {
		c.MaxWindowInches = 6
	}
	return c
}

// capDays applies the default when v is missing or <= 0, and clamps high
// values to maxDryDays. It does not log; parseConfig logs once at load.
func capDays(v, fallback float64) float64 {
	if v <= 0 {
		return fallback
	}
	if v > maxDryDays {
		return maxDryDays
	}
	return v
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
