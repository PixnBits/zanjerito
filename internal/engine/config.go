package engine

import (
	"encoding/json"
	"fmt"
	"os"
	"time"
)

// SequencingMode matches docs/decisions.md D2.
type SequencingMode string

const (
	SequenceOverlap SequencingMode = "overlap"
	SequenceIsolate SequencingMode = "isolate"
)

// Config is the runtime pin map + safety knobs (atomic JSON later owns persistence).
type Config struct {
	Chip       string          `json:"chip"`
	ActiveLow  *bool           `json:"active_low"` // pointer: missing is invalid
	Timezone   string          `json:"timezone"`
	MaxOnSec   int             `json:"max_on_sec"`
	Sequencing SequencingConfig `json:"sequencing"`
	Power      StationConfig   `json:"power"`
	Stations   []StationConfig `json:"stations"`
}

type SequencingConfig struct {
	Mode      SequencingMode `json:"mode"`
	OverlapMS int            `json:"overlap_ms"`
}

type StationConfig struct {
	ID    string `json:"id"`
	Title string `json:"title"`
	BCM   int    `json:"bcm"`
}

// LoadConfig reads JSON and validates Architect rules (refuse active_low missing/false).
func LoadConfig(path string) (Config, error) {
	b, err := os.ReadFile(path)
	if err != nil {
		return Config{}, err
	}
	var c Config
	if err := json.Unmarshal(b, &c); err != nil {
		return Config{}, err
	}
	if err := c.NormalizeAndValidate(); err != nil {
		return Config{}, err
	}
	return c, nil
}

func (c *Config) NormalizeAndValidate() error {
	if c.ActiveLow == nil {
		return fmt.Errorf("config: active_low is required")
	}
	if !*c.ActiveLow {
		return fmt.Errorf("config: active_low=false refused (this hardware is active-low)")
	}
	if c.Chip == "" {
		return fmt.Errorf("config: chip is required")
	}
	if c.Power.ID == "" {
		return fmt.Errorf("config: power.id is required")
	}
	if c.MaxOnSec <= 0 {
		c.MaxOnSec = 900
	}
	if c.Sequencing.Mode == "" {
		c.Sequencing.Mode = SequenceOverlap
	}
	if c.Sequencing.Mode != SequenceOverlap && c.Sequencing.Mode != SequenceIsolate {
		return fmt.Errorf("config: unknown sequencing.mode %q", c.Sequencing.Mode)
	}
	if c.Sequencing.Mode == SequenceOverlap && c.Sequencing.OverlapMS <= 0 {
		c.Sequencing.OverlapMS = 2000
	}
	if c.Timezone == "" {
		c.Timezone = "America/Phoenix"
	}
	ids := map[string]struct{}{c.Power.ID: {}}
	for _, s := range c.Stations {
		if s.ID == "" {
			return fmt.Errorf("config: station missing id")
		}
		if _, ok := ids[s.ID]; ok {
			return fmt.Errorf("config: duplicate id %q", s.ID)
		}
		ids[s.ID] = struct{}{}
	}
	return nil
}

// Validate is a convenience for already-normalized configs.
func (c Config) Validate() error {
	cc := c
	return cc.NormalizeAndValidate()
}

// MaxOn returns the per-station cap.
func (c Config) MaxOn() time.Duration {
	sec := c.MaxOnSec
	if sec <= 0 {
		sec = 900
	}
	return time.Duration(sec) * time.Second
}
