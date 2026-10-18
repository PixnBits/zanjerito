package store

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestScheduleLegacyRoundTripOmitsNameAndNotes(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "config.json")
	in := File{
		Config: validCfg(),
		Schedules: []Schedule{{
			ID: "dawn", Enabled: true, Note: "legacy title", Start: "06:10",
			Steps: []Step{{StationID: "front-north", Minutes: 6}},
		}},
	}
	if err := Save(path, in); err != nil {
		t.Fatal(err)
	}
	b, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(string(b), `"name"`) || strings.Contains(string(b), `"notes"`) {
		t.Fatalf("legacy file gained name or notes:\n%s", b)
	}
	out, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if len(out.Schedules) != 1 {
		t.Fatalf("schedules %+v", out.Schedules)
	}
	got := out.Schedules[0]
	if got.Note != "legacy title" || got.Name != "" || got.Notes != "" || got.Start != "06:10" {
		t.Fatalf("load changed schedule %+v", got)
	}

	got.Name = "Lawn · Morning"
	got.Notes = "only in details"
	out.Schedules[0] = got
	if err := Save(path, out); err != nil {
		t.Fatal(err)
	}
	again, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if again.Schedules[0].Name != "Lawn · Morning" || again.Schedules[0].Notes != "only in details" {
		t.Fatalf("round trip %+v", again.Schedules[0])
	}
	if again.Schedules[0].Note != "legacy title" {
		t.Fatalf("note changed %+v", again.Schedules[0])
	}

	again.Schedules[0].Name = ""
	again.Schedules[0].Notes = ""
	if err := Save(path, again); err != nil {
		t.Fatal(err)
	}
	b, err = os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if strings.Contains(string(b), `"name"`) || strings.Contains(string(b), `"notes"`) {
		t.Fatalf("cleared name/notes were written:\n%s", b)
	}
}

func TestDisplayNameFallback(t *testing.T) {
	s := Schedule{ID: "prog-1", Notes: "secret notes"}
	if s.DisplayName() != "prog-1" {
		t.Fatalf("id fallback %q", s.DisplayName())
	}
	s.Note = "  legacy note  "
	if s.DisplayName() != "legacy note" {
		t.Fatalf("note fallback %q", s.DisplayName())
	}
	s.Name = "  Lawn · Morning  "
	if s.DisplayName() != "Lawn · Morning" {
		t.Fatalf("name %q", s.DisplayName())
	}
	if strings.Contains(s.DisplayName(), "secret") {
		t.Fatal("notes leaked into the display name")
	}
}
