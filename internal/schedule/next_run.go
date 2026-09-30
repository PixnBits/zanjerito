package schedule

import (
	"sort"
	"time"

	"github.com/PixnBits/zanjerito/internal/store"
)

// nextRunHorizon is how many local calendar dates NextRun scans.
// The embedded UI uses the same 400-day window.
const nextRunHorizon = 400

// NextRunInfo is one scheduled fire. Name is the schedule note, or the id
// when the note is blank (same as programName). EndsAt is At plus TotalMin.
type NextRunInfo struct {
	ScheduleID string
	Name       string
	At         time.Time
	TotalMin   int
	EndsAt     time.Time
}

// NextRun is the next scheduled fire strictly after now's local minute.
// A start in the same local HH:MM as now has already passed, including when
// now is exactly the start of that minute. Same-day fires are included only
// when the start HH:MM is later than now's HH:MM. This matches the web
// next-fire rule, not the scheduler's "this minute is due" check.
//
// Enabled schedules with a parseable HH:MM, at least one step, a matching
// weekday (empty weekdays = every day), and an in-season date are candidates.
// The earliest instant wins. Equal instants keep the earlier schedule in the
// slice. Invalid starts and empty step lists are ignored. Nothing in the
// horizon returns false.
//
// Instants are built with time.Date in loc. DST:
//
//   - A skipped civil time does not occur, so it is not a fire. On
//     America/Denver 2026-03-08, 02:00–02:59 never happens. time.Date maps
//     02:30 to 01:30 MST; that normalized instant is not 02:30 and is not
//     used. The scheduler matches wall HH:MM and will not fire it either.
//     The next real occurrence is a later date whose 02:30 exists.
//   - A repeated civil time (America/Denver 2026-11-01 01:00–01:59) yields
//     two instants. time.Date returns one of them and does not promise which.
//     NextRun probes that result ±1 hour (the US fold) and keeps every
//     instant whose wall clock is the requested Y-M-D HH:MM. The HH:MM rule
//     decides whether today's slot is still upcoming; among matches, the
//     earliest instant strictly after now is used. A one-hour fold is the
//     assumption. America/Phoenix has no fold, so there is one instant.
//
// loc nil means UTC.
func NextRun(schedules []store.Schedule, now time.Time, loc *time.Location) (NextRunInfo, bool) {
	loc = locOrUTC(loc)
	local := now.In(loc)
	y, m, d := local.Date()
	start := time.Date(y, m, d, 12, 0, 0, 0, loc)
	nowH, nowMin := local.Hour(), local.Minute()
	return scanRuns(schedules, start, loc, func(offset, hour, min int, at time.Time) bool {
		if offset == 0 && (nowH > hour || (nowH == hour && nowMin >= min)) {
			return false
		}
		return at.After(now)
	})
}

// NextRunAfterPause is the first fire at or after until.
// Pause-until is exclusive: a fire whose instant equals until does run.
// Fires with at.Before(until) do not. The scan starts on until's local date
// and uses the same eligibility and DST rules as NextRun. It does not apply
// the "current minute already passed" rule. An active pause's until is after
// now; this function itself does not know now. loc nil means UTC.
func NextRunAfterPause(schedules []store.Schedule, until time.Time, loc *time.Location) (NextRunInfo, bool) {
	loc = locOrUTC(loc)
	u := until.In(loc)
	y, m, d := u.Date()
	start := time.Date(y, m, d, 12, 0, 0, 0, loc)
	return scanRuns(schedules, start, loc, func(_, _, _ int, at time.Time) bool {
		return !at.Before(until)
	})
}

func scanRuns(schedules []store.Schedule, start time.Time, loc *time.Location, accept func(offset, hour, min int, at time.Time) bool) (NextRunInfo, bool) {
	var best NextRunInfo
	found := false
	for offset := 0; offset < nextRunHorizon; offset++ {
		day := start.AddDate(0, 0, offset)
		for _, sch := range schedules {
			if !sch.Enabled || len(sch.Steps) == 0 {
				continue
			}
			hour, min, err := parseHHMM(sch.Start)
			if err != nil {
				continue
			}
			if !weekdayOK(sch, day) || !inSeason(sch, day) {
				continue
			}
			for _, at := range instantsForWall(day.Year(), day.Month(), day.Day(), hour, min, loc) {
				if !accept(offset, hour, min, at) {
					continue
				}
				if !found || at.Before(best.At) {
					best = nextRunInfo(sch, at)
					found = true
				}
			}
		}
		if found {
			return best, true
		}
	}
	return NextRunInfo{}, false
}

func nextRunInfo(sch store.Schedule, at time.Time) NextRunInfo {
	total := 0
	for _, s := range sch.Steps {
		total += s.Minutes
	}
	return NextRunInfo{
		ScheduleID: sch.ID,
		Name:       programName(sch),
		At:         at,
		TotalMin:   total,
		EndsAt:     at.Add(time.Duration(total) * time.Minute),
	}
}

// instantsForWall returns the absolute times when loc's wall clock reads
// y-m-d hour:min:00. A skipped civil time returns nil. A one-hour fold
// returns both occurrences, earliest first.
func instantsForWall(y int, month time.Month, day, hour, min int, loc *time.Location) []time.Time {
	primary := time.Date(y, month, day, hour, min, 0, 0, loc)
	cands := []time.Time{primary, primary.Add(time.Hour), primary.Add(-time.Hour)}
	var out []time.Time
	for _, c := range cands {
		c = c.In(loc)
		if c.Year() != y || c.Month() != month || c.Day() != day || c.Hour() != hour || c.Minute() != min || c.Second() != 0 {
			continue
		}
		dup := false
		for _, e := range out {
			if e.Equal(c) {
				dup = true
				break
			}
		}
		if !dup {
			out = append(out, c)
		}
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Before(out[j]) })
	return out
}
