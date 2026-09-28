package soil

import (
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/history"
)

func phoenix(t *testing.T) *time.Location {
	t.Helper()
	loc, err := time.LoadLocation("America/Phoenix")
	if err != nil {
		t.Fatal(err)
	}
	return loc
}

func TestBalanceCapsAndFloors(t *testing.T) {
	z := Zone{CropFactor: 1, Capacity: 1.0}
	dates := []string{"2026-09-26", "2026-09-27"}
	et := map[string]float64{"2026-09-26": 0, "2026-09-27": 0}
	rain := map[string]float64{"2026-09-26": 2.0}
	bal, series := runBalance(z, dates, et, rain, true, nil, false)
	if bal != 1.0 {
		t.Fatalf("cap got %v", bal)
	}
	if series[0].Balance != 1.0 {
		t.Fatalf("day0 %v", series[0].Balance)
	}

	et = map[string]float64{"2026-09-26": 5.0, "2026-09-27": 5.0}
	bal, series = runBalance(z, dates, et, map[string]float64{}, true, nil, false)
	if bal != 0 {
		t.Fatalf("floor got %v", bal)
	}
	if series[0].Balance != 0 || series[1].Balance != 0 {
		t.Fatalf("series %+v", series)
	}
}

func TestMissingETCarriesForward(t *testing.T) {
	z := Zone{CropFactor: 0.5, Capacity: 10}
	dates := []string{"2026-09-25", "2026-09-26", "2026-09-27"}
	et := map[string]float64{"2026-09-25": 0.40}
	bal, series := runBalance(z, dates, et, map[string]float64{}, true, nil, false)
	if !series[0].ETKnown || series[0].ETCarried || series[0].ETRefInches != 0.40 {
		t.Fatalf("day0 %+v", series[0])
	}
	if !series[1].ETKnown || !series[1].ETCarried || series[1].ETRefInches != 0.40 {
		t.Fatalf("carried %+v", series[1])
	}
	if !series[2].ETCarried || series[2].ETInches != 0.20 {
		t.Fatalf("today %+v", series[2])
	}
	// 10 - 0.20 - 0.20 - 0.20
	if bal < 9.40-1e-9 || bal > 9.40+1e-9 {
		t.Fatalf("bal %v", bal)
	}
}

func TestNoPriorETUnknown(t *testing.T) {
	z := Zone{CropFactor: 0.6, Capacity: 1.0}
	dates := []string{"2026-09-27"}
	_, series := runBalance(z, dates, map[string]float64{}, map[string]float64{"2026-09-27": 0.1}, true, nil, false)
	if series[0].ETKnown || series[0].ETCarried || series[0].ETRefInches != 0 || series[0].ETInches != 0 {
		t.Fatalf("%+v", series[0])
	}
	if series[0].Balance < 1.0-1e-9 && series[0].RainInches != 0.1 {
		t.Fatalf("rain should still apply %+v", series[0])
	}
	if series[0].Balance != 1.0 {
		t.Fatalf("balance = %v, want 1.0", series[0].Balance)
	}
}

func TestNullRateOmitsWatering(t *testing.T) {
	loc := phoenix(t)
	now := time.Date(2026, 9, 27, 12, 0, 0, 0, loc)
	cfg := Config{
		Enabled:    true,
		WindowDays: 1,
		CropFactor: 0.6,
		Capacity:   1.0,
		MaxDailyET: 0.6,
	}.normalized()
	stations := []engine.StationConfig{{ID: "front-north", Title: "Front North"}}
	et := []DayET{{Date: "2026-09-26", ETInches: 0.2}, {Date: "2026-09-27", ETInches: 0.2}}
	hist := []history.Entry{{
		Outcome:   engine.OutcomeCompleted,
		StartedAt: now,
		Stations:  []history.Station{{StationID: "front-north", ActualSec: 3600}},
	}}
	zones := Estimate(cfg, stations, et, map[string]float64{}, true, hist, now, loc)
	if len(zones) != 1 {
		t.Fatalf("zones %d", len(zones))
	}
	if zones[0].RateMeasured || zones[0].Inputs.WateringInches != nil {
		t.Fatalf("rate must be unknown %+v", zones[0])
	}
}

func TestWateringFromCompletedAndStoppedOnly(t *testing.T) {
	loc := phoenix(t)
	now := time.Date(2026, 9, 27, 16, 0, 0, 0, loc)
	rate := 1.0
	cfg := Config{
		Enabled:    true,
		WindowDays: 1,
		CropFactor: 1,
		Capacity:   5,
		MaxDailyET: 0.6,
		Zones: map[string]Zone{
			"front-north": {InchesPerHour: &rate, CropFactor: 1, Capacity: 5},
		},
	}
	stations := []engine.StationConfig{{ID: "front-north", Title: "Front North"}}
	et := []DayET{{Date: "2026-09-26", ETInches: 0}, {Date: "2026-09-27", ETInches: 0}}
	hist := []history.Entry{
		{Outcome: engine.OutcomeCompleted, StartedAt: now.Add(-time.Hour), Stations: []history.Station{{StationID: "front-north", ActualSec: 1800}}},
		{Outcome: engine.OutcomeStopped, StartedAt: now.Add(-30 * time.Minute), Stations: []history.Station{{StationID: "front-north", ActualSec: 1800}}},
		{Outcome: engine.OutcomeSkipped, StartedAt: now.Add(-20 * time.Minute), Stations: []history.Station{{StationID: "front-north", ActualSec: 3600}}},
		{Outcome: engine.OutcomeRefused, StartedAt: now.Add(-10 * time.Minute), Stations: []history.Station{{StationID: "front-north", ActualSec: 3600}}},
		{Outcome: engine.OutcomeError, StartedAt: now, Stations: []history.Station{{StationID: "front-north", ActualSec: 3600}}},
	}
	zones := Estimate(cfg, stations, et, map[string]float64{}, true, hist, now, loc)
	if !zones[0].RateMeasured || zones[0].Inputs.WateringInches == nil {
		t.Fatalf("%+v", zones[0])
	}
	// 1800+1800 sec = 1.0 hour * 1 in/hr = 1.00; skipped/refused/error ignored
	if *zones[0].Inputs.WateringInches != 1.00 {
		t.Fatalf("watering %v", *zones[0].Inputs.WateringInches)
	}
}

