package rain

import (
	"os"
	"strings"
	"testing"
	"time"
)

func TestParseFCDMCFixture(t *testing.T) {
	f, err := os.Open("testdata/fcdmc_sample.html")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	loc, err := time.LoadLocation(phoenixTZ)
	if err != nil {
		t.Fatal(err)
	}
	samples, err := ParseFCDMC(f, loc)
	if err != nil {
		t.Fatal(err)
	}
	if len(samples) != 7 {
		t.Fatalf("count %d", len(samples))
	}
	want := []struct {
		y, m, d, hh, mm, ss int
		inches              float64
	}{
		{2026, 9, 23, 6, 0, 0, 0},
		{2026, 9, 24, 12, 0, 0, 0},
		{2026, 9, 24, 16, 20, 11, 0.04},
		{2026, 9, 24, 16, 36, 24, 0.08},
		{2026, 9, 24, 16, 52, 6, 0.12},
		{2026, 9, 26, 12, 0, 0, 0},
		{2026, 9, 26, 18, 0, 0, 0},
	}
	for i, w := range want {
		got := samples[i]
		exp := time.Date(w.y, time.Month(w.m), w.d, w.hh, w.mm, w.ss, 0, loc)
		if !got.Time.Equal(exp) {
			t.Fatalf("sample %d time %s want %s", i, got.Time, exp)
		}
		if got.Inches != w.inches {
			t.Fatalf("sample %d inches %v want %v", i, got.Inches, w.inches)
		}
		_, off := got.Time.Zone()
		if off != -7*3600 {
			t.Fatalf("sample %d offset %d", i, off)
		}
		if i > 0 && got.Time.Before(samples[i-1].Time) {
			t.Fatalf("not oldest-first at %d", i)
		}
	}
}

func TestParseFCDMCEmpty(t *testing.T) {
	_, err := ParseFCDMC(strings.NewReader("<HTML><PRE>no rows</PRE>"), nil)
	if err == nil || !strings.Contains(err.Error(), "no samples") {
		t.Fatalf("got %v", err)
	}
}

func TestParseFCDMCSkipsJunk(t *testing.T) {
	body := "Date Time inches\nthis is junk\n09/01/2026 00:00:00 1.00\n"
	_, err := ParseFCDMC(strings.NewReader(body), nil)
	if err == nil || !strings.Contains(err.Error(), "no samples") {
		t.Fatalf("incomplete row should not count, got %v", err)
	}
}
