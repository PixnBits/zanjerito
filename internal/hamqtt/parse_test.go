package hamqtt

import (
	"os"
	"path/filepath"
	"reflect"
	"testing"
)

func haFixture(t *testing.T, name string) []byte {
	t.Helper()
	p := filepath.Join("..", "api", "testdata", "ha", name)
	b, err := os.ReadFile(p)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

func TestParseStateFixtures(t *testing.T) {
	dir := filepath.Join("..", "api", "testdata", "ha")
	matches, err := filepath.Glob(filepath.Join(dir, "*.json"))
	if err != nil {
		t.Fatal(err)
	}
	if len(matches) == 0 {
		t.Fatal("no ha fixtures")
	}
	for _, p := range matches {
		body, err := os.ReadFile(p)
		if err != nil {
			t.Fatal(err)
		}
		ids, ok := ParseState(body)
		if !ok {
			t.Errorf("%s: ParseState not ok", filepath.Base(p))
		}
		if !reflect.DeepEqual(ids, []string{"zone_1", "zone_2"}) {
			t.Errorf("%s: zoneIDs %v", filepath.Base(p), ids)
		}
	}
}

func TestParseStateRejects(t *testing.T) {
	cases := map[string][]byte{
		"invalid JSON":   []byte(`{`),
		"JSON array":     []byte(`[{"phase":"Idle"}]`),
		"JSON null":      []byte(`null`),
		"JSON string":    []byte(`"Idle"`),
		"JSON number":    []byte(`1`),
		"missing phase":  []byte(`{"paused":false}`),
		"empty":          []byte(``),
		"whitespace obj": []byte(`{}`),
	}
	for name, body := range cases {
		if _, ok := ParseState(body); ok {
			t.Errorf("%s: want not ok", name)
		}
	}
}

func TestParseStateToleratesMissingZones(t *testing.T) {
	ids, ok := ParseState([]byte(`{"phase":"Idle"}`))
	if !ok {
		t.Fatal("want ok")
	}
	if ids != nil {
		t.Fatalf("zoneIDs %v", ids)
	}
}

func TestParseStateSkipsBadZoneEntries(t *testing.T) {
	ids, ok := ParseState([]byte(`{"phase":"Idle","zones":[{"id":"zone_1","on":true},"x",{"on":true},{"id":""},{"id":"zone_2"}]}`))
	if !ok {
		t.Fatal("want ok")
	}
	if !reflect.DeepEqual(ids, []string{"zone_1", "zone_2"}) {
		t.Fatalf("zoneIDs %v", ids)
	}
}
