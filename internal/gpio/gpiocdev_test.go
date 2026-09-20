package gpio

import (
	"errors"
	"sync"
	"testing"
)

type mockLine struct {
	mu     sync.Mutex
	val    int
	closed bool
	setErr error
}

func (m *mockLine) SetValue(v int) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	if m.setErr != nil {
		return m.setErr
	}
	m.val = v
	return nil
}

func (m *mockLine) Close() error {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.closed = true
	return nil
}

func (m *mockLine) value() int {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.val
}

func TestGpiocdevSetupSetAllOffClose(t *testing.T) {
	d := &gpiocdevDriver{
		lines:   make(map[string]Line),
		handles: make(map[string]lineIO),
	}
	mocks := map[int]*mockLine{}
	d.request = func(chip string, offset int, activeLow bool) (lineIO, error) {
		if chip != "gpiochip0" {
			t.Fatalf("chip %q", chip)
		}
		if !activeLow {
			t.Fatal("want activeLow")
		}
		m := &mockLine{val: -1}
		mocks[offset] = m
		return m, nil
	}
	lines := []Line{
		{ID: "psu", BCM: 21},
		{ID: "front-west", BCM: 5},
		{ID: "front-north", BCM: 6},
	}
	if err := d.Setup("gpiochip0", lines, true); err != nil {
		t.Fatal(err)
	}
	for _, m := range mocks {
		if m.value() != -1 && m.value() != 0 {
			// request starts inactive via AsOutput(0); mock does not auto-set
		}
	}
	if err := d.Set("front-west", On); err != nil {
		t.Fatal(err)
	}
	if mocks[5].value() != 1 {
		t.Fatalf("west want active 1, got %d", mocks[5].value())
	}
	if err := d.AllOff(); err != nil {
		t.Fatal(err)
	}
	if mocks[5].value() != 0 || mocks[6].value() != 0 || mocks[21].value() != 0 {
		t.Fatalf("AllOff want all inactive: west=%d north=%d psu=%d", mocks[5].value(), mocks[6].value(), mocks[21].value())
	}
	// AllOff order: stations before psu — already set to 0; check Close releases
	if err := d.Close(); err != nil {
		t.Fatal(err)
	}
	for off, m := range mocks {
		if !m.closed {
			t.Fatalf("offset %d not closed", off)
		}
		if m.value() != 0 {
			t.Fatalf("offset %d left active %d", off, m.value())
		}
	}
}

func TestGpiocdevNormalizesDevPath(t *testing.T) {
	d := &gpiocdevDriver{lines: make(map[string]Line), handles: make(map[string]lineIO)}
	var got string
	d.request = func(chip string, offset int, activeLow bool) (lineIO, error) {
		got = chip
		return &mockLine{}, nil
	}
	if err := d.Setup("/dev/gpiochip0", []Line{{ID: "psu", BCM: 21}}, true); err != nil {
		t.Fatal(err)
	}
	if got != "gpiochip0" {
		t.Fatalf("got %q", got)
	}
	_ = d.Close()
}

func TestGpiocdevRefusesActiveHigh(t *testing.T) {
	d := &gpiocdevDriver{lines: make(map[string]Line), handles: make(map[string]lineIO)}
	d.request = func(string, int, bool) (lineIO, error) { return &mockLine{}, nil }
	err := d.Setup("gpiochip0", []Line{{ID: "psu", BCM: 21}}, false)
	if err == nil {
		t.Fatal("want error")
	}
}

func TestGpiocdevRequestErrorFailsClosed(t *testing.T) {
	d := &gpiocdevDriver{lines: make(map[string]Line), handles: make(map[string]lineIO)}
	n := 0
	d.request = func(string, int, bool) (lineIO, error) {
		n++
		if n == 2 {
			return nil, errors.New("busy")
		}
		return &mockLine{}, nil
	}
	err := d.Setup("gpiochip0", []Line{
		{ID: "front-west", BCM: 5},
		{ID: "psu", BCM: 21},
	}, true)
	if err == nil {
		t.Fatal("want error")
	}
	if len(d.handles) != 0 {
		t.Fatalf("leaked handles: %d", len(d.handles))
	}
}

func TestNewGpiocdevFactory(t *testing.T) {
	d, err := New("gpiocdev")
	if err != nil {
		t.Fatal(err)
	}
	if _, ok := d.(*gpiocdevDriver); !ok {
		t.Fatalf("type %T", d)
	}
}

func TestNormalizeChip(t *testing.T) {
	if normalizeChip("/dev/gpiochip0") != "gpiochip0" {
		t.Fatal()
	}
	if normalizeChip("") != "gpiochip0" {
		t.Fatal()
	}
}
