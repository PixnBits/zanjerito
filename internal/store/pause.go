package store

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

// PauseState is the durable "don't water for a while" flag (sidecar JSON).
// Active + Until==nil means until further notice. Timed pauses auto-expire on read.
type PauseState struct {
	Active bool       `json:"paused"`
	Until  *time.Time `json:"paused_until"` // RFC3339; null = indefinite when Active
	Reason string     `json:"reason,omitempty"`
}

// PausePath is pause.json beside the config file.
func PausePath(configPath string) string {
	return filepath.Join(filepath.Dir(configPath), "pause.json")
}

// LoadPause reads pause.json. Missing file → inactive. Expired timed pause → cleared + rewritten.
func LoadPause(configPath string, now time.Time) (PauseState, error) {
	path := PausePath(configPath)
	b, err := os.ReadFile(path)
	if err != nil {
		if os.IsNotExist(err) {
			return PauseState{}, nil
		}
		return PauseState{}, err
	}
	var p PauseState
	if err := json.Unmarshal(b, &p); err != nil {
		return PauseState{}, fmt.Errorf("store: decode pause %s: %w", path, err)
	}
	if p.Active && p.Until != nil && !p.Until.After(now) {
		cleared := PauseState{}
		if err := SavePause(configPath, cleared); err != nil {
			return PauseState{}, err
		}
		return cleared, nil
	}
	return p, nil
}

// SavePause writes pause.json atomically (same temp+rename as config).
func SavePause(configPath string, p PauseState) error {
	return atomicWriteJSON(PausePath(configPath), p)
}

// PauseView is the API/status shape after expire-normalize.
type PauseView struct {
	Paused      bool    `json:"paused"`
	PausedUntil *string `json:"paused_until"` // RFC3339 or null
	Reason      string  `json:"reason,omitempty"`
}

// View converts PauseState to API fields (paused_until null when indefinite or inactive).
// loc is used to format paused_until; nil means UTC.
func (p PauseState) View(loc *time.Location) PauseView {
	v := PauseView{Paused: p.Active, Reason: p.Reason}
	if p.Active && p.Until != nil {
		if loc == nil {
			loc = time.UTC
		}
		s := p.Until.In(loc).Format(time.RFC3339)
		v.PausedUntil = &s
	}
	return v
}
