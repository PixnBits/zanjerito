package soil

import (
	"encoding/json"
	"fmt"
	"log"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"time"
)

const (
	envConfig = "ZANJERITO_SOIL_CONFIG"

	DefaultURL        = "https://api.azmet.arizona.edu/v1/observations/daily"
	DefaultTimeout    = 15 * time.Second
	DefaultPollHours  = 6.0
	DefaultWindowDays = 14
	DefaultMaxDailyET = 0.6
	DefaultCropFactor = 0.6
	DefaultCapacity   = 1.0
)

// configLog is the loader logger. Tests replace it. Nil uses the default logger.
var configLog = log.Default()

// Zone is per-station soil knobs. InchesPerHour nil means the application
// rate is unknown; watering depth is then omitted from the estimate.
type Zone struct {
	InchesPerHour *float64
	CropFactor    float64
	Capacity      float64
}

// Config is the enabled soil-estimate policy. A present file defaults Enabled
// to true. Zero numeric fields are replaced by defaults in normalized.
type Config struct {
	Enabled     bool
	Station     string
	URL         string
	Timeout     time.Duration
	PollHours   float64
	PollSeconds float64
	WindowDays  int
	MaxDailyET  float64
	CropFactor  float64
	Capacity    float64
	Zones       map[string]Zone
}

type fileZone struct {
	InchesPerHour *float64 `json:"inches_per_hour"`
	CropFactor    *float64 `json:"crop_factor"`
	Capacity      *float64 `json:"capacity_inches"`
}

type fileConfig struct {
	Enabled        *bool               `json:"enabled"`
	Station        string              `json:"azmet_station"`
	URL            string              `json:"azmet_url"`
	TimeoutSeconds float64             `json:"timeout_seconds"`
	PollHours      float64             `json:"poll_hours"`
	PollSeconds    float64             `json:"poll_seconds"`
	WindowDays     int                 `json:"window_days"`
	MaxDailyET     *float64            `json:"max_daily_et_inches"`
	CropFactor     *float64            `json:"crop_factor"`
	Capacity       *float64            `json:"capacity_inches"`
	Zones          map[string]fileZone `json:"zones"`
}

// Path is soil.local.json beside the config file, or ZANJERITO_SOIL_CONFIG.
func Path(configPath string) string {
	if p := strings.TrimSpace(os.Getenv(envConfig)); p != "" {
		return p
	}
	return filepath.Join(filepath.Dir(configPath), "soil.local.json")
}

// CachePath is the optional ET sidecar beside the config file.
func CachePath(configPath string) string {
	return filepath.Join(filepath.Dir(configPath), "soil-et.json")
}

// Load reads the soil file. A missing file returns a disabled config and a nil
// error. enabled:false is disabled with a nil error. Malformed JSON, or an
// enabled file without azmet_station and a valid url, returns an error; callers
// must not poll. Error text does not include the AZMET station id or URL.
func Load(configPath string) (Config, error) {
	b, err := os.ReadFile(Path(configPath))
	if err != nil {
		if os.IsNotExist(err) {
			return Config{}, nil
		}
		return Config{}, fmt.Errorf("soil: config: %w", err)
	}
	return parseConfig(b)
}

func parseConfig(b []byte) (Config, error) {
	var f fileConfig
	if err := json.Unmarshal(b, &f); err != nil {
		return Config{}, fmt.Errorf("soil: config: %w", err)
	}
	if f.Enabled != nil && !*f.Enabled {
		return Config{}, nil
	}
	station := strings.TrimSpace(f.Station)
	if station == "" {
		return Config{}, fmt.Errorf("soil: azmet_station is required")
	}
	rawURL := strings.TrimSpace(f.URL)
	if rawURL == "" {
		rawURL = DefaultURL
	}
	u, err := url.Parse(rawURL)
	if err != nil || u.Host == "" || (u.Scheme != "http" && u.Scheme != "https") {
		return Config{}, fmt.Errorf("soil: azmet_url is invalid")
	}
	cfg := Config{
		Enabled:     true,
		Station:     station,
		URL:         rawURL,
		Timeout:     time.Duration(f.TimeoutSeconds * float64(time.Second)),
		PollHours:   f.PollHours,
		PollSeconds: f.PollSeconds,
		WindowDays:  f.WindowDays,
		Zones:       map[string]Zone{},
	}
	cfg.CropFactor = pickPositive(f.CropFactor, DefaultCropFactor, validCrop, "crop_factor")
	cfg.Capacity = pickPositive(f.Capacity, DefaultCapacity, validCapacity, "capacity_inches")
	cfg.MaxDailyET = pickPositive(f.MaxDailyET, DefaultMaxDailyET, validMaxET, "max_daily_et_inches")
	for id, z := range f.Zones {
		id = strings.TrimSpace(id)
		if id == "" {
			continue
		}
		cfg.Zones[id] = parseZone(id, z, cfg.CropFactor, cfg.Capacity)
	}
	return cfg.normalized(), nil
}

