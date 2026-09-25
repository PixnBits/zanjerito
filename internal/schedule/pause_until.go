package schedule

import (
	"time"

	"github.com/PixnBits/zanjerito/internal/store"
)

const (
	MinPauseDays = 1
	MaxPauseDays = 14
)

func locOrUTC(loc *time.Location) *time.Location {
	if loc == nil {
		return time.UTC
	}
	return loc
}

// MorningCutoff is the exclusive pause-until instant for a calendar date.
//
// day is any instant; the calendar date is taken in loc (y, m, d := day.In(loc).Date()).
// Enabled schedules with a parseable Start, at least one step, and that apply on
// that date (weekdayOK and inSeason, evaluated at noon local) are considered.
// The earliest such start (HH:MM) wins when it is before 12:00; otherwise the
// cutoff is 06:00 local.
//
// Pause-until is exclusive: the engine treats until<=now as expired, and due()
// matches the HH:MM minute, so ending exactly at the start instant means the
// scheduler tick at HH:MM:00+ sees the pause expired and that program DOES run.
// The noon guard keeps "morning" meaning morning (an evening-only program still
// runs since the pause ended at 06:00).
func MorningCutoff(schedules []store.Schedule, day time.Time, loc *time.Location) time.Time {
	loc = locOrUTC(loc)
	y, m, d := day.In(loc).Date()
	noon := time.Date(y, m, d, 12, 0, 0, 0, loc)
	found := false
	bestH, bestM := 0, 0
	for _, sch := range schedules {
		if !sch.Enabled {
			continue
		}
		if len(sch.Steps) == 0 {
			continue
		}
		h, mm, err := parseHHMM(sch.Start)
		if err != nil {
			continue
		}
		if !weekdayOK(sch, noon) || !inSeason(sch, noon) {
			continue
		}
		if !found || h < bestH || (h == bestH && mm < bestM) {
			found = true
			bestH, bestM = h, mm
		}
	}
	if found && bestH < 12 {
		return time.Date(y, m, d, bestH, bestM, 0, 0, loc)
	}
	return time.Date(y, m, d, 6, 0, 0, 0, loc)
}

// PauseUntilDays returns MorningCutoff for the local calendar date of now.In(loc) + days.
// days=1 is "until tomorrow morning". The pause is morning-anchored: "2 days"
// pressed Fri 3pm ends Sun at the morning cutoff (not 48 hours later).
func PauseUntilDays(schedules []store.Schedule, now time.Time, loc *time.Location, days int) time.Time {
	loc = locOrUTC(loc)
	y, m, d := now.In(loc).Date()
	day := time.Date(y, m, d+days, 12, 0, 0, 0, loc)
	return MorningCutoff(schedules, day, loc)
}

// PauseLabel is the household-friendly pause sentence for API/UI.
// "" if !paused; "Paused until further notice" if until==nil; otherwise
// "Paused until Sat 6:00 AM" when until is within 6 days (until.Sub(now) <= 6*24h),
// else "Paused until Fri Oct 2, 6:00 AM".
func PauseLabel(paused bool, until *time.Time, now time.Time, loc *time.Location) string {
	if !paused {
		return ""
	}
	if until == nil {
		return "Paused until further notice"
	}
	loc = locOrUTC(loc)
	t := until.In(loc)
	if until.Sub(now) <= 6*24*time.Hour {
		return "Paused until " + t.Format("Mon 3:04 PM")
	}
	return "Paused until " + t.Format("Mon Jan 2, 3:04 PM")
}
