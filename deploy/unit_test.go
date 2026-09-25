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
	if strings.Contains(s, "chromium") {
		t.Fatal("irrigation unit must not embed chromium")
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
	if !strings.Contains(s, "LISTEN=") || !strings.Contains(s, "DRIVER=gpiocdev") {
		t.Fatal("env example incomplete (want LISTEN + DRIVER=gpiocdev)")
	}
}

func TestKioskUnitOptionalAndGraphical(t *testing.T) {
	b, err := os.ReadFile("zanjerito-kiosk.service")
	if err != nil {
		t.Fatal(err)
	}
	s := string(b)
	for _, need := range []string{
		"WantedBy=graphical.target",
		"mode=kiosk",
		"chromium-browser",
		"--kiosk",
		"After=network-online.target",
	} {
		if !strings.Contains(s, need) {
			t.Fatalf("kiosk unit missing %q", need)
		}
	}
	// Must not bind irrigation start to chromium or multi-user pull-in
	if strings.Contains(s, "WantedBy=multi-user.target") {
		t.Fatal("kiosk must not WantedBy multi-user (breaks headless)")
	}
	if strings.Contains(s, "Requires=zanjerito") || strings.Contains(s, "BindsTo=zanjerito") {
		t.Fatal("kiosk must not Require/BindsTo zanjerito")
	}
}

func TestInstallShKioskOptIn(t *testing.T) {
	b, err := os.ReadFile("install.sh")
	if err != nil {
		t.Fatal(err)
	}
	s := string(b)
	if !strings.Contains(s, `INSTALL_KIOSK:-0`) {
		t.Fatal("install.sh must gate kiosk on INSTALL_KIOSK")
	}
	if !strings.Contains(s, "zanjerito-kiosk.service") {
		t.Fatal("install.sh should know kiosk unit path")
	}
	// Default path must still install irrigation unit
	if !strings.Contains(s, "zanjerito.service") {
		t.Fatal("install.sh missing zanjerito.service")
	}
}
