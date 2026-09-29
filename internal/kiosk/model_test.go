package kiosk

import (
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"
)

func TestLoadFixtures(t *testing.T) {
	idle, err := LoadFixture("testdata", "idle")
	if err != nil {
		t.Fatal(err)
	}
	if idle.Status.Phase != "Idle" || idle.Status.Paused || idle.Status.StationsOn != nil {
		t.Fatalf("idle status %+v", idle.Status)
	}
	if !idle.Status.RainStrip.Show || idle.Status.RainStrip.Inches != 0.24 || idle.Status.RainStrip.Hours != 24 {
		t.Fatalf("rain strip %+v", idle.Status.RainStrip)
	}
	if idle.Status.Rain.Enabled != true || idle.Status.Rain.Total24hInches != 0.24 {
		t.Fatalf("rain %+v", idle.Status.Rain)
	}
	if len(idle.Stations) != 4 || idle.Stations[0].ID != "az01" || idle.Stations[0].Title != "Test Station 1" {
		t.Fatalf("stations %+v", idle.Stations)
	}
	if len(idle.Schedules) != 2 || idle.Schedules[0].ID != "morning" || !idle.Schedules[0].Enabled {
		t.Fatalf("schedules %+v", idle.Schedules)
	}
	if idle.Schedules[1].Enabled {
		t.Fatal("disabled schedule loaded as enabled")
	}
	if idle.Status.Timezone == "" || idle.Status.Now == "" {
		t.Fatal("missing clock fields")
	}

	running, err := LoadFixture("testdata", "running")
	if err != nil {
		t.Fatal(err)
	}
	if running.Status.CurrentStation != "az02" || !running.Status.StationOn("az02") || running.Status.StationOn("az01") {
		t.Fatalf("running %+v", running.Status)
	}
	if len(running.Status.StationsOn) != 1 || running.Status.StationsOn[0] != "az02" {
		t.Fatalf("stations_on %v", running.Status.StationsOn)
	}

	paused, err := LoadFixture("testdata", "paused")
	if err != nil {
		t.Fatal(err)
	}
	if !paused.Status.Paused || paused.Status.PausedLabel != "Paused until Fri" || paused.Status.PausedUntil == nil {
		t.Fatalf("paused %+v", paused.Status)
	}
	if _, err := LoadFixture("testdata", "nope"); err == nil {
		t.Fatal("expected bad fixture-status error")
	}
}

func TestNextRunFixture(t *testing.T) {
	idle, err := LoadFixture("testdata", "idle")
	if err != nil {
		t.Fatal(err)
	}
	now, err := idle.Status.NowTime()
	if err != nil {
		t.Fatal(err)
	}
	loc := idle.Status.Location()
	up := NextRun(idle.Schedules, idle.Stations, now, loc)
	if !up.Found || up.ScheduleID != "morning" || up.Label != "Today 08:23" {
		t.Fatalf("%+v", up)
	}
	at := up.At.In(loc)
	if at.Hour() != 8 || at.Minute() != 23 || !at.After(now) {
		t.Fatal(at)
	}
	if !strings.Contains(up.Summary, "Test Station 1 2m") || !strings.Contains(up.Summary, "Test Station 2 3m") {
		t.Fatal(up.Summary)
	}

	running, err := LoadFixture("testdata", "running")
	if err != nil {
		t.Fatal(err)
	}
	now, err = running.Status.NowTime()
	if err != nil {
		t.Fatal(err)
	}
	up = NextRun(running.Schedules, running.Stations, now, running.Status.Location())
	if !up.Found || up.Label != "Tomorrow 08:23" || up.ScheduleID != "morning" {
		t.Fatalf("%+v", up)
	}
}

