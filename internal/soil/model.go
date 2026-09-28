package soil

import (
	"math"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/history"
)

const mmPerInch = 25.4

// DayET is one local calendar day's reference evapotranspiration, in inches.
type DayET struct {
	Date        string  `json:"date"`
	ETInches    float64 `json:"et_inches"`
	NeedsReview bool    `json:"-"`
}

// Inputs is today's water-balance terms for one zone.
type Inputs struct {
	Date           string   `json:"date"`
	RainInches     float64  `json:"rain_inches"`
	RainKnown      bool     `json:"rain_known"`
	ETRefInches    float64  `json:"et_ref_inches"`
	CropFactor     float64  `json:"crop_factor"`
	ETInches       float64  `json:"et_inches"`
	ETCarried      bool     `json:"et_carried"`
	ETKnown        bool     `json:"et_known"`
	WateringInches *float64 `json:"watering_inches"`
}

// ZoneView is one engine station's estimate.
// Percent and BalanceInches are null when the caller does not yet know ET.
// RainTotalInches and ETTotalInches sum the modelled window (crop ET, not reference).
type ZoneView struct {
	StationID       string   `json:"station_id"`
	Title           string   `json:"title"`
	BalanceInches   *float64 `json:"balance_inches"`
	CapacityInches  float64  `json:"capacity_inches"`
	Percent         *int     `json:"percent"`
	RateMeasured    bool     `json:"rate_measured"`
	RainTotalInches float64  `json:"rain_total_inches"`
	ETTotalInches   float64  `json:"et_total_inches"`
	Inputs          Inputs   `json:"inputs"`
}

// dayResult is one modelled local day. Tests inspect it.
type dayResult struct {
	Date           string
	Balance        float64
	RainInches     float64
	ETRefInches    float64
	ETInches       float64
	ETCarried      bool
	ETKnown        bool
	WateringInches *float64
}

func roundInches(v float64) float64 {
	return math.Round(v*100) / 100
}

func windowDates(now time.Time, loc *time.Location, windowDays int) []string {
	if loc == nil {
		loc = time.UTC
	}
	if windowDays <= 0 {
		windowDays = DefaultWindowDays
	}
	t := now.In(loc)
	today := time.Date(t.Year(), t.Month(), t.Day(), 0, 0, 0, 0, loc)
	start := today.AddDate(0, 0, -windowDays)
	out := make([]string, 0, windowDays+1)
	for d := start; !d.After(today); d = d.AddDate(0, 0, 1) {
		out = append(out, d.Format("2006-01-02"))
	}
	return out
}

func windowStart(now time.Time, loc *time.Location, windowDays int) time.Time {
	if loc == nil {
		loc = time.UTC
	}
	if windowDays <= 0 {
		windowDays = DefaultWindowDays
	}
	t := now.In(loc)
	today := time.Date(t.Year(), t.Month(), t.Day(), 0, 0, 0, 0, loc)
	return today.AddDate(0, 0, -windowDays)
}

func etMap(days []DayET) map[string]float64 {
	out := make(map[string]float64, len(days))
	for _, d := range days {
		if d.Date == "" {
			continue
		}
		out[d.Date] = d.ETInches
	}
	return out
}

func wateringByDate(stationID string, rate float64, entries []history.Entry, loc *time.Location) map[string]float64 {
	if loc == nil {
		loc = time.UTC
	}
	out := map[string]float64{}
	for _, e := range entries {
		if e.Outcome != engine.OutcomeCompleted && e.Outcome != engine.OutcomeStopped {
			continue
		}
		day := e.StartedAt.In(loc).Format("2006-01-02")
		for _, s := range e.Stations {
			if s.StationID != stationID || s.ActualSec <= 0 {
				continue
			}
			out[day] += float64(s.ActualSec) / 3600.0 * rate
		}
	}
	return out
}

