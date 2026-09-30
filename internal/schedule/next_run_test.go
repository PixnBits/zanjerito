package schedule

import (
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/store"
)

func mustLoc(t *testing.T, name string) *time.Location {
	t.Helper()
	loc, err := time.LoadLocation(name)
	if err != nil {
		t.Fatal(err)
	}
	return loc
}

func prog(id, note, start string, enabled bool, days []string, startsOn, endsOn string, mins ...int) store.Schedule {
	steps := make([]store.Step, 0, len(mins))
	for _, m := range mins {
		steps = append(steps, store.Step{StationID: "s1", Minutes: m})
	}
	return store.Schedule{
		ID: id, Note: note, Enabled: enabled, Start: start,
		Weekdays: days, StartsOn: startsOn, EndsOn: endsOn, Steps: steps,
	}
}

func TestNextRun(t *testing.T) {
	phx := mustLoc(t, "America/Phoenix")
	// 2026-09-14 is a Monday.
	mon8 := time.Date(2026, 9, 14, 8, 0, 0, 0, phx)
	mon859 := time.Date(2026, 9, 14, 8, 59, 59, 0, phx)
	mon9 := time.Date(2026, 9, 14, 9, 0, 0, 0, phx)
	mon10 := time.Date(2026, 9, 14, 10, 0, 0, 0, phx)
	mon2350 := time.Date(2026, 9, 14, 23, 50, 0, 0, phx)
	noon := time.Date(2026, 1, 1, 12, 0, 0, 0, phx)

	daily := func(id, note, start string, mins ...int) store.Schedule {
		return prog(id, note, start, true, nil, "", "", mins...)
	}

	cases := []struct {
		name string
		now  time.Time
		loc  *time.Location
		sch  []store.Schedule
		ok   bool
		id   string
		at   time.Time
		min  int
		note string
	}{
		{
			name: "same-day later",
			now:  mon8,
			sch:  []store.Schedule{daily("a", "Program A", "09:00", 5)},
			ok:   true, id: "a", at: time.Date(2026, 9, 14, 9, 0, 0, 0, phx), min: 5, note: "Program A",
		},
		{
			name: "same minute already passed",
			now:  mon9,
			sch:  []store.Schedule{daily("a", "", "09:00", 5)},
			ok:   true, id: "a", at: time.Date(2026, 9, 15, 9, 0, 0, 0, phx), min: 5, note: "a",
		},
		{
			name: "one second before the minute still fires today",
			now:  mon859,
			sch:  []store.Schedule{daily("a", "Program A", "09:00", 4, 8)},
			ok:   true, id: "a", at: time.Date(2026, 9, 14, 9, 0, 0, 0, phx), min: 12, note: "Program A",
		},
		{
			name: "same-day earlier wraps to tomorrow",
			now:  mon10,
			sch:  []store.Schedule{daily("a", "Program A", "09:00", 5)},
			ok:   true, id: "a", at: time.Date(2026, 9, 15, 9, 0, 0, 0, phx), min: 5, note: "Program A",
		},
		{
			name: "midnight rollover",
			now:  mon2350,
			sch:  []store.Schedule{daily("a", "Program A", "00:10", 3)},
			ok:   true, id: "a", at: time.Date(2026, 9, 15, 0, 10, 0, 0, phx), min: 3, note: "Program A",
		},
		{
			name: "weekday list wraps to Friday",
			now:  mon8,
			sch:  []store.Schedule{prog("fri", "Program A", "06:00", true, []string{"Fri"}, "", "", 5)},
			ok:   true, id: "fri", at: time.Date(2026, 9, 18, 6, 0, 0, 0, phx), min: 5, note: "Program A",
		},
		{
			name: "monday-only after today's slot is next week",
			now:  mon10,
			sch:  []store.Schedule{prog("mon", "", "09:00", true, []string{"mon"}, "", "", 5)},
			ok:   true, id: "mon", at: time.Date(2026, 9, 21, 9, 0, 0, 0, phx), min: 5, note: "mon",
		},
		{
			name: "empty weekdays means every day",
			now:  mon8,
			sch:  []store.Schedule{prog("all", "Program A", "09:00", true, []string{}, "", "", 1)},
			ok:   true, id: "all", at: time.Date(2026, 9, 14, 9, 0, 0, 0, phx), min: 1, note: "Program A",
		},
		{
			name: "disabled schedule ignored",
			now:  mon8,
			sch: []store.Schedule{
				prog("off", "nope", "07:00", false, nil, "", "", 5),
				daily("on", "Program A", "09:00", 5),
			},
			ok: true, id: "on", at: time.Date(2026, 9, 14, 9, 0, 0, 0, phx), min: 5, note: "Program A",
		},
		{
			name: "invalid start ignored",
			now:  mon8,
			sch: []store.Schedule{
				prog("bad", "nope", "25:00", true, nil, "", "", 5),
				prog("bad2", "nope", "", true, nil, "", "", 5),
				prog("bad3", "nope", "12:60", true, nil, "", "", 5),
				daily("ok", "Program A", "09:00", 5),
			},
			ok: true, id: "ok", at: time.Date(2026, 9, 14, 9, 0, 0, 0, phx), min: 5, note: "Program A",
		},
		{
			name: "no steps ignored",
			now:  mon8,
			sch: []store.Schedule{
				prog("empty", "nope", "07:00", true, nil, "", ""),
				daily("ok", "Program A", "09:00", 2),
			},
			ok: true, id: "ok", at: time.Date(2026, 9, 14, 9, 0, 0, 0, phx), min: 2, note: "Program A",
		},
		{
			name: "starts_on in the future",
			now:  time.Date(2026, 9, 14, 1, 0, 0, 0, phx),
			sch:  []store.Schedule{prog("sea", "Program A", "06:00", true, nil, "2026-09-16", "", 5)},
			ok:   true, id: "sea", at: time.Date(2026, 9, 16, 6, 0, 0, 0, phx), min: 5, note: "Program A",
		},
		{
			name: "ends_on in the past",
			now:  mon8,
			sch:  []store.Schedule{prog("old", "Program A", "09:00", true, nil, "", "2026-09-13", 5)},
			ok:   false,
		},
		{
			name: "tie keeps list order",
			now:  time.Date(2026, 9, 14, 6, 0, 0, 0, phx),
			sch: []store.Schedule{
				daily("first", "Program A", "07:00", 5),
				daily("second", "Program B", "07:00", 9),
			},
			ok: true, id: "first", at: time.Date(2026, 9, 14, 7, 0, 0, 0, phx), min: 5, note: "Program A",
		},
		{
			name: "earliest wins even if later in the list",
			now:  time.Date(2026, 9, 14, 6, 0, 0, 0, phx),
			sch: []store.Schedule{
				daily("late", "Program A", "09:00", 5),
				daily("early", "Program B", "07:00", 4),
			},
			ok: true, id: "early", at: time.Date(2026, 9, 14, 7, 0, 0, 0, phx), min: 4, note: "Program B",
		},
		{
			name: "nothing qualifies",
			now:  mon8,
			sch: []store.Schedule{
				prog("off", "", "09:00", false, nil, "", "", 5),
				prog("bad", "", "nope", true, nil, "", "", 5),
			},
			ok: false,
		},
		{
			name: "empty list",
			now:  mon8,
			sch:  nil,
			ok:   false,
		},
		{
			name: "horizon includes day 399 and not day 400",
			now:  noon,
			sch: []store.Schedule{
				prog("far", "Program A", "00:00", true, nil, noon.AddDate(0, 0, 399).Format("2006-01-02"), "", 1),
			},
			ok: true, id: "far", at: time.Date(noon.AddDate(0, 0, 399).Year(), noon.AddDate(0, 0, 399).Month(), noon.AddDate(0, 0, 399).Day(), 0, 0, 0, 0, phx), min: 1, note: "Program A",
		},
		{
			name: "beyond horizon",
			now:  noon,
			sch: []store.Schedule{
				prog("too-far", "Program A", "00:00", true, nil, noon.AddDate(0, 0, 400).Format("2006-01-02"), "", 1),
			},
			ok: false,
		},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			loc := tc.loc
			if loc == nil {
				loc = phx
			}
			got, ok := NextRun(tc.sch, tc.now, loc)
			if ok != tc.ok {
				t.Fatalf("ok=%v want %v info=%+v", ok, tc.ok, got)
			}
			if !tc.ok {
				return
			}
			if got.ScheduleID != tc.id || !got.At.Equal(tc.at) || got.TotalMin != tc.min || got.Name != tc.note {
				t.Fatalf("got id=%s name=%q at=%s min=%d want id=%s name=%q at=%s min=%d",
					got.ScheduleID, got.Name, got.At.Format(time.RFC3339), got.TotalMin,
					tc.id, tc.note, tc.at.Format(time.RFC3339), tc.min)
			}
			wantEnd := got.At.Add(time.Duration(got.TotalMin) * time.Minute)
			if !got.EndsAt.Equal(wantEnd) {
				t.Fatalf("ends %s want %s", got.EndsAt, wantEnd)
			}
		})
	}

	if _, ok := NextRun(nil, time.Date(2026, 9, 14, 8, 0, 0, 0, time.UTC), nil); ok {
		t.Fatal("nil loc and nil schedules should not match")
	}
}

