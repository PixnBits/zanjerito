// Package gpio abstracts Raspberry Pi GPIO for zanjerito.
// Real hardware uses the Linux character device (go-gpiocdev); see docs/architecture.md.
package gpio

import "fmt"

// Level is the logical valve/PSU intent before active_low polarity is applied.
// On means "energize / water"; Off means "safe / de-energized".
type Level bool

const (
	Off Level = false
	On  Level = true
)

// Driver is the single writer surface for station and PSU lines.
type Driver interface {
	// Setup claims lines. activeLow matches config active_low (HIGH ≈ off when true).
	Setup(chip string, lines []Line, activeLow bool) error
	// Set drives one line by id (station id or "psu").
	Set(id string, level Level) error
	// AllOff de-energizes every configured station then the PSU (fail-safe).
	AllOff() error
	Close() error
}

// Line describes one GPIO output from config / pin-map.
type Line struct {
	ID  string
	BCM int
}

// New returns a driver by name.
//   - fake: logs only (dev / CI)
//   - lockout: like fake but Set/AllOff are no-ops that still log lockout (safe Pi bring-up)
//   - gpiocdev: real character-device driver (wired in a follow-up PR; stub rejects for now if unfinished)
func New(name string) (Driver, error) {
	switch name {
	case "fake":
		return NewFake(), nil
	case "lockout":
		return NewLockout(), nil
	case "gpiocdev":
		return NewGpiocdev()
	default:
		return nil, fmt.Errorf("unknown gpio driver %q (want fake|lockout|gpiocdev)", name)
	}
}
