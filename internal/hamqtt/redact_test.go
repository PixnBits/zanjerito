package hamqtt

import (
	"strings"
	"testing"
	"time"
)

const passwordMarker = "s3cret-marker"

func TestRedactURLHidesUserinfo(t *testing.T) {
	raw := "tcp://mqtt-user:" + passwordMarker + "@broker.example:1883"
	got := RedactURL(raw)
	if strings.Contains(got, passwordMarker) || strings.Contains(got, "mqtt-user") {
		t.Fatalf("userinfo leaked: %s", got)
	}
	if got != "tcp://REDACTED@broker.example:1883" {
		t.Fatalf("got %s", got)
	}

	if got := RedactURL("tcp://mqtt-user@broker.example:1883"); got != "tcp://REDACTED@broker.example:1883" {
		t.Fatalf("user-only: %s", got)
	}
	if got := RedactURL("tcp://:" + passwordMarker + "@broker.example:1883"); strings.Contains(got, passwordMarker) || got != "tcp://REDACTED@broker.example:1883" {
		t.Fatalf("password-only: %s", got)
	}
	if got := RedactURL("tcp://broker.example:1883"); got != "tcp://broker.example:1883" {
		t.Fatalf("no userinfo: %s", got)
	}
	for _, bad := range []string{"", "not a url", "://bad", "%zz", passwordMarker} {
		if got := RedactURL(bad); got != "<invalid url>" || strings.Contains(got, passwordMarker) {
			t.Fatalf("unparsable %q -> %s", bad, got)
		}
	}
}

func TestRedactReplacesSecrets(t *testing.T) {
	got := Redact("publish failed: "+passwordMarker+" then "+passwordMarker, "", passwordMarker)
	if strings.Contains(got, passwordMarker) {
		t.Fatalf("secret leaked: %s", got)
	}
	if got != "publish failed: REDACTED then REDACTED" {
		t.Fatalf("got %s", got)
	}
	if got := Redact("hello", ""); got != "hello" {
		t.Fatalf("empty secret changed the string: %q", got)
	}

	// A shorter prefix must not leave the tail of a longer secret.
	const longer = passwordMarker + "-tail"
	got = Redact("err "+longer, passwordMarker, longer)
	if strings.Contains(got, passwordMarker) || strings.Contains(got, "tail") {
		t.Fatalf("partial secret leaked: %s", got)
	}
}

func TestRedactErrorAndEncodedPassword(t *testing.T) {
	raw := "tcp://mqtt-user:" + passwordMarker + "@broker.example:1883"
	other := "other-secret-marker"
	errText := "connect " + raw + " failed, auth " + other
	got := Redact(errText, append([]string{other}, URLPasswords(raw)...)...)
	if strings.Contains(got, passwordMarker) || strings.Contains(got, other) {
		t.Fatalf("error string leaked: %s", got)
	}

	encoded := "tcp://mqtt-user:p%40ss@broker.example:1883"
	text := encoded + " decoded p@ss"
	got = Redact(text, URLPasswords(encoded)...)
	if strings.Contains(got, "p%40ss") || strings.Contains(got, "p@ss") {
		t.Fatalf("encoded password leaked: %s", got)
	}
}

func TestStartupLineOmitsPassword(t *testing.T) {
	cfg := Config{
		URL:             "tcp://mqtt-user:" + passwordMarker + "@broker.example:1883",
		Pass:            passwordMarker,
		ClientID:        "zanjerito-mqtt",
		HAURL:           "http://controller.example:8080/api/ha",
		PollInterval:    2 * time.Second,
		RefreshInterval: 5 * time.Minute,
	}
	line := StartupLine(cfg, "dev")
	if strings.Contains(line, passwordMarker) || strings.Contains(line, "mqtt-user") {
		t.Fatalf("startup line leaked credentials: %s", line)
	}
	if !strings.Contains(line, "user=(from url)") {
		t.Fatalf("user field: %s", line)
	}
	if strings.Contains(line, "user=(none)") {
		t.Fatalf("userinfo claimed none: %s", line)
	}
	if !strings.Contains(line, "broker=tcp://REDACTED@broker.example:1883") {
		t.Fatalf("broker: %s", line)
	}
	if !strings.Contains(line, "ha=http://controller.example:8080/api/ha") {
		t.Fatalf("ha: %s", line)
	}

	cfg.URL = "tcp://:" + passwordMarker + "@broker.example:1883"
	cfg.User = ""
	cfg.Pass = passwordMarker
	line = StartupLine(cfg, "dev")
	if strings.Contains(line, passwordMarker) || !strings.Contains(line, "user=(from url)") || strings.Contains(line, "user=(none)") {
		t.Fatalf("password-only url: %s", line)
	}

	cfg.URL = "tcp://broker.example:1883"
	cfg.User = ""
	cfg.Pass = passwordMarker
	line = StartupLine(cfg, "1.2.3")
	if strings.Contains(line, passwordMarker) || !strings.Contains(line, "user=(none)") {
		t.Fatalf("no user: %s", line)
	}
	if !strings.Contains(line, "zanjerito-mqtt 1.2.3 ") || !strings.Contains(line, "broker=tcp://broker.example:1883") {
		t.Fatalf("plain broker: %s", line)
	}

	cfg.User = "mqtt-user"
	cfg.URL = "tcp://other:" + passwordMarker + "@broker.example:1883"
	line = StartupLine(cfg, "dev")
	if strings.Contains(line, passwordMarker) || strings.Contains(line, "user=(none)") || !strings.Contains(line, "user=mqtt-user") {
		t.Fatalf("explicit user with url userinfo: %s", line)
	}
}