func TestNextRunAfterPause(t *testing.T) {
	phx := mustLoc(t, "America/Phoenix")
	day := time.Date(2026, 9, 14, 6, 0, 0, 0, phx)
	one := []store.Schedule{prog("a", "Program A", "06:00", true, nil, "", "", 5)}
	cases := []struct {
		name  string
		until time.Time
		sch   []store.Schedule
		id    string
		at    time.Time
	}{
		{
			name:  "exactly at until runs",
			until: day,
			sch:   one,
			id:    "a", at: day,
		},
		{
			name:  "until before the fire still runs that fire",
			until: day.Add(-time.Second),
			sch:   one,
			id:    "a", at: day,
		},
		{
			name:  "fire before until waits for the next one",
			until: day.Add(time.Second),
			sch:   one,
			id:    "a", at: day.AddDate(0, 0, 1),
		},
		{
			name:  "earliest at or after until wins",
			until: day,
			sch: []store.Schedule{
				prog("late", "Program A", "08:00", true, nil, "", "", 5),
				prog("on-time", "Program B", "06:00", true, nil, "", "", 4),
			},
			id: "on-time", at: day,
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got, ok := NextRunAfterPause(tc.sch, tc.until, phx)
			if !ok {
				t.Fatal("expected a fire")
			}
			if got.ScheduleID != tc.id || !got.At.Equal(tc.at) {
				t.Fatalf("got %s at %s want %s at %s", got.ScheduleID, got.At.Format(time.RFC3339), tc.id, tc.at.Format(time.RFC3339))
			}
		})
	}
}

