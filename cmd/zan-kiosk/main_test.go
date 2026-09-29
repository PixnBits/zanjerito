package main

import (
	"flag"
	"strings"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/kiosk"
)

func TestFlagsDefaultReadOnly(t *testing.T) {
	fs := flag.NewFlagSet("zan-kiosk", flag.ContinueOnError)
	opt := defineFlags(fs)
	if err := fs.Parse(nil); err != nil {
		t.Fatal(err)
	}
	if *opt.allowWrites {
		t.Fatal("writes enabled by default")
	}
	if *opt.api != "http://127.0.0.1:8080" || *opt.sink != "mem" || *opt.fixtureStatus != "idle" {
		t.Fatalf("api %s sink %s status %s", *opt.api, *opt.sink, *opt.fixtureStatus)
	}
	if *opt.duration != 0 || *opt.anim || *opt.rotate != 0 {
		t.Fatal("unexpected defaults")
	}
	usage := fs.Lookup("allow-writes").Usage
	if !strings.Contains(usage, "Not for use against a live daemon") {
		t.Fatal(usage)
	}
	if _, ok := chooseActions(false, *opt.api).(kiosk.ReadOnlyActions); !ok {
		t.Fatal("default actions must be read-only")
	}
	got := chooseActions(true, *opt.api)
	httpActs, ok := got.(kiosk.HTTPActions)
	if !ok || httpActs.BaseURL != *opt.api || httpActs.HTTP == nil || httpActs.HTTP.Timeout != 2*time.Second {
		t.Fatalf("%#v", got)
	}

	fs2 := flag.NewFlagSet("zan-kiosk", flag.ContinueOnError)
	opt2 := defineFlags(fs2)
	if err := fs2.Parse([]string{"-allow-writes", "-anim", "-rotate", "90"}); err != nil {
		t.Fatal(err)
	}
	if !*opt2.allowWrites || !*opt2.anim || *opt2.rotate != 90 {
		t.Fatal("flags did not parse")
	}
}
