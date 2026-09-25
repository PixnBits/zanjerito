package schedule

import (
	"strings"
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/store"
)

func mustPhoenix(t *testing.T) *time.Location {
	t.Helper()
	loc, err := time.LoadLocation(Phoenix)
	if err != nil {
		t.Fatal(err)
	}
	return loc
}

func withStep(start string, weekdays []string) store.Schedule {
	return store.Schedule{
		ID:       "s-" + start,
		Enabled:  true,
		Start:    start,
		Weekdays: weekdays,
		Steps:    []store.Step{{StationID: "front-west", Minutes: 1}},
	}
}

func TestMorningCutoff(t *testing.T) {
	loc := mustPhoenix(t)
	sat := time.Date(2026, 9, 26, 15, 0, 0, 0, loc) // Saturday
	mon := time.Date(2026, 9, 28, 8, 0, 0, 0, loc)  // Monday
	step := []store.Step{{StationID: "front-west", Minutes: 1}}

	tests := []struct {
		name string
		sch  []store.Schedule
		day  time.Time
		want time.Time
	}{
		{
			name: "no schedules → 06:00 of that day",
			sch:  nil,
			day:  sat,
			want: time.Date(2026, 9, 26, 6, 0, 0, 0, loc),
		},
		{
			name: "single 05:30 daily",
			sch:  []store.Schedule{withStep("05:30", nil)},
			day:  sat,
			want: time.Date(2026, 9, 26, 5, 30, 0, 0, loc),
		},
		{
			name: "weekday-limited not applying → 06:00",
			sch:  []store.Schedule{withStep("05:30", []string{"mon"})},
			day:  sat,
			want: time.Date(2026, 9, 26, 6, 0, 0, 0, loc),
		},
		{
			name: "weekday-limited falls back to next earliest that applies",
			sch: []store.Schedule{
				withStep("05:30", []string{"mon"}),
				withStep("07:00", []string{"sat"}),
			},
			day:  sat,
			want: time.Date(2026, 9, 26, 7, 0, 0, 0, loc),
		},
		{
			name: "out-of-season starts_on after target ignored",
			sch: []store.Schedule{{
				ID: "later", Enabled: true, Start: "05:30", Steps: step,
				StartsOn: "2026-10-01",
			}},
			day:  sat,
			want: time.Date(2026, 9, 26, 6, 0, 0, 0, loc),
		},
		{
			name: "out-of-season ends_on before target ignored",
			sch: []store.Schedule{{
				ID: "earlier", Enabled: true, Start: "05:30", Steps: step,
				EndsOn: "2026-09-01",
			}},
			day:  sat,
			want: time.Date(2026, 9, 26, 6, 0, 0, 0, loc),
		},
		{
			name: "disabled ignored",
			sch: []store.Schedule{{
				ID: "off", Enabled: false, Start: "05:30", Steps: step,
			}},
			day:  sat,
			want: time.Date(2026, 9, 26, 6, 0, 0, 0, loc),
		},
		{
			name: "no-steps ignored",
			sch: []store.Schedule{{
				ID: "empty", Enabled: true, Start: "05:30",
			}},
			day:  sat,
			want: time.Date(2026, 9, 26, 6, 0, 0, 0, loc),
		},
		{
			name: "earliest-of-several",
			sch: []store.Schedule{
				withStep("07:00", nil),
				withStep("05:30", nil),
				withStep("06:15", nil),
			},
			day:  mon,
			want: time.Date(2026, 9, 28, 5, 30, 0, 0, loc),
		},
		{
			name: "only-evening 18:00 → 06:00 noon guard",
			sch:  []store.Schedule{withStep("18:00", nil)},
			day:  sat,
			want: time.Date(2026, 9, 26, 6, 0, 0, 0, loc),
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got := MorningCutoff(tc.sch, tc.day, loc)
			if !got.Equal(tc.want) {
				t.Fatalf("got %s want %s", got, tc.want)
			}
		})
	}
}