func parseZone(id string, z fileZone, defCrop, defCap float64) Zone {
	out := Zone{CropFactor: defCrop, Capacity: defCap}
	if z.CropFactor != nil {
		if validCrop(*z.CropFactor) {
			out.CropFactor = *z.CropFactor
		} else {
			logConfig("soil: zone %s crop_factor invalid, using default", id)
		}
	}
	if z.Capacity != nil {
		if validCapacity(*z.Capacity) {
			out.Capacity = *z.Capacity
		} else {
			logConfig("soil: zone %s capacity_inches invalid, using default", id)
		}
	}
	if z.InchesPerHour != nil {
		if validRate(*z.InchesPerHour) {
			v := *z.InchesPerHour
			out.InchesPerHour = &v
		} else {
			logConfig("soil: zone %s inches_per_hour invalid, ignoring", id)
		}
	}
	return out
}

func pickPositive(v *float64, fallback float64, ok func(float64) bool, field string) float64 {
	if v == nil {
		return fallback
	}
	if ok(*v) {
		return *v
	}
	logConfig("soil: %s invalid, using default", field)
	return fallback
}

func validCrop(v float64) bool     { return v > 0 && v <= 1.5 }
func validCapacity(v float64) bool { return v > 0 && v <= 12 }
func validRate(v float64) bool     { return v > 0 && v <= 10 }
func validMaxET(v float64) bool    { return v > 0 && v <= 12 }

func (c Config) normalized() Config {
	if strings.TrimSpace(c.URL) == "" {
		c.URL = DefaultURL
	}
	if c.Timeout <= 0 {
		c.Timeout = DefaultTimeout
	}
	if c.PollHours <= 0 {
		c.PollHours = DefaultPollHours
	}
	if c.WindowDays <= 0 {
		c.WindowDays = DefaultWindowDays
	}
	if !validMaxET(c.MaxDailyET) {
		c.MaxDailyET = DefaultMaxDailyET
	}
	if !validCrop(c.CropFactor) {
		c.CropFactor = DefaultCropFactor
	}
	if !validCapacity(c.Capacity) {
		c.Capacity = DefaultCapacity
	}
	if c.Zones == nil {
		c.Zones = map[string]Zone{}
	}
	return c
}

// ZoneFor returns knobs for an engine station. Stations missing from the file
// use the file defaults with inches_per_hour unknown.
func (c Config) ZoneFor(stationID string) Zone {
	c = c.normalized()
	if z, ok := c.Zones[stationID]; ok {
		if !validCrop(z.CropFactor) {
			z.CropFactor = c.CropFactor
		}
		if !validCapacity(z.Capacity) {
			z.Capacity = c.Capacity
		}
		if z.InchesPerHour != nil && !validRate(*z.InchesPerHour) {
			z.InchesPerHour = nil
		}
		return z
	}
	return Zone{CropFactor: c.CropFactor, Capacity: c.Capacity}
}

// PollInterval is poll_seconds when set, otherwise poll_hours (default 6).
func (c Config) PollInterval() time.Duration {
	c = c.normalized()
	if c.PollSeconds > 0 {
		d := time.Duration(c.PollSeconds * float64(time.Second))
		if d <= 0 {
			return time.Second
		}
		return d
	}
	d := time.Duration(c.PollHours * float64(time.Hour))
	if d <= 0 {
		return time.Duration(DefaultPollHours * float64(time.Hour))
	}
	return d
}

func logConfig(format string, args ...any) {
	lg := configLog
	if lg == nil {
		lg = log.Default()
	}
	lg.Printf(format, args...)
}
