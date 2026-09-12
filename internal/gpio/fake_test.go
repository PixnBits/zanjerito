package gpio

import "testing"

func TestFakeSetupSetAllOff(t *testing.T) {
	d := NewFake()
	lines := []Line{
		{ID: "front-west", BCM: 5},
		{ID: "psu", BCM: 21},
	}
	if err := d.Setup("gpiochip0", lines, true); err != nil {
		t.Fatal(err)
	}
	if err := d.Set("front-west", On); err != nil {
		t.Fatal(err)
	}
	st := StateForTest(d)
	if st["front-west"] != On {
		t.Fatalf("expected On, got %v", st["front-west"])
	}
	if err := d.AllOff(); err != nil {
		t.Fatal(err)
	}
	st = StateForTest(d)
	if st["front-west"] != Off || st["psu"] != Off {
		t.Fatalf("expected all off, got %#v", st)
	}
	if err := d.Close(); err != nil {
		t.Fatal(err)
	}
}

func TestLockoutRefusesEnergize(t *testing.T) {
	d := NewLockout()
	_ = d.Setup("gpiochip0", []Line{{ID: "front-west", BCM: 5}}, true)
	if err := d.Set("front-west", On); err != nil {
		t.Fatal(err)
	}
	st := StateForTest(d)
	if st["front-west"] != Off {
		t.Fatalf("lockout must leave line Off, got %v", st["front-west"])
	}
}

func TestUnknownDriver(t *testing.T) {
	if _, err := New("nope"); err == nil {
		t.Fatal("expected error")
	}
}