func TestPauseUntilDays(t *testing.T) {
	loc := mustPhoenix(t)
	fri := time.Date(2026, 9, 25, 15, 0, 0, 0, loc)
	early := time.Date(2026, 9, 25, 0, 30, 0, 0, loc)
	tests := []struct {
		name string
		now  time.Time
		days int
		want time.Time
	}{
		{"1 day from Fri 15:00 → Sat 06:00", fri, 1, time.Date(2026, 9, 26, 6, 0, 0, 0, loc)},
		{"2 days from Fri 15:00 → Sun 06:00", fri, 2, time.Date(2026, 9, 27, 6, 0, 0, 0, loc)},
		{"7 days from Fri 15:00 → Fri Oct 2 06:00", fri, 7, time.Date(2026, 10, 2, 6, 0, 0, 0, loc)},
		{"14 days from Fri 15:00 → Fri Oct 9 06:00", fri, 14, time.Date(2026, 10, 9, 6, 0, 0, 0, loc)},
		{"1 day from Fri 00:30 still next calendar day", early, 1, time.Date(2026, 9, 26, 6, 0, 0, 0, loc)},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got := PauseUntilDays(nil, tc.now, loc, tc.days)
			if !got.Equal(tc.want) {
				t.Fatalf("got %s want %s", got, tc.want)
			}
		})
	}
}

func TestPauseUntilDaysWeekdayAndSeasonEdges(t *testing.T) {
	loc := mustPhoenix(t)
	fri := time.Date(2026, 9, 25, 15, 0, 0, 0, loc) // Friday; tomorrow = Sat 2026-09-26
	step := []store.Step{{StationID: "front-west", Minutes: 1}}
	tests := []struct {
		name string
		sch  []store.Schedule
		days int
		want time.Time
	}{
		{
			name: "tomorrow uses tomorrow's weekday (sat 05:15), not today's (fri 04:45)",
			sch:  []store.Schedule{withStep("04:45", []string{"fri"}), withStep("05:15", []string{"sat"})},
			days: 1,
			want: time.Date(2026, 9, 26, 5, 15, 0, 0, loc),
		},
		{
			name: "season starting exactly tomorrow applies",
			sch: []store.Schedule{{ID: "fall", Enabled: true, Start: "05:00", Steps: step,
				StartsOn: "2026-09-26"}},
			days: 1,
			want: time.Date(2026, 9, 26, 5, 0, 0, 0, loc),
		},
		{
			name: "season ending today does not apply tomorrow",
			sch: []store.Schedule{{ID: "summer", Enabled: true, Start: "05:00", Steps: step,
				EndsOn: "2026-09-25"}},
			days: 1,
			want: time.Date(2026, 9, 26, 6, 0, 0, 0, loc),
		},
		{
			name: "2 days lands on Sunday: sun-only program wins",
			sch:  []store.Schedule{withStep("07:30", []string{"sun"}), withStep("05:00", []string{"sat"})},
			days: 2,
			want: time.Date(2026, 9, 27, 7, 30, 0, 0, loc),
		},
		{
			name: "1 week lands on next Friday with daily 06:00 program",
			sch:  []store.Schedule{withStep("06:00", nil)},
			days: 7,
			want: time.Date(2026, 10, 2, 6, 0, 0, 0, loc),
		},
		{
			name: "14 days crosses into October season window",
			sch: []store.Schedule{{ID: "oct", Enabled: true, Start: "05:45", Steps: step,
				StartsOn: "2026-10-01", EndsOn: "2026-10-31"}},
			days: 14,
			want: time.Date(2026, 10, 9, 5, 45, 0, 0, loc),
		},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got := PauseUntilDays(tc.sch, fri, loc, tc.days)
			if !got.Equal(tc.want) {
				t.Fatalf("got %s want %s", got, tc.want)
			}
		})
	}
}

func TestPauseLabel(t *testing.T) {
	loc := mustPhoenix(t)
	now := time.Date(2026, 9, 25, 15, 0, 0, 0, loc)
	sat := time.Date(2026, 9, 26, 6, 0, 0, 0, loc)
	oct2 := time.Date(2026, 10, 2, 6, 0, 0, 0, loc)
	tests := []struct {
		name   string
		paused bool
		until  *time.Time
		want   string
		has    string
	}{
		{"not paused", false, &sat, "", ""},
		{"nil until", true, nil, "Paused until further notice", ""},
		{"within 6 days", true, &sat, "Paused until Sat 6:00 AM", ""},
		{"beyond 6 days includes Oct 2", true, &oct2, "Paused until Fri Oct 2, 6:00 AM", "Oct 2"},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			got := PauseLabel(tc.paused, tc.until, now, loc)
			if tc.want != "" && got != tc.want {
				t.Fatalf("got %q want %q", got, tc.want)
			}
			if tc.has != "" && !strings.Contains(got, tc.has) {
				t.Fatalf("got %q want substring %q", got, tc.has)
			}
			if !tc.paused && got != "" {
				t.Fatalf("not paused want empty, got %q", got)
			}
		})
	}
}
