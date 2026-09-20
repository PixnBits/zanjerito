package gpio

import (
	"fmt"
	"log"
	"strings"
	"sync"

	"github.com/warthog618/go-gpiocdev"
)

// lineIO is the subset of gpiocdev.Line we need (mockable in tests).
type lineIO interface {
	SetValue(value int) error
	Close() error
}

// requestLineFunc claims one chip offset. Production uses gpiocdev.RequestLine.
type requestLineFunc func(chip string, offset int, activeLow bool) (lineIO, error)

func requestRealLine(chip string, offset int, activeLow bool) (lineIO, error) {
	chip = normalizeChip(chip)
	opts := []gpiocdev.LineReqOption{
		// 0 = inactive. With AsActiveLow, inactive is HIGH → relays de-energized.
		gpiocdev.AsOutput(0),
	}
	if activeLow {
		opts = append(opts, gpiocdev.AsActiveLow)
	}
	l, err := gpiocdev.RequestLine(chip, offset, opts...)
	if err != nil {
		return nil, err
	}
	return l, nil
}

func normalizeChip(chip string) string {
	chip = strings.TrimSpace(chip)
	chip = strings.TrimPrefix(chip, "/dev/")
	if chip == "" {
		return "gpiochip0"
	}
	return chip
}

type gpiocdevDriver struct {
	mu        sync.Mutex
	activeLow bool
	lines     map[string]Line
	handles   map[string]lineIO
	order     []string // stations then psu for AllOff
	request   requestLineFunc
}

// NewGpiocdev returns the Linux character-device driver (go-gpiocdev).
// Pure Go / CGO_ENABLED=0. Lines request AsOutput(inactive) + AsActiveLow when
// activeLow so release/Close leaves valves de-energized (inactive-on-release).
func NewGpiocdev() (Driver, error) {
	return &gpiocdevDriver{
		lines:   make(map[string]Line),
		handles: make(map[string]lineIO),
		request: requestRealLine,
	}, nil
}

func (d *gpiocdevDriver) Setup(chip string, lines []Line, activeLow bool) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	if len(d.handles) > 0 {
		return fmt.Errorf("gpiocdev: already set up")
	}
	if !activeLow {
		return fmt.Errorf("gpiocdev: active_low=false refused (this hardware is active-low)")
	}
	d.activeLow = activeLow
	chip = normalizeChip(chip)
	req := d.request
	if req == nil {
		req = requestRealLine
	}
	d.order = make([]string, 0, len(lines))
	var psu string
	for _, ln := range lines {
		if ln.ID == "" {
			return fmt.Errorf("gpiocdev: line missing id")
		}
		if ln.BCM < 0 {
			return fmt.Errorf("gpiocdev: bad bcm for %s", ln.ID)
		}
		h, err := req(chip, ln.BCM, activeLow)
		if err != nil {
			d.closeLocked()
			return fmt.Errorf("gpiocdev: request %s bcm=%d on %s: %w", ln.ID, ln.BCM, chip, err)
		}
		d.lines[ln.ID] = ln
		d.handles[ln.ID] = h
		if ln.ID == "psu" {
			psu = ln.ID
		} else {
			d.order = append(d.order, ln.ID)
		}
	}
	if psu != "" {
		d.order = append(d.order, psu)
	}
	log.Printf("gpio/gpiocdev: setup chip=%s activeLow=%v lines=%d (inactive-on-release)", chip, activeLow, len(lines))
	return nil
}

func (d *gpiocdevDriver) Set(id string, level Level) error {
	d.mu.Lock()
	defer d.mu.Unlock()
	h, ok := d.handles[id]
	if !ok {
		return fmt.Errorf("gpiocdev: unknown line %q", id)
	}
	v := 0
	if level == On {
		v = 1 // active
	}
	if err := h.SetValue(v); err != nil {
		return fmt.Errorf("gpiocdev: Set %s=%v: %w", id, level, err)
	}
	log.Printf("gpio/gpiocdev: Set %s=%v", id, level)
	return nil
}

func (d *gpiocdevDriver) AllOff() error {
	d.mu.Lock()
	defer d.mu.Unlock()
	var first error
	for _, id := range d.order {
		h := d.handles[id]
		if h == nil {
			continue
		}
		if err := h.SetValue(0); err != nil && first == nil {
			first = err
		}
		log.Printf("gpio/gpiocdev: AllOff %s", id)
	}
	return first
}

func (d *gpiocdevDriver) Close() error {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.closeLocked()
}

func (d *gpiocdevDriver) closeLocked() error {
	var first error
	// stations then psu (same order as AllOff)
	for _, id := range d.order {
		h := d.handles[id]
		if h == nil {
			continue
		}
		_ = h.SetValue(0) // inactive before release
		if err := h.Close(); err != nil && first == nil {
			first = err
		}
		delete(d.handles, id)
	}
	for id, h := range d.handles {
		_ = h.SetValue(0)
		if err := h.Close(); err != nil && first == nil {
			first = err
		}
		delete(d.handles, id)
	}
	d.lines = make(map[string]Line)
	d.order = nil
	log.Printf("gpio/gpiocdev: close")
	return first
}
