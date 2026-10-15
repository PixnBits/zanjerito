package hamqtt

import (
	"bytes"
	"encoding/json"
	"strings"
)

const originURL = "https://github.com/PixnBits/zanjerito"

// Discovery builds one Home Assistant device-discovery payload.
// removed zone keys are published as platform-only entries so HA deletes them.
// There are no command topics.
func Discovery(version string, zoneIDs []string, removed []string) ([]byte, error) {
	cmps := make(map[string]map[string]any, 32+len(zoneIDs)+len(removed))

	add := func(key, platform, name, valTpl string, extra map[string]any) {
		m := map[string]any{
			"p":       platform,
			"name":    name,
			"uniq_id": uniqueIDPrefix + key,
			"stat_t":  TopicState,
			"val_tpl": valTpl,
		}
		for k, v := range extra {
			m[k] = v
		}
		cmps[key] = m
	}

	add("phase", "sensor", "Phase", fieldTpl("phase"), nil)
	add("pause_source", "sensor", "Pause source", noneIfEmpty("pause_source"), nil)
	add("paused_until", "sensor", "Paused until", noneIfNullUnknown("paused_until"), map[string]any{"dev_cla": "timestamp"})
	add("active_zone", "sensor", "Active zone", noneIfNull("active_zone"), nil)
	add("zones_on", "sensor", "Zones on", fieldTpl("zones_on"), nil)
	add("step_remaining", "sensor", "Step remaining", fieldTpl("step_remaining_sec"), map[string]any{"dev_cla": "duration", "unit_of_meas": "s"})
	add("run_remaining", "sensor", "Run remaining", fieldTpl("run_remaining_sec"), map[string]any{"dev_cla": "duration", "unit_of_meas": "s"})
	add("next_run", "sensor", "Next run", noneIfNullUnknown("next_run_at"), map[string]any{"dev_cla": "timestamp"})
	add("next_run_ends", "sensor", "Next run ends", noneIfNullUnknown("next_run_ends_at"), map[string]any{"dev_cla": "timestamp"})
	add("next_run_length", "sensor", "Next run length", fieldTpl("next_run_total_min"), map[string]any{"dev_cla": "duration", "unit_of_meas": "min"})
	add("rain_last_update", "sensor", "Rain last update", noneIfNullUnknown("rain_last_ok_at"), map[string]any{"dev_cla": "timestamp"})
	add("rain_24h", "sensor", "Rain 24h", fieldTpl("rain_24h_in"), map[string]any{"dev_cla": "precipitation", "unit_of_meas": "in"})
	add("rain_72h", "sensor", "Rain 72h", fieldTpl("rain_72h_in"), map[string]any{"dev_cla": "precipitation", "unit_of_meas": "in"})
	add("last_run_kind", "sensor", "Last run kind", noneIfNull("last_run_kind"), nil)
	add("last_run_outcome", "sensor", "Last run outcome", noneIfNull("last_run_outcome"), nil)
	add("last_run_started", "sensor", "Last run started", noneIfNullUnknown("last_run_started_at"), map[string]any{"dev_cla": "timestamp"})
	add("last_run_ended", "sensor", "Last run ended", noneIfNullUnknown("last_run_ended_at"), map[string]any{"dev_cla": "timestamp"})

	add("paused", "binary_sensor", "Paused", onOff("paused"), nil)
	add("lockout", "binary_sensor", "Lockout", onOff("lockout"), map[string]any{"dev_cla": "problem"})
	add("error", "binary_sensor", "Error", onOff("has_error"), map[string]any{"dev_cla": "problem"})
	add("next_run_skipped_by_pause", "binary_sensor", "Next run skipped by pause", onOff("next_run_skipped_by_pause"), nil)
	add("rain_feed_enabled", "binary_sensor", "Rain feed enabled", onOff("rain_enabled"), nil)
	add("rain_unavailable", "binary_sensor", "Rain feed unavailable", onOff("rain_unavailable"), map[string]any{"dev_cla": "problem"})

	for _, id := range zoneIDs {
		add(id, "binary_sensor", zoneName(id), zoneOnOff(id), map[string]any{"dev_cla": "running"})
	}
	for _, id := range removed {
		cmps[id] = map[string]any{"p": "binary_sensor"}
	}

	payload := map[string]any{
		"dev": map[string]any{
			"ids":  []string{"zanjerito"},
			"name": "Irrigation",
			"mf":   "PixnBits",
			"mdl":  "Zanjerito",
			"sw":   version,
		},
		"o": map[string]any{
			"name": "zanjerito-mqtt",
			"sw":   version,
			"url":  originURL,
		},
		"avty_t": TopicAvailability,
		"stat_t": TopicState,
		"qos":    1,
		"cmps":   cmps,
	}
	return marshalJSON(payload)
}

func zoneName(id string) string {
	return "Zone " + strings.TrimPrefix(id, "zone_")
}

func fieldTpl(field string) string {
	return "{{ value_json." + field + " }}"
}

func noneIfEmpty(field string) string {
	return "{{ value_json." + field + " if value_json." + field + " else 'none' }}"
}

func noneIfNull(field string) string {
	return "{{ value_json." + field + " if value_json." + field + " is not none else 'none' }}"
}

// noneIfNullUnknown renders JSON null as the literal None so HA MQTT sensors
// treat the value as unknown (not the string "none").
func noneIfNullUnknown(field string) string {
	return "{{ value_json." + field + " if value_json." + field + " is not none else 'None' }}"
}

func onOff(field string) string {
	return "{{ 'ON' if value_json." + field + " else 'OFF' }}"
}

func zoneOnOff(id string) string {
	return "{{ 'ON' if (value_json.zones | selectattr('id','eq','" + id + "') | selectattr('on') | list | count) > 0 else 'OFF' }}"
}

func marshalJSON(v any) ([]byte, error) {
	var buf bytes.Buffer
	enc := json.NewEncoder(&buf)
	enc.SetEscapeHTML(false)
	if err := enc.Encode(v); err != nil {
		return nil, err
	}
	b := buf.Bytes()
	if n := len(b); n > 0 && b[n-1] == '\n' {
		b = b[:n-1]
	}
	return b, nil
}
