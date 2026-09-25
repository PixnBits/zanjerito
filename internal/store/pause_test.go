package store

import (
	"path/filepath"
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