// runBalance walks dates oldest to newest. balance before the first date is capacity.
func runBalance(z Zone, dates []string, etByDate map[string]float64, rainByDate map[string]float64, rainKnown bool, waterByDate map[string]float64, rateKnown bool) (balance float64, series []dayResult) {
	cap := z.Capacity
	if cap <= 0 {
		cap = DefaultCapacity
	}
	crop := z.CropFactor
	if crop <= 0 {
		crop = DefaultCropFactor
	}
	bal := cap
	var lastET float64
	haveET := false
	series = make([]dayResult, 0, len(dates))
	for _, d := range dates {
		rain := 0.0
		if rainKnown {
			rain = rainByDate[d]
			if rain < 0 {
				rain = 0
			}
		}
		etRef := 0.0
		etCarried := false
		etKnown := false
		if v, ok := etByDate[d]; ok {
			etRef = v
			lastET = v
			haveET = true
			etKnown = true
		} else if haveET {
			etRef = lastET
			etCarried = true
			etKnown = true
		}
		et := etRef * crop
		addW := 0.0
		var watering *float64
		if rateKnown {
			w := waterByDate[d]
			if w < 0 {
				w = 0
			}
			addW = w
			watering = &w
		}
		bal = bal + rain + addW - et
		if bal < 0 {
			bal = 0
		}
		if bal > cap {
			bal = cap
		}
		series = append(series, dayResult{
			Date:           d,
			Balance:        bal,
			RainInches:     rain,
			ETRefInches:    etRef,
			ETInches:       et,
			ETCarried:      etCarried,
			ETKnown:        etKnown,
			WateringInches: watering,
		})
	}
	return bal, series
}

func seriesTotals(series []dayResult) (rain, et float64) {
	var r, e float64
	for _, d := range series {
		r += d.RainInches
		e += d.ETInches
	}
	return roundInches(r), roundInches(e)
}

func percentFull(balance, capacity float64) int {
	if capacity <= 0 {
		return 0
	}
	pct := int(math.Round(100 * balance / capacity))
	if pct < 0 {
		return 0
	}
	if pct > 100 {
		return 100
	}
	return pct
}

func roundedPtr(v *float64) *float64 {
	if v == nil {
		return nil
	}
	x := roundInches(*v)
	return &x
}

func zoneView(st engine.StationConfig, z Zone, series []dayResult, balance float64) ZoneView {
	title := st.Title
	if title == "" {
		title = st.ID
	}
	in := Inputs{CropFactor: roundInches(z.CropFactor)}
	if len(series) > 0 {
		last := series[len(series)-1]
		in = Inputs{
			Date:           last.Date,
			RainInches:     roundInches(last.RainInches),
			ETRefInches:    roundInches(last.ETRefInches),
			CropFactor:     roundInches(z.CropFactor),
			ETInches:       roundInches(last.ETInches),
			ETCarried:      last.ETCarried,
			ETKnown:        last.ETKnown,
			WateringInches: roundedPtr(last.WateringInches),
		}
	}
	bal := roundInches(balance)
	pct := percentFull(balance, z.Capacity)
	rainTotal, etTotal := seriesTotals(series)
	return ZoneView{
		StationID:       st.ID,
		Title:           title,
		BalanceInches:   &bal,
		CapacityInches:  roundInches(z.Capacity),
		Percent:         &pct,
		RateMeasured:    z.InchesPerHour != nil,
		RainTotalInches: rainTotal,
		ETTotalInches:   etTotal,
		Inputs:          in,
	}
}

// Estimate recomputes each engine station's bucket from cached ET, rain, and history.
// It does no I/O. Stations not listed in cfg.Zones use defaults with an unknown rate.
func Estimate(cfg Config, stations []engine.StationConfig, et []DayET, rainByDate map[string]float64, rainKnown bool, hist []history.Entry, now time.Time, loc *time.Location) []ZoneView {
	cfg = cfg.normalized()
	if loc == nil {
		var err error
		loc, err = time.LoadLocation("America/Phoenix")
		if err != nil {
			loc = time.FixedZone("MST", -7*3600)
		}
	}
	dates := windowDates(now, loc, cfg.WindowDays)
	etByDate := etMap(et)
	if rainByDate == nil {
		rainByDate = map[string]float64{}
	}
	out := make([]ZoneView, 0, len(stations))
	for _, st := range stations {
		z := cfg.ZoneFor(st.ID)
		rateKnown := z.InchesPerHour != nil
		var water map[string]float64
		if rateKnown {
			water = wateringByDate(st.ID, *z.InchesPerHour, hist, loc)
		}
		bal, series := runBalance(z, dates, etByDate, rainByDate, rainKnown, water, rateKnown)
		view := zoneView(st, z, series, bal)
		view.Inputs.RainKnown = rainKnown
		out = append(out, view)
	}
	return out
}
