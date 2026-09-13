package schedule

import (
	"context"
	"errors"
	"fmt"
	"log"
	"os"
	"strings"
	"sync"
	"time"

	"github.com/PixnBits/zanjerito/internal/engine"
	"github.com/PixnBits/zanjerito/internal/store"
)

const Phoenix = "America/Phoenix"

// Clock is injectable for tests.
type Clock interface {
	Now() time.Time
}

type realClock struct{}

func (realClock) Now() time.Time { return time.Now() }

// Runner fires enabled schedules against the engine.
type Runner struct {
	Eng  *engine.Engine
	Path string
	Loc  *time.Location
	Now  Clock
	Log  *log.Logger

	mu        sync.Mutex
	lastFired map[string]string // schedule id -> YYYY-MM-DD in Loc
}

func NewRunner(e *engine.Engine, path string) (*Runner, error) {
	loc, err := time.LoadLocation(Phoenix)
	if err != nil {
		return nil, err
	}
	return &Runner{
		Eng:       e,
		Path:      path,
		Loc:       loc,
		Now:       realClock{},
		Log:       log.New(os.Stderr, "schedule: ", log.LstdFlags),
		lastFired: map[string]string{},
	}, nil
}

// FrontParity is the example Front West 4 / North 8 / South 8 fixture (not bash dead-time).
// Live bash/front.sh on the Pi may differ (e.g. 2/3/3 at 08:23); the file on disk wins.
func FrontParity() store.Schedule {
	return store.Schedule{
		ID:       "front-parity",
		Enabled:  true,
		Note:     "front.sh parity: west 4, north 8, south 8",
		Weekdays: []string{"mon", "tue", "wed", "thu", "fri", "sat", "sun"},
		Start:    "06:00",
		Steps: []store.Step{
			{StationID: "front-west", Minutes: 4},
			{StationID: "front-north", Minutes: 8},
			{StationID: "front-south", Minutes: 8},
		},
	}
}

func Itinerary(sch store.Schedule) ([]engine.Step, error) {
	out := make([]engine.Step, 0, len(sch.Steps))
	for _, s := range sch.Steps {
		if s.StationID == "" || s.Minutes <= 0 {
			return nil, fmt.Errorf("schedule: bad step %+v", s)
		}
		out = append(out, engine.Step{
			StationID: s.StationID,
			Duration:  time.Duration(s.Minutes) * time.Minute,
		})
	}
	if len(out) == 0 {
		return nil, fmt.Errorf("schedule: %s has no steps", sch.ID)
	}
	return out, nil
}

func weekdayName(d time.Weekday) string {
	return []string{"sun", "mon", "tue", "wed", "thu", "fri", "sat"}[int(d)]
}

func inSeason(sch store.Schedule, day time.Time) bool {
	ds := day.Format("2006-01-02")
	if sch.StartsOn != "" && ds < sch.StartsOn {
		return false
	}
	if sch.EndsOn != "" && ds > sch.EndsOn {
		return false
	}
	return true
}

func weekdayOK(sch store.Schedule, day time.Time) bool {
	if len(sch.Weekdays) == 0 {
		return true
	}
	want := weekdayName(day.Weekday())
	for _, w := range sch.Weekdays {
		if strings.ToLower(strings.TrimSpace(w)) == want {
			return true
		}
	}
	return false
}

func clockOK(sch store.Schedule, now time.Time) bool {
	h, m, err := parseHHMM(sch.Start)
	if err != nil {
		return false
	}
	return now.Hour() == h && now.Minute() == m
}

func parseHHMM(s string) (int, int, error) {
	var h, m int
	n, err := fmt.Sscanf(strings.TrimSpace(s), "%d:%d", &h, &m)
	if err != nil || n != 2 || h < 0 || h > 23 || m < 0 || m > 59 {
		return 0, 0, fmt.Errorf("bad start %q", s)
	}
	return h, m, nil
}

func (r *Runner) due(sch store.Schedule, now time.Time) bool {
	if !sch.Enabled {
		return false
	}
	if !inSeason(sch, now) || !weekdayOK(sch, now) || !clockOK(sch, now) {
		return false
	}
	day := now.Format("2006-01-02")
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.lastFired[sch.ID] != day
}

func (r *Runner) markFired(id string, now time.Time) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.lastFired[id] = now.Format("2006-01-02")
}

// Tick loads schedules and starts any that are due. Busy engine → skip+log (D13).
func (r *Runner) Tick(ctx context.Context) error {
	if r.Now == nil {
		r.Now = realClock{}
	}
	if r.Loc == nil {
		loc, err := time.LoadLocation(Phoenix)
		if err != nil {
			return err
		}
		r.Loc = loc
	}
	if r.Log == nil {
		r.Log = log.New(os.Stderr, "schedule: ", log.LstdFlags)
	}
	f, err := store.Load(r.Path)
	if err != nil {
		return err
	}
	now := r.Now.Now().In(r.Loc)
	for _, sch := range f.Schedules {
		if !r.due(sch, now) {
			continue
		}
		steps, err := Itinerary(sch)
		if err != nil {
			r.Log.Printf("skip %s: %v", sch.ID, err)
			continue
		}
		r.Log.Printf("start %s at %s Phoenix (%d steps)", sch.ID, now.Format("15:04"), len(steps))
		err = r.Eng.RunItinerary(ctx, steps)
		if errors.Is(err, engine.ErrBusy) {
			r.Log.Printf("skip %s: busy (D13)", sch.ID)
			continue
		}
		if err != nil {
			r.Log.Printf("run %s: %v", sch.ID, err)
			continue
		}
		r.markFired(sch.ID, now)
		r.Log.Printf("done %s", sch.ID)
	}
	return nil
}

// Loop Ticks until ctx is cancelled. interval defaults to 1s.
func (r *Runner) Loop(ctx context.Context, interval time.Duration) {
	if interval <= 0 {
		interval = time.Second
	}
	if r.Log == nil {
		r.Log = log.New(os.Stderr, "schedule: ", log.LstdFlags)
	}
	tick := time.NewTicker(interval)
	defer tick.Stop()
	if err := r.Tick(ctx); err != nil && !errors.Is(err, context.Canceled) {
		r.Log.Printf("tick: %v", err)
	}
	for {
		select {
		case <-ctx.Done():
			return
		case <-tick.C:
			if err := r.Tick(ctx); err != nil && !errors.Is(err, context.Canceled) {
				r.Log.Printf("tick: %v", err)
			}
		}
	}
}
