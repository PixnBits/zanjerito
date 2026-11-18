package deploy_test

import (
	"os"
	"regexp"
	"strings"
	"testing"
)

// RFC1918 literals only; placeholders such as <broker-host> must not match.
var rfc1918Literal = regexp.MustCompile(`\b(?:10\.\d{1,3}\.\d{1,3}\.\d{1,3}|192\.168\.\d{1,3}\.\d{1,3}|172\.(?:1[6-9]|2[0-9]|3[0-1])\.\d{1,3}\.\d{1,3})\b`)

func TestServiceForcesAllOff(t *testing.T) {
	b, err := os.ReadFile("zanjerito.service")
	if err != nil {
		t.Fatal(err)
	}
	s := string(b)
	for _, need := range []string{
		"ExecStart=/opt/zanjerito/zanjerito",
		"ExecStop=/bin/kill -s TERM $MAINPID",
		"KillSignal=SIGTERM",
		"/opt/zanjerito/config.json",
		"WantedBy=multi-user.target",
	} {
		if !strings.Contains(s, need) {
			t.Fatalf("unit missing %q", need)
		}
	}
	if strings.Contains(s, "gpiocdev") {
		t.Fatal("unit must not require gpiocdev yet")
	}
}

func TestEnvExampleIsLAN(t *testing.T) {
	b, err := os.ReadFile("zanjerito.env.example")
	if err != nil {
		t.Fatal(err)
	}
	s := string(b)
	if strings.Contains(s, "0.0.0.0") {
		t.Fatal("do not advertise 0.0.0.0")
	}
	if !strings.Contains(s, "LISTEN=") || !strings.Contains(s, "DRIVER=dualrun") {
		t.Fatal("env example incomplete")
	}
}

func TestMQTTServiceIsolated(t *testing.T) {
	b, err := os.ReadFile("zanjerito-mqtt.service")
	if err != nil {
		t.Fatal(err)
	}
	s := string(b)
	for _, need := range []string{
		"EnvironmentFile=/opt/zanjerito/zanjerito-mqtt.env",
		"Restart=on-failure",
		"ExecStart=/opt/zanjerito/zanjerito-mqtt",
		"WantedBy=multi-user.target",
		"DynamicUser=yes",
		"NoNewPrivileges=yes",
		"ProtectSystem=strict",
		"RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX",
		"CapabilityBoundingSet=",
		"AmbientCapabilities=",
		"SystemCallFilter=@system-service",
		"UMask=0077",
	} {
		if !hasExactLine(s, need) {
			t.Fatalf("mqtt unit missing line %q", need)
		}
	}
	if !strings.Contains(s, "EnvironmentFile is read by systemd as root") {
		t.Fatal("mqtt unit missing EnvironmentFile ownership comment")
	}
	if strings.Contains(s, "zanjerito.service") {
		t.Fatal("mqtt unit must not reference zanjerito.service")
	}
	if strings.Contains(strings.ToLower(s), "kiosk") {
		t.Fatal("mqtt unit must not reference kiosk")
	}
}

func hasExactLine(s, want string) bool {
	for _, line := range strings.Split(s, "\n") {
		if strings.TrimSpace(line) == want {
			return true
		}
	}
	return false
}

func TestMQTTEnvExample(t *testing.T) {
	b, err := os.ReadFile("zanjerito-mqtt.env.example")
	if err != nil {
		t.Fatal(err)
	}
	s := string(b)
	for _, k := range []string{
		"MQTT_URL=",
		"MQTT_USER=",
		"MQTT_PASS=",
		"MQTT_CLIENT_ID=",
		"ZANJERITO_HA_URL=",
		"MQTT_POLL_INTERVAL=",
		"MQTT_REFRESH_INTERVAL=",
	} {
		if !strings.Contains(s, k) {
			t.Fatalf("mqtt env example missing %q", k)
		}
	}
	if strings.Contains(s, "0.0.0.0") {
		t.Fatal("do not advertise 0.0.0.0")
	}
	if rfc1918Literal.MatchString(s) {
		t.Fatal("mqtt env example must not contain RFC1918 IP literals")
	}
}
