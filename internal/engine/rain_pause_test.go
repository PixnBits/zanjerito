package engine

import (
	"context"
	"errors"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/gpio"
)

func rainPauseCfg() Config {
	al := true
	return Config{
		Chip: "gpiochip0", ActiveLow: &al, Timezone: "America/Phoenix", MaxOnSec: 900,
		Sequencing: SequencingConfig{Mode: SequenceOverlap, OverlapMS: 50},
		Power:      StationConfig{ID: "psu", BCM: 21},
		Stations: []StationConfig{
			{ID: "front-north", Title: "Front North", BCM: 6},
			{ID: "drip", Title: "Drip Line", BCM: 13, RainPauseExempt: true},
		},
	}
}

func TestAutoRainPauseExemptOnly(t *testing.T) {
	e, err := New(rainPauseCfg(), gpio.NewFake())
	if err != nil {
		t.Fatal(err)
	}
	defer e.Close()
	until := time.Now().Add(time.Hour)
	last := time.Now().Add(-time.Hour)
	e.SetPauseMeta(&until, "rain", PauseMeta{
		Source: PauseSourceAuto, RainInches: 0.4, LastRainAt: &last,
	})
	rec := &capture{}
	e.SetRecorder(rec)

	err = e.RunItinerary(context.Background(), []Step{{StationID: "front-north", Duration: 30 * time.Millisecond}})
	if !errors.Is(err, ErrPaused) {
		t.Fatalf("non-exempt want ErrPaused, got %v", err)
	}
	err = e.RunItinerary(context.Background(), []Step{
		{StationID: "drip", Duration: 20 * time.Millisecond},
		{StationID: "front-north", Duration: 20 * time.Millisecond},
	})
	if !errors.Is(err, ErrPaused) {
		t.Fatalf("mixed want ErrPaused, got %v", err)
	}
	if len(rec.snapshot()) != 0 {
		t.Fatalf("rejections recorded %+v", rec.snapshot())
	}

	err = e.RunItinerary(context.Background(), []Step{{StationID: "drip", Duration: 40 * time.Millisecond}})
	if err != nil {
		t.Fatal(err)
	}
	got := rec.snapshot()
	if len(got) != 1 || got[0].Outcome != OutcomeCompleted || got[0].Kind != KindManual {
		t.Fatalf("exempt record %+v", got)
	}
	if len(got[0].Stations) != 1 || got[0].Stations[0].StationID != "drip" || got[0].Stations[0].ActualSec < 0 {
		t.Fatalf("stations %+v", got[0].Stations)
	}

	e.SetPause(&until, "rain") // manual, even with reason rain
	err = e.RunItinerary(context.Background(), []Step{{StationID: "drip", Duration: 20 * time.Millisecond}})
	if !errors.Is(err, ErrPaused) {
		t.Fatalf("manual pause must block exempt, got %v", err)
	}
}
