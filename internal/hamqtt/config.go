package hamqtt

import (
	"fmt"
	"strings"
	"time"
)

const (
	defaultClientID = "zanjerito-mqtt"
	defaultHAURL    = "http://127.0.0.1:8080/api/ha"
	defaultPoll     = 2 * time.Second
	defaultRefresh  = 5 * time.Minute
	minPoll         = 500 * time.Millisecond
	maxPoll         = 10 * time.Second
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
// MQTT_URL is required. MQTT_PASS is not trimmed (passwords may have spaces).
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
	if cfg.HAURL == "" {
		cfg.HAURL = defaultHAURL
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
