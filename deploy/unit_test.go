package deploy_test

import (
	"os"
	"strings"
	"testing"
)

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
	if !strings.Contains(s, "LISTEN=") || !strings.Contains(s, "DRIVER=fake") {
		t.Fatal("env example incomplete")
	}
}
