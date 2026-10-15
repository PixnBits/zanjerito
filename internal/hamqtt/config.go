package hamqtt

import (
	"errors"
	"fmt"
	"net/url"
	"strings"
	"time"
)

const (
	defaultClientID = "zanjerito-mqtt"
	defaultPoll     = 2 * time.Second
	defaultRefresh  = 5 * time.Minute
	minPoll         = 500 * time.Millisecond
	maxPoll         = 10 * time.Second

	haURLRequired = "ZANJERITO_HA_URL is required (set it to the daemon LISTEN address, e.g. http://<controller-host>:8080/api/ha)"
)

// Config is the sidecar process settings. Topics are fixed.
type Config struct {
	URL               string
	User              string
	Pass              string
	ClientID          string
	HAURL             string
	PollInterval      time.Duration
	RefreshInterval   time.Duration
	DiscoveryTopic    string
	StateTopic        string
	AvailabilityTopic string
	HAStatusTopic     string
}

// LoadConfig reads sidecar settings from env. getenv is typically os.Getenv.
// MQTT_URL and ZANJERITO_HA_URL are required. MQTT_PASS is not trimmed
// (passwords may have spaces). ZANJERITO_HA_URL must be an http or https URL.
func LoadConfig(getenv func(string) string) (Config, error) {
	if getenv == nil {
		return Config{}, fmt.Errorf("getenv is required")
	}
	get := func(key string) string {
		v := getenv(key)
		if key == "MQTT_PASS" {
			return v
		}
		return strings.TrimSpace(v)
	}

	cfg := Config{
		URL:               get("MQTT_URL"),
		User:              get("MQTT_USER"),
		Pass:              get("MQTT_PASS"),
		ClientID:          get("MQTT_CLIENT_ID"),
		HAURL:             get("ZANJERITO_HA_URL"),
		DiscoveryTopic:    TopicDiscovery,
		StateTopic:        TopicState,
		AvailabilityTopic: TopicAvailability,
		HAStatusTopic:     TopicHAStatus,
	}
	if cfg.URL == "" {
		return Config{}, fmt.Errorf("MQTT_URL is required")
	}
	if cfg.ClientID == "" {
		cfg.ClientID = defaultClientID
	}
	if err := validateHAURL(cfg.HAURL); err != nil {
		return Config{}, err
	}

	poll, err := parseDuration(get("MQTT_POLL_INTERVAL"), defaultPoll)
	if err != nil {
		return Config{}, fmt.Errorf("MQTT_POLL_INTERVAL: %w", err)
	}
	if poll < minPoll || poll > maxPoll {
		return Config{}, fmt.Errorf("MQTT_POLL_INTERVAL must be between %s and %s", minPoll, maxPoll)
	}
	cfg.PollInterval = poll

	refresh, err := parseDuration(get("MQTT_REFRESH_INTERVAL"), defaultRefresh)
	if err != nil {
		return Config{}, fmt.Errorf("MQTT_REFRESH_INTERVAL: %w", err)
	}
	if refresh < 0 {
		return Config{}, fmt.Errorf("MQTT_REFRESH_INTERVAL must be >= 0")
	}
	cfg.RefreshInterval = refresh
	return cfg, nil
}

func validateHAURL(raw string) error {
	if raw == "" {
		return errors.New(haURLRequired)
	}
	u, err := url.Parse(raw)
	if err != nil || u.Host == "" || !isHTTPScheme(u.Scheme) {
		return fmt.Errorf("ZANJERITO_HA_URL must be an http or https URL")
	}
	return nil
}

func isHTTPScheme(s string) bool {
	return strings.EqualFold(s, "http") || strings.EqualFold(s, "https")
}

func parseDuration(s string, fallback time.Duration) (time.Duration, error) {
	if s == "" {
		return fallback, nil
	}
	d, err := time.ParseDuration(s)
	if err != nil {
		return 0, err
	}
	return d, nil
}
