// Package store is the atomic JSON persistence layer (docs/decisions.md D4).
// Engine remains the validator and sole GPIO writer; this package only load/saves.
package store

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"

	"github.com/PixnBits/zanjerito/internal/engine"
)

// File is the on-disk document: pin map + stations + optional schedule placeholders.
type File struct {
	engine.Config
	Schedules []Schedule `json:"schedules,omitempty"`
}

// Schedule is a minimal placeholder until the scheduler PR.
type Schedule struct {
	ID      string `json:"id"`
	Enabled bool   `json:"enabled"`
	Note    string `json:"note,omitempty"`
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

func atomicWriteJSON(path string, v any) error {
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
	return nil
}
