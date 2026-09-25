package store

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestPauseSaveLoadIndefinite(t *testing.T) {
	dir := t.TempDir()
	cfg := filepath.Join(dir, "config.json")
	in := PauseState{Active: true, Reason: "rain"}
	if err := SavePause(cfg, in); err != nil {
		t.Fatal(err)
	}
	out, err := LoadPause(cfg, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if !out.Active || out.Until != nil || out.Reason != "rain" {
		t.Fatalf("got %+v", out)
	}
}

func TestPauseAutoExpire(t *testing.T) {
	dir := t.TempDir()
	cfg := filepath.Join(dir, "config.json")
	past := time.Now().Add(-time.Minute)
	if err := SavePause(cfg, PauseState{Active: true, Until: &past, Reason: "gone"}); err != nil {
		t.Fatal(err)
	}
	out, err := LoadPause(cfg, time.Now())
	if err != nil {
		t.Fatal(err)
	}
	if out.Active {
		t.Fatalf("expected expired clear, got %+v", out)
	}
	// disk rewritten inactive
	out2, err := LoadPause(cfg, time.Now())
	if err != nil || out2.Active {
		t.Fatalf("disk %+v err %v", out2, err)
	}
}

func TestPauseMissingFile(t *testing.T) {
	out, err := LoadPause(filepath.Join(t.TempDir(), "cfg.json"), time.Now())
	if err != nil || out.Active {
		t.Fatalf("%+v %v", out, err)
	}
}

func TestPauseRoundTripKeepsOffset(t *testing.T) {
	dir := t.TempDir()
	cfg := filepath.Join(dir, "config.json")
	loc, err := time.LoadLocation("America/Phoenix")
	if err != nil {
		t.Fatal(err)
	}
	until := time.Date(2026, 9, 27, 6, 0, 0, 0, loc)
	in := PauseState{Active: true, Until: &until, Reason: "rain"}
	if err := SavePause(cfg, in); err != nil {
		t.Fatal(err)
	}
	out, err := LoadPause(cfg, time.Date(2026, 9, 25, 15, 0, 0, 0, loc))
	if err != nil {
		t.Fatal(err)
	}
	if !out.Active || out.Until == nil || !out.Until.Equal(until) {
		t.Fatalf("got %+v want until %s", out, until)
	}
	v := out.View(loc)
	if v.PausedUntil == nil || !strings.HasSuffix(*v.PausedUntil, "-07:00") {
		t.Fatalf("view %v", v.PausedUntil)
	}
}

func TestPauseLoadsLegacyUTCFile(t *testing.T) {
	dir := t.TempDir()
	cfg := filepath.Join(dir, "config.json")
	future := time.Date(2026, 12, 1, 12, 0, 0, 0, time.UTC)
	body := `{"paused":true,"paused_until":"` + future.Format(time.RFC3339) + `","reason":"rain"}`
	if err := os.WriteFile(PausePath(cfg), []byte(body), 0o644); err != nil {
		t.Fatal(err)
	}
	out, err := LoadPause(cfg, time.Date(2026, 9, 25, 15, 0, 0, 0, time.UTC))
	if err != nil {
		t.Fatal(err)
	}
	if !out.Active || out.Until == nil || !out.Until.Equal(future) {
		t.Fatalf("legacy load %+v", out)
	}
	if !strings.HasSuffix(future.Format(time.RFC3339), "Z") {
		t.Fatal("fixture must be Z")
	}
}
