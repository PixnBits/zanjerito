package schedule

import (
	"testing"
	"time"

	"github.com/PixnBits/zanjerito/internal/store"
)

func TestNextRunUsesDisplayName(t *testing.T) {
	loc := time.UTC
	now := time.Date(2026, 1, 5, 1, 0, 0, 0, loc) // Monday 01:00
	base := store.Schedule{
		ID: "prog-1", Enabled: true, Start: "06:10",
		Steps: []store.Step{{StationID: "s1", Minutes: 6}},
	}
	info, ok := NextRun([]store.Schedule{base}, now, loc)
	if !ok || info.Name != "prog-1" {
		t.Fatalf("id fallback %+v ok=%v", info, ok)
	}
	base.Note = "legacy note"
	base.Notes = "secret notes"
	info, ok = NextRun([]store.Schedule{base}, now, loc)
	if !ok || info.Name != "legacy note" {
		t.Fatalf("note fallback %+v", info)
	}
	base.Name = "Lawn · Morning"
	info, ok = NextRun([]store.Schedule{base}, now, loc)
	if !ok || info.Name != "Lawn · Morning" {
		t.Fatalf("display name %+v", info)
	}
}
