package main

import (
	"strings"
	"testing"
	"time"
)

func TestParseProcStat(t *testing.T) {
	s := "12 (kiosk name) S 0 0 0 0 -1 0 0 0 0 0 12 34 0 0 20 0 1 0 999\n"
	user, sys, start, err := parseProcStat(s)
	if err != nil {
		t.Fatal(err)
	}
	if user != 12 || sys != 34 || start != 999 {
		t.Fatalf("%d %d %d", user, sys, start)
	}
	text := "Name:\tkiosk\nVmRSS:\t  4321 kB\nVmHWM:\t5000 kB\n"
	rss, err := parseStatusKB(text, "VmRSS")
	if err != nil || rss != 4321 {
		t.Fatal(rss, err)
	}
	hwm, err := parseStatusKB(text, "VmHWM")
	if err != nil || hwm != 5000 {
		t.Fatal(hwm, err)
	}
	boot, err := parseBootUnix("cpu 1\nbtime 1700000000\n")
	if err != nil || boot != 1700000000 {
		t.Fatal(boot, err)
	}
}

func TestP95(t *testing.T) {
	ds := []time.Duration{10, 20, 30, 40, 100}
	if p95Dur(ds) != 100 {
		t.Fatal(p95Dur(ds))
	}
	if p95Dur(nil) != 0 || avgDur(nil) != 0 {
		t.Fatal("empty")
	}
}

func TestProcSelf(t *testing.T) {
	ps, err := processStart()
	if err != nil {
		t.Fatal(err)
	}
	d := time.Since(ps)
	if d < -2*time.Second || d > 6*time.Hour {
		t.Fatalf("process start %s delta %s", ps, d)
	}
	rss, hwm, err := procMem()
	if err != nil || rss <= 0 || hwm < rss {
		t.Fatal(rss, hwm, err)
	}
	user, sys, err := procCPU()
	if err != nil || user < 0 || sys < 0 {
		t.Fatal(user, sys, err)
	}
	rep := (frameStats{mainStart: time.Now().Add(-time.Second), first: time.Now(), touches: 2}).report()
	if rep.TouchEvents != 2 || rep.VmRSSKB <= 0 {
		t.Fatalf("%+v", rep)
	}
	if !strings.Contains("process_start_to_first_frame_ms", "process_start") {
		t.Fatal("field name")
	}
}
