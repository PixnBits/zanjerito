package gpio

import (
	"fmt"
	"log"
	"sync"
)

type fakeDriver struct {
	mu        sync.Mutex
	activeLow bool
	lines     map[string]Line
	state     map[string]Level
	name      string
}

// NewFake returns a logging driver for development and tests.
func NewFake() Driver {
	return &fakeDriver{
		lines: make(map[string]Line),
		state: make(map[string]Level),
		name:  "fake",
	}
}

// NewLockout returns a driver that never claims hardware; Sets are logged as locked out.
func NewLockout() Driver {
	return &fakeDriver{
		lines: make(map[string]Line),
		state: make(map[string]Level),
		name:  "lockout",
	}
}

func (d *fakeDriver) Setup(chip string, lines []Line, activeLow bool) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.activeLow = activeLow
	for _, ln := range lines {
		if ln.ID == "" {
			return fmt.Errorf("%s: line missing id", d.name)
		}
		d.lines[ln.ID] = ln
		d.state[ln.ID] = Off
	}
	log.Printf("gpio/%s: setup chip=%s activeLow=%v lines=%d", d.name, chip, activeLow, len(lines))
	return nil
}

func (d *fakeDriver) Set(id string, level Level) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	if _, ok := d.lines[id]; !ok {
		return fmt.Errorf("%s: unknown line %q", d.name, id)
	}
	if d.name == "lockout" {
		log.Printf("gpio/lockout: refuse Set %s=%v", id, level)
		return nil
	}
	d.state[id] = level
	log.Printf("gpio/fake: Set %s=%v (activeLow=%v)", id, level, d.activeLow)
	return nil
}

func (d *fakeDriver) AllOff() error {
	d.mu.Lock()
	defer d.mu.Unlock()
	for id := range d.lines {
		if id == "psu" {
			continue
		}
		d.state[id] = Off
		log.Printf("gpio/%s: AllOff station %s", d.name, id)
	}
	if _, ok := d.lines["psu"]; ok {
		d.state["psu"] = Off
		log.Printf("gpio/%s: AllOff psu", d.name)
	}
	return nil
}

func (d *fakeDriver) Close() error {
	_ = d.AllOff()
	log.Printf("gpio/%s: close", d.name)
	return nil
}

// StateForTest returns logical levels (tests only).
func StateForTest(d Driver) map[string]Level {
	f, ok := d.(*fakeDriver)
	if !ok {
		return nil
	}
	f.mu.Lock()
	defer f.mu.Unlock()
	out := make(map[string]Level, len(f.state))
	for k, v := range f.state {
		out[k] = v
	}
	return out
}
