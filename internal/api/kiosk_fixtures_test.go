package api

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"sort"
	"testing"
)

func TestNativeKioskFixturesMatchKioskBody(t *testing.T) {
	root := filepath.Join("..", "..", "native-kiosk", "tests", "fixtures")
	ents, err := os.ReadDir(root)
	if err != nil {
		t.Fatal(err)
	}
	names := make([]string, 0, len(ents))
	for _, e := range ents {
		if e.IsDir() {
			names = append(names, e.Name())
		}
	}
	sort.Strings(names)
	if len(names) == 0 {
		t.Fatal("no fixture directories")
	}
	want := []string{
		"now", "timezone", "phase", "lockout", "last_error", "current_station", "stations_on",
		"pause", "rain_strip", "rain", "next_run", "next_effective_run", "run", "stations", "soil",
	}
	for _, name := range names {
		path := filepath.Join(root, name, "kiosk.json")
		raw, err := os.ReadFile(path)
		if err != nil {
			t.Errorf("%s: %v", name, err)
			continue
		}
		dec := json.NewDecoder(bytes.NewReader(raw))
		dec.DisallowUnknownFields()
		var body kioskBody
		if err := dec.Decode(&body); err != nil {
			t.Errorf("%s: decode: %v", name, err)
			continue
		}
		var m map[string]any
		if err := json.Unmarshal(raw, &m); err != nil {
			t.Errorf("%s: map: %v", name, err)
			continue
		}
		for _, k := range want {
			if _, ok := m[k]; !ok {
				t.Errorf("%s: missing top-level %s", name, k)
			}
		}
	}
}
