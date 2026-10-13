package hamqtt

import (
	"strings"
	"testing"
	"time"
)

func envMap(m map[string]string) func(string) string {
	return func(k string) string { return m[k] }
}

func TestLoadConfigDefaults(t *testing.T) {
	cfg, err := LoadConfig(envMap(map[string]string{
		"MQTT_URL": "tcp://127.0.0.1:1883",
	}))
	if err != nil {
		t.Fatal(err)
	}
	if cfg.URL != "tcp://127.0.0.1:1883" {
		t.Fatalf("URL %q", cfg.URL)
	}
	if cfg.User != "" || cfg.Pass != "" {
		t.Fatalf("user/pass should be empty, got %q / %d bytes", cfg.User, len(cfg.Pass))
	}
	if cfg.ClientID != "zanjerito-mqtt" {
		t.Fatalf("ClientID %q", cfg.ClientID)
	}
	if cfg.HAURL != "http://127.0.0.1:8080/api/ha" {
		t.Fatalf("HAURL %q", cfg.HAURL)
	}
	if cfg.PollInterval != 2*time.Second {
		t.Fatalf("PollInterval %s", cfg.PollInterval)
	}
	if cfg.RefreshInterval != 5*time.Minute {
		t.Fatalf("RefreshInterval %s", cfg.RefreshInterval)
	}
	if cfg.DiscoveryTopic != TopicDiscovery || cfg.StateTopic != TopicState ||
		cfg.AvailabilityTopic != TopicAvailability || cfg.HAStatusTopic != TopicHAStatus {
		t.Fatalf("topics %+v", cfg)
	}
}

func TestLoadConfigCustom(t *testing.T) {
	cfg, err := LoadConfig(envMap(map[string]string{
		"MQTT_URL":              "tcp://<broker-host>:1883",
		"MQTT_USER":             "mqtt-user",
		"MQTT_PASS":             " pass with spaces ",
		"MQTT_CLIENT_ID":        "custom-id",
		"ZANJERITO_HA_URL":      "http://<controller-host>:8080/api/ha",
		"MQTT_POLL_INTERVAL":    "1s",
		"MQTT_REFRESH_INTERVAL": "1m",
	}))
	if err != nil {
		t.Fatal(err)
	}
	if cfg.User != "mqtt-user" {
		t.Fatalf("User %q", cfg.User)
	}
	if cfg.Pass != " pass with spaces " {
		t.Fatal("password must not be trimmed")
	}
	if cfg.ClientID != "custom-id" {
		t.Fatalf("ClientID %q", cfg.ClientID)
	}
	if cfg.HAURL != "http://<controller-host>:8080/api/ha" {
		t.Fatalf("HAURL %q", cfg.HAURL)
	}
	if cfg.PollInterval != time.Second || cfg.RefreshInterval != time.Minute {
		t.Fatalf("intervals poll=%s refresh=%s", cfg.PollInterval, cfg.RefreshInterval)
	}
}

func TestLoadConfigValidation(t *testing.T) {
	_, err := LoadConfig(envMap(nil))
	if err == nil || !strings.Contains(err.Error(), "MQTT_URL") {
		t.Fatalf("missing URL: %v", err)
	}
	_, err = LoadConfig(envMap(map[string]string{"MQTT_URL": "   "}))
	if err == nil || !strings.Contains(err.Error(), "MQTT_URL") {
		t.Fatalf("blank URL: %v", err)
	}

	for _, poll := range []string{"499ms", "10s1ns", "11s", "0s", "-1s", "foo"} {
		_, err := LoadConfig(envMap(map[string]string{
			"MQTT_URL":           "tcp://127.0.0.1:1883",
			"MQTT_POLL_INTERVAL": poll,
		}))
		if err == nil {
			t.Fatalf("MQTT_POLL_INTERVAL=%s: want error", poll)
		}
	}
	for _, poll := range []string{"500ms", "2s", "10s"} {
		if _, err := LoadConfig(envMap(map[string]string{
			"MQTT_URL":           "tcp://127.0.0.1:1883",
			"MQTT_POLL_INTERVAL": poll,
		})); err != nil {
			t.Fatalf("MQTT_POLL_INTERVAL=%s: %v", poll, err)
		}
	}

	_, err = LoadConfig(envMap(map[string]string{
		"MQTT_URL":              "tcp://127.0.0.1:1883",
		"MQTT_REFRESH_INTERVAL": "nope",
	}))
	if err == nil {
		t.Fatal("bad refresh: want error")
	}
	_, err = LoadConfig(envMap(map[string]string{
		"MQTT_URL":              "tcp://127.0.0.1:1883",
		"MQTT_REFRESH_INTERVAL": "-1s",
	}))
	if err == nil {
		t.Fatal("negative refresh: want error")
	}
}

func TestLoadConfigNilGetenv(t *testing.T) {
	if _, err := LoadConfig(nil); err == nil {
		t.Fatal("nil getenv: want error")
	}
}