func TestNextRunDSTDenver(t *testing.T) {
	loc := mustLoc(t, "America/Denver")
	daily := func(start string) []store.Schedule {
		return []store.Schedule{prog("a", "Program A", start, true, nil, "", "", 5)}
	}

	// Spring-forward: 2026-03-08 02:00 MST becomes 03:00 MDT. 02:30 does not exist.
	// time.Date normalizes it to 01:30 MST. NextRun must not use that instant.
	gap := time.Date(2026, 3, 8, 2, 30, 0, 0, loc)
	if gap.Format(time.RFC3339) != "2026-03-08T01:30:00-07:00" {
		t.Fatalf("time.Date skipped-time normalization changed: %s", gap.Format(time.RFC3339))
	}
	springNow := time.Date(2026, 3, 8, 1, 0, 0, 0, loc)
	got, ok := NextRun(daily("02:30"), springNow, loc)
	want := time.Date(2026, 3, 9, 2, 30, 0, 0, loc)
	if !ok || !got.At.Equal(want) || got.At.Equal(gap) {
		t.Fatalf("spring 02:30 got %v %s want %s", ok, got.At.Format(time.RFC3339), want.Format(time.RFC3339))
	}
	if got.At.Hour() != 2 || got.At.Minute() != 30 {
		t.Fatalf("spring next wall clock %s", got.At.Format(time.RFC3339))
	}

	got, ok = NextRun(daily("03:30"), springNow, loc)
	want = time.Date(2026, 3, 8, 3, 30, 0, 0, loc)
	if !ok || !got.At.Equal(want) {
		t.Fatalf("spring 03:30 got %v %s", ok, got.At.Format(time.RFC3339))
	}

	got, ok = NextRun(daily("01:30"), springNow, loc)
	want = time.Date(2026, 3, 8, 1, 30, 0, 0, loc)
	if !ok || !got.At.Equal(want) {
		t.Fatalf("spring 01:30 got %v %s", ok, got.At.Format(time.RFC3339))
	}

	onlyGap := []store.Schedule{prog("gap", "Program A", "02:30", true, nil, "", "2026-03-08", 5)}
	if _, ok := NextRun(onlyGap, springNow, loc); ok {
		t.Fatal("skipped wall time on the only in-season day is not a fire")
	}

	until := time.Date(2026, 3, 8, 1, 0, 0, 0, loc)
	got, ok = NextRunAfterPause(daily("02:30"), until, loc)
	want = time.Date(2026, 3, 9, 2, 30, 0, 0, loc)
	if !ok || !got.At.Equal(want) {
		t.Fatalf("spring after pause got %v %s", ok, got.At.Format(time.RFC3339))
	}

	// Fall-back: 2026-11-01 01:30 happens twice. time.Date returns the first (MDT).
	first := time.Date(2026, 11, 1, 1, 30, 0, 0, loc)
	if first.Format(time.RFC3339) != "2026-11-01T01:30:00-06:00" {
		t.Fatalf("time.Date fold choice changed: %s", first.Format(time.RFC3339))
	}
	second := first.Add(time.Hour)
	if second.Format(time.RFC3339) != "2026-11-01T01:30:00-07:00" {
		t.Fatalf("second 01:30 = %s", second.Format(time.RFC3339))
	}

	got, ok = NextRun(daily("01:30"), time.Date(2026, 11, 1, 0, 30, 0, 0, loc), loc)
	if !ok || !got.At.Equal(first) {
		t.Fatalf("before the fold got %v %s", ok, got.At.Format(time.RFC3339))
	}

	// 01:15 MST is the second pass through the hour, so 01:30 is still upcoming.
	second0115 := second.Add(-15 * time.Minute)
	if second0115.Hour() != 1 || second0115.Minute() != 15 {
		t.Fatalf("second 01:15 wall %s", second0115.Format(time.RFC3339))
	}
	got, ok = NextRun(daily("01:30"), second0115, loc)
	if !ok || !got.At.Equal(second) {
		t.Fatalf("during the second 01:xx got %v %s want %s", ok, got.At.Format(time.RFC3339), second.Format(time.RFC3339))
	}

	// 01:45 MDT is still the first pass. The HH:MM slot has passed for today.
	got, ok = NextRun(daily("01:30"), first.Add(15*time.Minute), loc)
	want = time.Date(2026, 11, 2, 1, 30, 0, 0, loc)
	if !ok || !got.At.Equal(want) {
		t.Fatalf("after first 01:30 got %v %s want %s", ok, got.At.Format(time.RFC3339), want.Format(time.RFC3339))
	}

	got, ok = NextRunAfterPause(daily("01:30"), first, loc)
	if !ok || !got.At.Equal(first) {
		t.Fatalf("pause exactly at first 01:30 got %v %s", ok, got.At.Format(time.RFC3339))
	}
	got, ok = NextRunAfterPause(daily("01:30"), first.Add(time.Second), loc)
	if !ok || !got.At.Equal(second) {
		t.Fatalf("pause just after first got %v %s", ok, got.At.Format(time.RFC3339))
	}
	got, ok = NextRunAfterPause(daily("01:30"), second.Add(time.Second), loc)
	want = time.Date(2026, 11, 2, 1, 30, 0, 0, loc)
	if !ok || !got.At.Equal(want) {
		t.Fatalf("pause just after second got %v %s want %s", ok, got.At.Format(time.RFC3339), want.Format(time.RFC3339))
	}
}