func TestNextRunTable(t *testing.T) {
	loc := time.FixedZone("L", -7*3600)
	at := func(y int, m time.Month, d, hh, mm int) time.Time {
		return time.Date(y, m, d, hh, mm, 0, 0, loc)
	}
	stations := []Station{{ID: "az01", Title: "Test Station 1"}, {ID: "az03", Title: "Test Station 3"}}
	daily := func(id, start string, enabled bool) Schedule {
		return Schedule{ID: id, Enabled: enabled, Start: start, Steps: []Step{{StationID: "az01", Minutes: 2}}}
	}
	thu := Schedule{
		ID: "thu", Enabled: true, Start: "07:31", Weekdays: []string{"thu"},
		Steps: []Step{{StationID: "az03", Minutes: 5}},
	}

	cases := []struct {
		name    string
		now     time.Time
		sch     []Schedule
		wantOn  bool
		label   string
		id      string
		y, d    int
		month   time.Month
		summary string
	}{
		{
			name:   "weekday wrap",
			now:    at(2026, 10, 2, 12, 0),
			sch:    []Schedule{thu},
			wantOn: true, label: "Thu 07:31", id: "thu",
			y: 2026, month: 10, d: 8,
			summary: "Test Station 3 5m",
		},
		{
			name:   "disabled skipped",
			now:    at(2026, 10, 2, 8, 0),
			sch:    []Schedule{daily("soon", "09:00", false), thu},
			wantOn: true, label: "Thu 07:31", id: "thu",
			y: 2026, month: 10, d: 8,
		},
		{
			name: "starts_on in the future",
			now:  at(2026, 10, 2, 7, 0),
			sch: []Schedule{{
				ID: "later", Enabled: true, Start: "08:23", StartsOn: "2026-10-05",
				Steps: []Step{{StationID: "az01", Minutes: 2}},
			}},
			wantOn: true, label: "Mon 08:23", id: "later",
			y: 2026, month: 10, d: 5,
		},
		{
			name: "ends_on passed",
			now:  at(2026, 10, 2, 7, 0),
			sch: []Schedule{{
				ID: "old", Enabled: true, Start: "08:23", EndsOn: "2026-09-01",
			}},
		},
		{
			name:   "time already passed today",
			now:    at(2026, 10, 2, 10, 0),
			sch:    []Schedule{daily("morn", "08:23", true)},
			wantOn: true, label: "Tomorrow 08:23", id: "morn",
			y: 2026, month: 10, d: 3,
		},
		{
			name: "ends_on today still ahead",
			now:  at(2026, 10, 2, 7, 0),
			sch: []Schedule{{
				ID: "last", Enabled: true, Start: "08:23", EndsOn: "2026-10-02",
			}},
			wantOn: true, label: "Today 08:23", id: "last",
			y: 2026, month: 10, d: 2,
		},
		{
			name: "ends_on today time passed",
			now:  at(2026, 10, 2, 10, 0),
			sch: []Schedule{{
				ID: "last", Enabled: true, Start: "08:23", EndsOn: "2026-10-02",
			}},
		},
		{
			name:   "empty weekdays are every day",
			now:    at(2026, 10, 2, 7, 0),
			sch:    []Schedule{daily("all", "08:23", true)},
			wantOn: true, label: "Today 08:23", id: "all",
			y: 2026, month: 10, d: 2,
		},
		{
			name: "weekday case",
			now:  at(2026, 10, 2, 12, 0),
			sch: []Schedule{{
				ID: "thu", Enabled: true, Start: "07:31", Weekdays: []string{"THU"},
			}},
			wantOn: true, label: "Thu 07:31", id: "thu",
			y: 2026, month: 10, d: 8,
		},
		{
			name: "tie keeps earlier schedule",
			now:  at(2026, 10, 1, 12, 0),
			sch: []Schedule{
				{ID: "a", Enabled: true, Start: "09:00", Weekdays: []string{"fri"}},
				{ID: "b", Enabled: true, Start: "09:00", Weekdays: []string{"fri"}},
			},
			wantOn: true, label: "Tomorrow 09:00", id: "a",
			y: 2026, month: 10, d: 2,
		},
		{
			name: "all disabled",
			now:  at(2026, 10, 2, 7, 0),
			sch:  []Schedule{daily("off", "08:23", false)},
		},
		{
			name: "starts_on skips a non-matching weekday",
			now:  at(2026, 10, 2, 12, 0),
			sch: []Schedule{{
				ID: "mon", Enabled: true, Start: "08:00", Weekdays: []string{"mon"},
				StartsOn: "2026-10-03",
			}},
			wantOn: true, label: "Mon 08:00", id: "mon",
			y: 2026, month: 10, d: 5,
		},
		{
			name: "unknown station id in summary",
			now:  at(2026, 10, 2, 7, 0),
			sch: []Schedule{{
				ID: "x", Enabled: true, Start: "08:00",
				Steps: []Step{{StationID: "az09", Minutes: 4}},
			}},
			wantOn: true, label: "Today 08:00", id: "x",
			y: 2026, month: 10, d: 2,
			summary: "az09 4m",
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			up := NextRun(tc.sch, stations, tc.now, loc)
			if up.Found != tc.wantOn {
				t.Fatalf("found %v %+v", up.Found, up)
			}
			if !tc.wantOn {
				return
			}
			if up.Label != tc.label || up.ScheduleID != tc.id {
				t.Fatalf("label %q id %q", up.Label, up.ScheduleID)
			}
			got := up.At.In(loc)
			if got.Year() != tc.y || got.Month() != tc.month || got.Day() != tc.d {
				t.Fatal(got)
			}
			if tc.summary != "" && up.Summary != tc.summary {
				t.Fatalf("summary %q", up.Summary)
			}
		})
	}
}

