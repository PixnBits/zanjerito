package hamqtt

import (
	"bytes"
	"strconv"
	"strings"
	"testing"
	"time"
)

func envMap(m map[string]string) func(string) string {
	return func(k string) string { return m[k] }
}

func TestLoadConfigDefaults(t *testing.T) {
	cfg, err := LoadConfig(envMap(map[string]string{
		"MQTT_URL":         "tcp://127.0.0.1:1883",
		"ZANJERITO_HA_URL": "http://127.0.0.1:8080/api/ha",
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
			"ZANJERITO_HA_URL":   "http://127.0.0.1:8080/api/ha",
			"MQTT_POLL_INTERVAL": poll,
		}))
		if err == nil {
			t.Fatalf("MQTT_POLL_INTERVAL=%s: want error", poll)
		}
	}
	for _, poll := range []string{"500ms", "2s", "10s"} {
		if _, err := LoadConfig(envMap(map[string]string{
			"MQTT_URL":           "tcp://127.0.0.1:1883",
			"ZANJERITO_HA_URL":   "http://127.0.0.1:8080/api/ha",
			"MQTT_POLL_INTERVAL": poll,
		})); err != nil {
			t.Fatalf("MQTT_POLL_INTERVAL=%s: %v", poll, err)
		}
	}

	_, err = LoadConfig(envMap(map[string]string{
		"MQTT_URL":              "tcp://127.0.0.1:1883",
		"ZANJERITO_HA_URL":      "http://127.0.0.1:8080/api/ha",
		"MQTT_REFRESH_INTERVAL": "nope",
	}))
	if err == nil {
		t.Fatal("bad refresh: want error")
	}
	_, err = LoadConfig(envMap(map[string]string{
		"MQTT_URL":              "tcp://127.0.0.1:1883",
		"ZANJERITO_HA_URL":      "http://127.0.0.1:8080/api/ha",
		"MQTT_REFRESH_INTERVAL": "-1s",
	}))
	if err == nil {
		t.Fatal("negative refresh: want error")
	}
}

func TestLoadConfigRequiresHAURL(t *testing.T) {
	const want = "ZANJERITO_HA_URL is required (set it to the daemon LISTEN address, e.g. http://<controller-host>:8080/api/ha)"
	for _, ha := range []string{"", "   "} {
		_, err := LoadConfig(envMap(map[string]string{
			"MQTT_URL":         "tcp://127.0.0.1:1883",
			"ZANJERITO_HA_URL": ha,
		}))
		if err == nil || err.Error() != want {
			t.Fatalf("HA URL %q: %v", ha, err)
		}
	}
	for _, ha := range []string{
		"not-a-url",
		"/api/ha",
		"ftp://controller.example/api/ha",
		"mqtt://broker.example:1883",
		"http://",
		"://controller.example/api/ha",
	} {
		_, err := LoadConfig(envMap(map[string]string{
			"MQTT_URL":         "tcp://127.0.0.1:1883",
			"ZANJERITO_HA_URL": ha,
		}))
		if err == nil || !strings.Contains(err.Error(), "http or https") {
			t.Fatalf("HA URL %q: %v", ha, err)
		}
	}
	cfg, err := LoadConfig(envMap(map[string]string{
		"MQTT_URL":         "tcp://127.0.0.1:1883",
		"ZANJERITO_HA_URL": "https://controller.example/api/ha",
	}))
	if err != nil {
		t.Fatal(err)
	}
	if cfg.HAURL != "https://controller.example/api/ha" {
		t.Fatalf("HAURL %q", cfg.HAURL)
	}
}

func TestLoadConfigNilGetenv(t *testing.T) {
	if _, err := LoadConfig(nil); err == nil {
		t.Fatal("nil getenv: want error")
	}
}

func TestLoadConfigCredentialCaps(t *testing.T) {
	if MaxMQTTClientIDLen != 64 || MaxMQTTUserLen != 256 || MaxMQTTPassLen != 256 {
		t.Fatalf("caps client=%d user=%d pass=%d", MaxMQTTClientIDLen, MaxMQTTUserLen, MaxMQTTPassLen)
	}
	cases := []struct {
		key   string
		limit int
		fill  byte
	}{
		{"MQTT_CLIENT_ID", MaxMQTTClientIDLen, 'c'},
		{"MQTT_USER", MaxMQTTUserLen, 'u'},
		{"MQTT_PASS", MaxMQTTPassLen, 'p'},
	}
	for _, tc := range cases {
		okVal := cappedValue(tc.limit, tc.fill, tc.key == "MQTT_PASS")
		over := cappedValue(tc.limit+1, tc.fill, tc.key == "MQTT_PASS")
		cfg, err := LoadConfig(credEnv(tc.key, okVal))
		if err != nil {
			t.Fatalf("%s at cap: %v", tc.key, err)
		}
		got := cfgField(cfg, tc.key)
		if got != okVal {
			t.Fatalf("%s at cap: value changed (len %d)", tc.key, len(got))
		}
		_, err = LoadConfig(credEnv(tc.key, over))
		if err == nil {
			t.Fatalf("%s cap+1: got nil error", tc.key)
		}
		msg := err.Error()
		if !strings.Contains(msg, tc.key) || !strings.Contains(msg, strconv.Itoa(tc.limit)) {
			t.Fatalf("%s cap+1: error %q", tc.key, msg)
		}
		if strings.Contains(msg, "ZQMARK") || strings.Contains(msg, over) {
			t.Fatalf("%s cap+1: error contains the value", tc.key)
		}
	}
	// 32 × U+00E1 is 64 bytes; 33 × is 66 bytes (still under a 64-rune cap).
	at := strings.Repeat("á", MaxMQTTClientIDLen/2)
	over := strings.Repeat("á", MaxMQTTClientIDLen/2+1)
	if len(at) != MaxMQTTClientIDLen || len(over) <= MaxMQTTClientIDLen {
		t.Fatalf("byte lengths at=%d over=%d", len(at), len(over))
	}
	if _, err := LoadConfig(credEnv("MQTT_CLIENT_ID", at)); err != nil {
		t.Fatalf("64-byte client id: %v", err)
	}
	if _, err := LoadConfig(credEnv("MQTT_CLIENT_ID", over)); err == nil {
		t.Fatal("66-byte client id: got nil error")
	}
}

func credEnv(key, val string) func(string) string {
	m := map[string]string{
		"MQTT_URL":         "tcp://127.0.0.1:1883",
		"ZANJERITO_HA_URL": "http://127.0.0.1:8080/api/ha",
		key:                val,
	}
	return envMap(m)
}

func cappedValue(n int, fill byte, pad bool) string {
	b := bytes.Repeat([]byte{fill}, n)
	marker := []byte("ZQMARK")
	if n >= len(marker) {
		copy(b[n/2:], marker)
	}
	if pad && n >= 2 {
		b[0] = ' '
		b[n-1] = ' '
	}
	return string(b)
}

func cfgField(cfg Config, key string) string {
	switch key {
	case "MQTT_CLIENT_ID":
		return cfg.ClientID
	case "MQTT_USER":
		return cfg.User
	case "MQTT_PASS":
		return cfg.Pass
	default:
		return ""
	}
}
