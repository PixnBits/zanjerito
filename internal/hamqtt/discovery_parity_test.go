package hamqtt

import (
	"sort"
	"strings"
	"testing"
)

// Identity of Discovery() at f62588b18e63d2d1659c48a704201f211d533d0b.
// Home Assistant entities duplicate if a unique_id or topic moves.
// The only payload addition allowed after that commit is stat_cla
// "measurement" on zones_on, rain_24h, and rain_72h.
const (
	f62588bDiscoveryTopic    = "homeassistant/device/zanjerito/config"
	f62588bStateTopic        = "zanjerito/state"
	f62588bAvailabilityTopic = "zanjerito/availability"
	f62588bHAStatusTopic     = "homeassistant/status"
	f62588bUniqueIDPrefix    = "zanjerito_mqtt_"
)

// Fixed component keys from f62588b Discovery(), excluding zone_N inputs.
var f62588bComponentKeys = []string{
	"phase",
	"pause_source",
	"paused_until",
	"active_zone",
	"zones_on",
	"step_remaining",
	"run_remaining",
	"next_run",
	"next_run_ends",
	"next_run_length",
	"rain_last_update",
	"rain_24h",
	"rain_72h",
	"last_run_kind",
	"last_run_outcome",
	"last_run_started",
	"last_run_ended",
	"paused",
	"lockout",
	"error",
	"next_run_skipped_by_pause",
	"rain_feed_enabled",
	"rain_unavailable",
}

func TestDiscoveryParityWithF62588B(t *testing.T) {
	if TopicDiscovery != f62588bDiscoveryTopic ||
		TopicState != f62588bStateTopic ||
		TopicAvailability != f62588bAvailabilityTopic ||
		TopicHAStatus != f62588bHAStatusTopic ||
		uniqueIDPrefix != f62588bUniqueIDPrefix {
		t.Fatalf("topic or unique_id constants drifted from f62588b: discovery %q state %q availability %q ha %q prefix %q",
			TopicDiscovery, TopicState, TopicAvailability, TopicHAStatus, uniqueIDPrefix)
	}

	zones := []string{"zone_1", "zone_3"}
	removed := []string{"zone_9"}
	root := decodeDiscovery(t, "parity", zones, removed)
	cmps := cmpsOf(t, root)

	wantKeys := append(append([]string{}, f62588bComponentKeys...), zones...)
	wantKeys = append(wantKeys, removed...)
	if got, want := sortedKeys(cmps), sortedCopy(wantKeys); strings.Join(got, ",") != strings.Join(want, ",") {
		t.Fatalf("component keys\n got %v\nwant %v", got, want)
	}

	if root["avty_t"] != f62588bAvailabilityTopic {
		t.Fatalf("device avty_t %v", root["avty_t"])
	}
	if root["stat_t"] != f62588bStateTopic {
		t.Fatalf("device stat_t %v", root["stat_t"])
	}

	live := append(append([]string{}, f62588bComponentKeys...), zones...)
	var gotIDs []string
	for _, key := range live {
		c := cmps[key]
		uid, _ := c["uniq_id"].(string)
		if uid != f62588bUniqueIDPrefix+key {
			t.Fatalf("%s uniq_id %q", key, uid)
		}
		if c["stat_t"] != f62588bStateTopic {
			t.Fatalf("%s stat_t %v", key, c["stat_t"])
		}
		gotIDs = append(gotIDs, uid)
	}
	sort.Strings(gotIDs)
	var wantIDs []string
	for _, key := range live {
		wantIDs = append(wantIDs, f62588bUniqueIDPrefix+key)
	}
	sort.Strings(wantIDs)
	if strings.Join(gotIDs, ",") != strings.Join(wantIDs, ",") {
		t.Fatalf("unique_ids\n got %v\nwant %v", gotIDs, wantIDs)
	}

	for _, key := range removed {
		c := cmps[key]
		if _, ok := c["uniq_id"]; ok {
			t.Fatalf("removed %s uniq_id %v", key, c["uniq_id"])
		}
		if _, ok := c["stat_t"]; ok {
			t.Fatalf("removed %s stat_t %v", key, c["stat_t"])
		}
		if len(c) != 1 || c["p"] != "binary_sensor" {
			t.Fatalf("removed %s payload %+v", key, c)
		}
	}

	walkPairs(root, func(k string, v any) {
		if topicField(k) && k != "avty_t" && k != "stat_t" {
			t.Fatalf("new topic field %q", k)
		}
		s, ok := v.(string)
		if !ok {
			return
		}
		if strings.HasPrefix(s, "homeassistant/") || strings.HasPrefix(s, "zanjerito/") {
			if s != f62588bStateTopic && s != f62588bAvailabilityTopic {
				t.Fatalf("unexpected topic value %q at %s", s, k)
			}
		}
	})

	// stat_cla is not a topic. It may exist only on the three measurement sensors.
	for key, c := range cmps {
		sc, ok := c["stat_cla"]
		switch key {
		case "zones_on", "rain_24h", "rain_72h":
			if ok && sc != "measurement" {
				t.Fatalf("%s stat_cla %v", key, sc)
			}
		default:
			if ok {
				t.Fatalf("%s unexpected stat_cla %v", key, sc)
			}
		}
	}
}

func topicField(k string) bool {
	lk := strings.ToLower(k)
	return strings.Contains(lk, "topic") || strings.HasSuffix(lk, "_t")
}

func sortedKeys(m map[string]map[string]any) []string {
	out := make([]string, 0, len(m))
	for k := range m {
		out = append(out, k)
	}
	sort.Strings(out)
	return out
}

func sortedCopy(in []string) []string {
	out := append([]string{}, in...)
	sort.Strings(out)
	return out
}

func walkPairs(v any, fn func(k string, val any)) {
	switch x := v.(type) {
	case map[string]any:
		for k, val := range x {
			fn(k, val)
			walkPairs(val, fn)
		}
	case []any:
		for _, val := range x {
			walkPairs(val, fn)
		}
	}
}