func TestPollStatusMoreOftenThanCatalog(t *testing.T) {
	var statusN, stationN, schedN, bad atomic.Int32
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			bad.Add(1)
		}
		switch r.URL.Path {
		case "/api/status":
			statusN.Add(1)
			_, _ = w.Write([]byte(`{"phase":"Idle","now":"2026-01-02T03:04:05Z","timezone":"UTC","stations_on":null,"rain_strip":{"show":false,"inches":0,"hours":24}}`))
		case "/api/stations":
			stationN.Add(1)
			_, _ = w.Write([]byte(`{"stations":[{"id":"az01","title":"Test Station 1","color":"red"}]}`))
		case "/api/schedules":
			schedN.Add(1)
			_, _ = w.Write([]byte(`{"schedules":[]}`))
		default:
			bad.Add(1)
			http.NotFound(w, r)
		}
	}))
	t.Cleanup(srv.Close)

	c := NewClient(srv.URL)
	if c.HTTP.Timeout != ClientTimeout {
		t.Fatalf("timeout %s", c.HTTP.Timeout)
	}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	snap, err := c.Load(ctx)
	if err != nil {
		t.Fatal(err)
	}
	if snap.Status.Phase != "Idle" || len(snap.Stations) != 1 || snap.Stations[0].ID != "az01" {
		t.Fatalf("%+v", snap)
	}
	done := make(chan struct{})
	go func() {
		_ = c.Poll(ctx, PollIntervals{Status: 15 * time.Millisecond, Catalog: time.Hour}, snap, func(Snapshot) {})
		close(done)
	}()
	deadline := time.Now().Add(2 * time.Second)
	for statusN.Load() < 3 && time.Now().Before(deadline) {
		time.Sleep(5 * time.Millisecond)
	}
	cancel()
	select {
	case <-done:
	case <-time.After(2 * time.Second):
		t.Fatal("poll did not return")
	}
	if statusN.Load() < 3 {
		t.Fatalf("status polls %d", statusN.Load())
	}
	if stationN.Load() != 1 || schedN.Load() != 1 {
		t.Fatalf("stations %d schedules %d", stationN.Load(), schedN.Load())
	}
	if bad.Load() != 0 {
		t.Fatalf("unexpected requests %d", bad.Load())
	}
}