func TestRainSummedByLocalDate(t *testing.T) {
	loc := phoenix(t)
	now := time.Date(2026, 9, 27, 12, 0, 0, 0, loc)
	cfg := Config{Enabled: true, WindowDays: 1, CropFactor: 1, Capacity: 5, MaxDailyET: 0.6}
	stations := []engine.StationConfig{{ID: "drip", Title: "Drip"}}
	et := []DayET{{Date: "2026-09-26", ETInches: 0}, {Date: "2026-09-27", ETInches: 0}}
	rain := map[string]float64{"2026-09-27": 0.10 + 0.05}
	zones := Estimate(cfg, stations, et, rain, true, nil, now, loc)
	if zones[0].Inputs.RainInches != 0.15 || !zones[0].Inputs.RainKnown {
		t.Fatalf("rain %+v", zones[0].Inputs)
	}
	if !zones[0].Inputs.ETKnown {
		t.Fatal("et should be known")
	}
}

func TestEstimateEngineOrderAndTodayInputs(t *testing.T) {
	loc := phoenix(t)
	now := time.Date(2026, 9, 27, 9, 0, 0, 0, loc)
	rate := 0.5
	cfg := Config{
		Enabled:    true,
		WindowDays: 1,
		CropFactor: 0.6,
		Capacity:   1.0,
		MaxDailyET: 0.6,
		Zones: map[string]Zone{
			"front-north": {InchesPerHour: &rate, CropFactor: 0.6, Capacity: 1.0},
		},
	}
	stations := []engine.StationConfig{
		{ID: "front-west", Title: "Front West"},
		{ID: "front-north", Title: "Front North"},
	}
	et := []DayET{{Date: "2026-09-26", ETInches: 0.30}, {Date: "2026-09-27", ETInches: 0.20}}
	zones := Estimate(cfg, stations, et, map[string]float64{}, false, nil, now, loc)
	if len(zones) != 2 || zones[0].StationID != "front-west" || zones[1].StationID != "front-north" {
		t.Fatalf("order %+v", zones)
	}
	if zones[0].RateMeasured || zones[0].Inputs.WateringInches != nil {
		t.Fatalf("west should be rain & ET only %+v", zones[0])
	}
	if !zones[1].RateMeasured || zones[1].Inputs.WateringInches == nil {
		t.Fatalf("north %+v", zones[1])
	}
	if *zones[1].Inputs.WateringInches != 0 {
		t.Fatalf("watering %v", *zones[1].Inputs.WateringInches)
	}
	if zones[1].Inputs.Date != "2026-09-27" || zones[1].Inputs.ETRefInches != 0.20 {
		t.Fatalf("today %+v", zones[1].Inputs)
	}
	if zones[1].Inputs.ETInches != 0.12 { // 0.20 * 0.6
		t.Fatalf("et inches %v", zones[1].Inputs.ETInches)
	}
	if zones[0].Inputs.RainKnown || zones[1].Inputs.RainKnown {
		t.Fatal("rain unknown")
	}
}

func TestWindowRainAndETTotals(t *testing.T) {
	loc := phoenix(t)
	now := time.Date(2026, 9, 27, 12, 0, 0, 0, loc)
	cfg := Config{Enabled: true, WindowDays: 1, CropFactor: 0.6, Capacity: 1, MaxDailyET: 0.6}.normalized()
	stations := []engine.StationConfig{{ID: "front-north", Title: "Front North"}}
	et := []DayET{{Date: "2026-09-26", ETInches: 0.20}, {Date: "2026-09-27", ETInches: 0.20}}
	rain := map[string]float64{"2026-09-26": 0.15, "2026-09-27": 0.25}
	zones := Estimate(cfg, stations, et, rain, true, nil, now, loc)
	if len(zones) != 1 {
		t.Fatalf("zones %d", len(zones))
	}
	// Modelled days are today and the previous window_days. Crop ET is 0.20*0.6 per day.
	if zones[0].RainTotalInches != 0.40 || zones[0].ETTotalInches != 0.24 {
		t.Fatalf("totals rain %v et %v", zones[0].RainTotalInches, zones[0].ETTotalInches)
	}
	if zones[0].Inputs.RainInches != 0.25 {
		t.Fatalf("today rain %v", zones[0].Inputs.RainInches)
	}
	if zones[0].Percent == nil || zones[0].BalanceInches == nil {
		t.Fatal("known ET should publish percent and balance")
	}
}
