package hamqtt

import (
	"encoding/json"
	"strings"
	"testing"
)

func decodeDiscovery(t *testing.T, version string, zones, removed []string) map[string]any {
	t.Helper()
	raw, err := Discovery(version, zones, removed)
	if err != nil {
		t.Fatal(err)
	}
	var m map[string]any
	if err := json.Unmarshal(raw, &m); err != nil {
		t.Fatal(err)
	}
	return m
}

func cmpsOf(t *testing.T, root map[string]any) map[string]map[string]any {
	t.Helper()
	raw, ok := root["cmps"].(map[string]any)
	if !ok {
		t.Fatalf("cmps %T", root["cmps"])
	}
	out := make(map[string]map[string]any, len(raw))
	for k, v := range raw {
		cm, ok := v.(map[string]any)
		if !ok {
			t.Fatalf("cmps[%s] %T", k, v)
		}
		out[k] = cm
	}
	return out
}

func walkKeys(v any, fn func(k string)) {
	switch x := v.(type) {
	case map[string]any:
		for k, val := range x {
			fn(k)
			walkKeys(val, fn)
		}
	case []any:
		for _, val := range x {
			walkKeys(val, fn)
		}
	}
}

func TestDiscoveryShape(t *testing.T) {
	body := haFixture(t, "idle.json")
	zones, ok := ParseState(body)
	if !ok {
		t.Fatal("fixture ParseState")
	}
	const ver = "test-sw"
	removed := []string{"zone_9"}
	root := decodeDiscovery(t, ver, zones, removed)

	for _, k := range []string{"dev", "o", "cmps", "avty_t", "stat_t", "qos"} {
		if _, ok := root[k]; !ok {
			t.Fatalf("missing top-level %q", k)
		}
	}
	if root["avty_t"] != TopicAvailability {
		t.Fatalf("avty_t %v", root["avty_t"])
	}
	if root["stat_t"] != TopicState {
		t.Fatalf("stat_t %v", root["stat_t"])
	}
	if root["qos"] != float64(1) {
		t.Fatalf("qos %v", root["qos"])
	}

	dev, _ := root["dev"].(map[string]any)
	if dev["name"] != "Irrigation" || dev["mf"] != "PixnBits" || dev["mdl"] != "Zanjerito" || dev["sw"] != ver {
		t.Fatalf("dev %+v", dev)
	}
	ids, _ := dev["ids"].([]any)
	if len(ids) != 1 || ids[0] != "zanjerito" {
		t.Fatalf("dev.ids %v", ids)
	}
	o, _ := root["o"].(map[string]any)
	if o["name"] != "zanjerito-mqtt" || o["sw"] != ver || o["url"] != originURL {
		t.Fatalf("o %+v", o)
	}

	walkKeys(root, func(k string) {
		lk := strings.ToLower(k)
		if strings.Contains(lk, "cmd_t") || strings.Contains(lk, "command_topic") {
			t.Fatalf("command key %q", k)
		}
	})

	cmps := cmpsOf(t, root)
	for _, c := range cmps {
		if _, ok := c["p"]; !ok {
			t.Fatalf("component missing p: %+v", c)
		}
	}

	rem := cmps["zone_9"]
	if rem == nil {
		t.Fatal("missing removed zone_9")
	}
	if len(rem) != 1 || rem["p"] != "binary_sensor" {
		t.Fatalf("removed entry %+v", rem)
	}

	for _, z := range zones {
		c := cmps[z]
		if c == nil {
			t.Fatalf("missing zone %s", z)
		}
		uid, _ := c["uniq_id"].(string)
		if uid != uniqueIDPrefix+z {
			t.Fatalf("zone %s uniq_id %q", z, uid)
		}
		if c["p"] != "binary_sensor" || c["dev_cla"] != "running" {
			t.Fatalf("zone %s %+v", z, c)
		}
		name, _ := c["name"].(string)
		if name != zoneName(z) {
			t.Fatalf("zone %s name %q", z, name)
		}
		tpl, _ := c["val_tpl"].(string)
		if !strings.Contains(tpl, "selectattr('id','eq','"+z+"')") {
			t.Fatalf("zone %s val_tpl %q", z, tpl)
		}
	}

	type spec struct {
		platform, name, field, class, unit string
		none, unknownNone                  bool
	}
	want := map[string]spec{
		"phase":                     {platform: "sensor", name: "Phase", field: "phase"},
		"pause_source":              {platform: "sensor", name: "Pause source", field: "pause_source", none: true},
		"paused_until":              {platform: "sensor", name: "Paused until", field: "paused_until", class: "timestamp", unknownNone: true},
		"active_zone":               {platform: "sensor", name: "Active zone", field: "active_zone", none: true},
		"zones_on":                  {platform: "sensor", name: "Zones on", field: "zones_on"},
		"step_remaining":            {platform: "sensor", name: "Step remaining", field: "step_remaining_sec", class: "duration", unit: "s"},
		"run_remaining":             {platform: "sensor", name: "Run remaining", field: "run_remaining_sec", class: "duration", unit: "s"},
		"next_run":                  {platform: "sensor", name: "Next run", field: "next_run_at", class: "timestamp", unknownNone: true},
		"next_run_ends":             {platform: "sensor", name: "Next run ends", field: "next_run_ends_at", class: "timestamp", unknownNone: true},
		"next_run_length":           {platform: "sensor", name: "Next run length", field: "next_run_total_min", class: "duration", unit: "min"},
		"rain_last_update":          {platform: "sensor", name: "Rain last update", field: "rain_last_ok_at", class: "timestamp", unknownNone: true},
		"rain_24h":                  {platform: "sensor", name: "Rain 24h", field: "rain_24h_in", class: "precipitation", unit: "in"},
		"rain_72h":                  {platform: "sensor", name: "Rain 72h", field: "rain_72h_in", class: "precipitation", unit: "in"},
		"last_run_kind":             {platform: "sensor", name: "Last run kind", field: "last_run_kind", none: true},
		"last_run_outcome":          {platform: "sensor", name: "Last run outcome", field: "last_run_outcome", none: true},
		"last_run_started":          {platform: "sensor", name: "Last run started", field: "last_run_started_at", class: "timestamp", unknownNone: true},
		"last_run_ended":            {platform: "sensor", name: "Last run ended", field: "last_run_ended_at", class: "timestamp", unknownNone: true},
		"paused":                    {platform: "binary_sensor", name: "Paused", field: "paused"},
		"lockout":                   {platform: "binary_sensor", name: "Lockout", field: "lockout", class: "problem"},
		"error":                     {platform: "binary_sensor", name: "Error", field: "has_error", class: "problem"},
		"next_run_skipped_by_pause": {platform: "binary_sensor", name: "Next run skipped by pause", field: "next_run_skipped_by_pause"},
		"rain_feed_enabled":         {platform: "binary_sensor", name: "Rain feed enabled", field: "rain_enabled"},
		"rain_unavailable":          {platform: "binary_sensor", name: "Rain feed unavailable", field: "rain_unavailable", class: "problem"},
	}
	for key, sp := range want {
		c := cmps[key]
		if c == nil {
			t.Fatalf("missing cmps[%s]", key)
		}
		uid, _ := c["uniq_id"].(string)
		if uid != uniqueIDPrefix+key {
			t.Fatalf("%s uniq_id %q", key, uid)
		}
		if !strings.HasPrefix(uid, uniqueIDPrefix) {
			t.Fatalf("%s uniq_id prefix %q", key, uid)
		}
		if c["p"] != sp.platform {
			t.Fatalf("%s p %v", key, c["p"])
		}
		if c["name"] != sp.name {
			t.Fatalf("%s name %v want %q", key, c["name"], sp.name)
		}
		if c["stat_t"] != TopicState {
			t.Fatalf("%s stat_t %v", key, c["stat_t"])
		}
		tpl, _ := c["val_tpl"].(string)
		if !strings.Contains(tpl, "value_json."+sp.field) {
			t.Fatalf("%s val_tpl %q missing field %s", key, tpl, sp.field)
		}
		if sp.platform == "binary_sensor" && !strings.Contains(tpl, "'ON'") {
			t.Fatalf("%s binary val_tpl %q", key, tpl)
		}
		if sp.none && !strings.Contains(tpl, "'none'") {
			t.Fatalf("%s want 'none' in %q", key, tpl)
		}
		if sp.unknownNone {
			if !strings.Contains(tpl, "'None'") {
				t.Fatalf("%s want 'None' in %q", key, tpl)
			}
			if !strings.Contains(tpl, "is not none") {
				t.Fatalf("%s want null guard in %q", key, tpl)
			}
		}
		if sp.class != "" && c["dev_cla"] != sp.class {
			t.Fatalf("%s dev_cla %v want %s", key, c["dev_cla"], sp.class)
		}
		if sp.unit != "" && c["unit_of_meas"] != sp.unit {
			t.Fatalf("%s unit_of_meas %v want %s", key, c["unit_of_meas"], sp.unit)
		}
	}

	for k, c := range cmps {
		if k == "zone_9" {
			continue
		}
		uid, _ := c["uniq_id"].(string)
		if !strings.HasPrefix(uid, uniqueIDPrefix) {
			t.Fatalf("%s uniq_id %q", k, uid)
		}
	}
}

func TestDiscoveryPauseSourceEmptyVsNull(t *testing.T) {
	root := decodeDiscovery(t, "v", nil, nil)
	cmps := cmpsOf(t, root)
	ps, _ := cmps["pause_source"]["val_tpl"].(string)
	if strings.Contains(ps, "is not none") {
		t.Fatalf("pause_source should treat empty string as none, got %q", ps)
	}
	az, _ := cmps["active_zone"]["val_tpl"].(string)
	if !strings.Contains(az, "is not none") {
		t.Fatalf("active_zone should guard JSON null, got %q", az)
	}
}
