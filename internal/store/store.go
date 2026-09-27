// Package store is the atomic JSON persistence layer (docs/decisions.md D4).
// Engine remains the validator and sole GPIO writer; this package only load/saves.
package store

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"syscall"

	"github.com/PixnBits/zanjerito/internal/engine"
)

// File is the on-disk document: pin map + stations + optional schedule placeholders.
type File struct {
	engine.Config
	Schedules []Schedule `json:"schedules,omitempty"`
}

// Step is one station run in a schedule itinerary (minutes, not bash dead-time).
type Step struct {
	StationID string `json:"station_id"`
	Minutes   int    `json:"minutes"`
}

// Schedule is a wall-clock program (America/Phoenix). Seasonal windows optional.
type Schedule struct {
	ID       string   `json:"id"`
	Enabled  bool     `json:"enabled"`
	Note     string   `json:"note,omitempty"`
	Weekdays []string `json:"weekdays,omitempty"` // mon..sun; empty = every day
	Start    string   `json:"start,omitempty"`    // HH:MM local
	Steps    []Step   `json:"steps,omitempty"`
	StartsOn string   `json:"starts_on,omitempty"` // YYYY-MM-DD optional
	EndsOn   string   `json:"ends_on,omitempty"`
}

// Load reads path, validates via engine (active_low required and true).
func Load(path string) (File, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return File{}, err
	}
	var f File
	if err := json.Unmarshal(b, &f); err != nil {
		return File{}, fmt.Errorf("store: decode %s: %w", path, err)
	}
	if err := f.NormalizeAndValidate(); err != nil {
		return File{}, err
	}
	return f, nil
}

// Save writes f atomically (temp file in the same directory, then rename).
func Save(path string, f File) error {
	if err := f.NormalizeAndValidate(); err != nil {
		return err
	}
	return atomicWriteJSON(path, f)
}

// SaveConfig persists an engine.Config (no schedules) atomically.
func SaveConfig(path string, cfg engine.Config) error {
	return Save(path, File{Config: cfg})
}

// ApplyAndSave replaces engine config only when Idle, then persists.
// Existing schedules are load-merged so an Idle pin-map apply does not wipe them.
// SaveConfig stays the no-schedule helper and is not used here.
// Busy watering returns engine.ErrBusy and does not write the file.
func ApplyAndSave(e *engine.Engine, path string, cfg engine.Config) error {
	existing, err := Load(path)
	var schedules []Schedule
	if err == nil {
		schedules = existing.Schedules
	} else if !os.IsNotExist(err) {
		return err
	}
	if err := e.ApplyConfig(cfg); err != nil {
		return err
	}
	// Persist the engine-normalized copy (defaults like max_on_sec), not the raw caller cfg.
	if err := cfg.NormalizeAndValidate(); err != nil {
		return err
	}
	return Save(path, File{Config: cfg, Schedules: schedules})
}

// HistoryPath is history.json beside the config file (same directory as pause.json).
func HistoryPath(configPath string) string {
	return filepath.Join(filepath.Dir(configPath), "history.json")
}

// AtomicWriteJSON writes v as indented JSON via a temp file in the same directory, then rename.
// After rename it fsyncs the directory so the new name survives a crash.
// Directory sync errors from filesystems that do not support it (EINVAL and the like) are ignored.
func AtomicWriteJSON(path string, v any) error {
	dir := filepath.Dir(path)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return err
	}
	b, err := json.MarshalIndent(v, "", "  ")
	if err != nil {
		return err
	}
	b = append(b, '\n')

	tmp, err := os.CreateTemp(dir, "."+filepath.Base(path)+".tmp-*")
	if err != nil {
		return fmt.Errorf("store: temp: %w", err)
	}
	tmpName := tmp.Name()
	cleanup := true
	defer func() {
		if cleanup {
			_ = os.Remove(tmpName)
		}
	}()

	if _, err := tmp.Write(b); err != nil {
		_ = tmp.Close()
		return err
	}
	if err := tmp.Sync(); err != nil {
		_ = tmp.Close()
		return err
	}
	if err := tmp.Close(); err != nil {
		return err
	}
	if err := os.Rename(tmpName, path); err != nil {
		return fmt.Errorf("store: rename: %w", err)
	}
	cleanup = false
	if err := syncDir(dir); err != nil {
		return err
	}
	return nil
}

// syncDir fsyncs dir after a rename. EINVAL-like errors mean this filesystem
// cannot sync a directory; the renamed file is already in place, so those are not failures.
func syncDir(dir string) error {
	f, err := os.Open(dir)
	if err != nil {
		return fmt.Errorf("store: sync dir: %w", err)
	}
	syncErr := f.Sync()
	closeErr := f.Close()
	if syncErr != nil && !dirSyncUnsupported(syncErr) {
		return fmt.Errorf("store: sync dir: %w", syncErr)
	}
	if closeErr != nil && !dirSyncUnsupported(closeErr) {
		return fmt.Errorf("store: sync dir: %w", closeErr)
	}
	return nil
}

func dirSyncUnsupported(err error) bool {
	return errors.Is(err, syscall.EINVAL) ||
		errors.Is(err, syscall.ENOTSUP) ||
		errors.Is(err, syscall.EOPNOTSUPP)
}

func atomicWriteJSON(path string, v any) error {
	return AtomicWriteJSON(path, v)
}
