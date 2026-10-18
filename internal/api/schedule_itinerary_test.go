package api

import (
	"encoding/json"
	"net/http"
	"reflect"
	"testing"

	"github.com/PixnBits/zanjerito/internal/store"
)

func TestScheduleItineraryRoundTrip(t *testing.T) {
	s := newTestServer(t)
	want := []store.Step{
		{StationID: "lawn-a", Minutes: 12},
		{StationID: "trees", Minutes: 6},
		{StationID: "beds", Minutes: 3},
	}
	body := map[string]any{
		"id":       "lawn-morning",
		"enabled":  true,
		"name":     "Lawn · Morning",
		"weekdays": []string{"mon", "wed", "fri"},
		"start":    "06:10",
		"steps":    want,
	}
	rr := doJSON(t, s, http.MethodPut, "/api/schedules/lawn-morning", body)
	if rr.Code != http.StatusOK {
		t.Fatalf("put %d %s", rr.Code, rr.Body.String())
	}
	rr = doJSON(t, s, http.MethodGet, "/api/schedules/lawn-morning", nil)
	if rr.Code != http.StatusOK {
		t.Fatalf("get %d %s", rr.Code, rr.Body.String())
	}
	var got store.Schedule
	if err := json.Unmarshal(rr.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	if got.Name != "Lawn · Morning" || got.Start != "06:10" {
		t.Fatalf("name/start %+v", got)
	}
	if !reflect.DeepEqual(got.Steps, want) {
		t.Fatalf("steps %+v", got.Steps)
	}
	if !reflect.DeepEqual(got.Weekdays, []string{"mon", "wed", "fri"}) {
		t.Fatalf("weekdays %+v", got.Weekdays)
	}
}
