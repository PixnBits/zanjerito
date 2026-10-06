# Home Assistant (read-only)

`GET /api/ha` is a flat snapshot for Home Assistant to poll. It reports phase, pause, the zone that is on, how long the current run has left, the next scheduled fire, rain totals, and the newest history row. It does not include station titles, colours, station ids, schedule or program names, soil, or error text.

The route is read-only and unauthenticated. Keep the controller's HTTP port on a trusted LAN. Do not publish it on the public internet.

The daemon never dials Home Assistant. If Home Assistant is down, slow, or not installed, schedules and valves are unchanged. Home Assistant polls; this process has no dependency on it.

Phase 2 is not this endpoint. Stop, pause, manual run, and authentication stay out of scope until a later change. Watering writes remain on their existing routes.

## Request

`GET http://<controller-host>:<port>/api/ha`

Any other method is HTTP 405. A config that cannot be read is HTTP 500, same as `GET /api/kiosk`. Success is one JSON object, `Content-Type: application/json`, `Cache-Control: no-store`.

Times are RFC3339 in the config timezone, the same clock as `/api/status` and `/api/kiosk`. `null` means the value does not apply.

Zone ids are `zone_N`, 1-based, in config order. `zone_1` is the first station in the config file. `active_zone` uses that map. The response has no schedule id or name. `next_run_*` is the same fire `/api/kiosk` reports as `next_run` (the next start, including one the pause would skip).

Reordering stations in the config renumbers the zones (`zone_N` follows config order), so Home Assistant entities follow the new order.

`rain_24h_in` and `rain_72h_in` are `0` when rain is disabled or the totals are not known. `has_error` is true when the engine has a last error; the text is never included. `lockout` is always `false` today, matching `/api/kiosk`.

`last_run_kind` is `schedule` or `manual`. `last_run_outcome` is `completed`, `stopped`, `skipped`, `refused`, or `error`. Empty or unrecognised values report `unknown`. Both are null when there is no history.

| Field | JSON | Meaning |
|---|---|---|
| `phase` | string | `Idle`, `PowerUp`, `StationOn`, `Overlap`, `PowerDown`, or `Fault` |
| `paused` | bool | watering is held |
| `pause_source` | string | `manual`, `auto`, or empty when not paused |
| `paused_until` | string or null | when the hold ends; null if not paused or the hold is indefinite |
| `lockout` | bool | always `false` today |
| `has_error` | bool | engine last error is non-empty; text is omitted |
| `active_zone` | string or null | `zone_N` of the station that is on, or null when none is |
| `zones_on` | number | how many configured stations are on |
| `step_remaining_sec` | number | seconds left on the current step; `0` when idle |
| `run_remaining_sec` | number | seconds left in the whole run; `0` when idle |
| `next_run_at` | string or null | next scheduled start |
| `next_run_ends_at` | string or null | when that fire would finish |
| `next_run_total_min` | number | planned minutes; `0` when there is no next fire |
| `next_run_skipped_by_pause` | bool | the active pause would skip that fire |
| `rain_enabled` | bool | a rain feed is configured |
| `rain_unavailable` | bool | the feed is on but not usable |
| `rain_last_ok_at` | string or null | last good rain fetch |
| `rain_24h_in` | number | inches over the last 24 hours |
| `rain_72h_in` | number | inches over the last 72 hours |
| `last_run_kind` | string or null | `schedule`, `manual`, or `unknown`; null if there is no history |
| `last_run_outcome` | string or null | `completed`, `stopped`, `skipped`, `refused`, `error`, or `unknown`; null if there is no history |
| `last_run_started_at` | string or null | when that run started |
| `last_run_ended_at` | string or null | when that run ended |
| `zones` | array | `{"id":"zone_N","on":bool}` for every configured station, config order |

Calling this route does not start or stop a run, set or clear a pause, or rewrite config, history, or `pause.json`. An expired timed pause is reported as not paused and is left for the status route or the scheduler to clear.

## Home Assistant setup

This is a ready-to-paste RESTful config. One `rest:` resource polls `GET /api/ha` every 30 seconds (one HTTP request feeds all entities), timeout 10 seconds. Every field in the response maps to one entity; `zones` maps to one `binary_sensor` per zone (`zone_1`..`zone_4`). The host `http://zanjerito.local:8080` is a placeholder: replace it with the controller's host and LISTEN port; if `zanjerito.local` does not resolve (mDNS is not always available to Home Assistant, for example in some Docker/VM setups) use the controller's LAN address, `http://<pi-ip>:8080/api/ha`.

### Config

The `template:` block at the end is optional; it adds a connectivity sensor that turns off (rather than unavailable) while the controller cannot be reached or does not return JSON.

```yaml
rest:
  - resource: http://zanjerito.local:8080/api/ha
    method: GET
    scan_interval: 30
    timeout: 10
    sensor:
      - name: Irrigation phase
        unique_id: zanjerito_phase
        availability: "{{ value_json is defined }}"
        icon: mdi:sprinkler-variant
        value_template: "{{ value_json.get('phase') or 'unknown' }}"
      - name: Irrigation pause source
        unique_id: zanjerito_pause_source
        availability: "{{ value_json is defined }}"
        icon: mdi:pause-circle-outline
        value_template: "{{ value_json.get('pause_source') or 'none' }}"
      - name: Irrigation paused until
        unique_id: zanjerito_paused_until
        device_class: timestamp
        availability: "{{ value_json is defined and value_json.get('paused_until') is not none }}"
        value_template: "{{ value_json.get('paused_until') }}"
      - name: Irrigation active zone
        unique_id: zanjerito_active_zone
        availability: "{{ value_json is defined }}"
        icon: mdi:sprinkler
        value_template: "{{ value_json.get('active_zone') or 'none' }}"
      - name: Irrigation zones on
        unique_id: zanjerito_zones_on
        availability: "{{ value_json is defined }}"
        icon: mdi:counter
        state_class: measurement
        value_template: "{{ value_json.get('zones_on') or 0 }}"
      - name: Irrigation step remaining
        unique_id: zanjerito_step_remaining
        availability: "{{ value_json is defined }}"
        device_class: duration
        unit_of_measurement: s
        value_template: "{{ value_json.get('step_remaining_sec') or 0 }}"
      - name: Irrigation run remaining
        unique_id: zanjerito_run_remaining
        availability: "{{ value_json is defined }}"
        device_class: duration
        unit_of_measurement: s
        value_template: "{{ value_json.get('run_remaining_sec') or 0 }}"
      - name: Irrigation next run
        unique_id: zanjerito_next_run_at
        device_class: timestamp
        availability: "{{ value_json is defined and value_json.get('next_run_at') is not none }}"
        value_template: "{{ value_json.get('next_run_at') }}"
      - name: Irrigation next run ends
        unique_id: zanjerito_next_run_ends_at
        device_class: timestamp
        availability: "{{ value_json is defined and value_json.get('next_run_ends_at') is not none }}"
        value_template: "{{ value_json.get('next_run_ends_at') }}"
      - name: Irrigation next run length
        unique_id: zanjerito_next_run_total_min
        availability: "{{ value_json is defined }}"
        device_class: duration
        unit_of_measurement: min
        value_template: "{{ value_json.get('next_run_total_min') or 0 }}"
      - name: Irrigation rain last update
        unique_id: zanjerito_rain_last_ok_at
        device_class: timestamp
        availability: "{{ value_json is defined and value_json.get('rain_last_ok_at') is not none }}"
        value_template: "{{ value_json.get('rain_last_ok_at') }}"
      - name: Irrigation rain 24h
        unique_id: zanjerito_rain_24h
        availability: "{{ value_json is defined }}"
        device_class: precipitation
        unit_of_measurement: in
        state_class: measurement
        value_template: "{{ value_json.get('rain_24h_in') or 0 }}"
      - name: Irrigation rain 72h
        unique_id: zanjerito_rain_72h
        availability: "{{ value_json is defined }}"
        device_class: precipitation
        unit_of_measurement: in
        state_class: measurement
        value_template: "{{ value_json.get('rain_72h_in') or 0 }}"
      - name: Irrigation last run kind
        unique_id: zanjerito_last_run_kind
        availability: "{{ value_json is defined }}"
        icon: mdi:history
        value_template: "{{ value_json.get('last_run_kind') or 'none' }}"
      - name: Irrigation last run outcome
        unique_id: zanjerito_last_run_outcome
        availability: "{{ value_json is defined }}"
        icon: mdi:clipboard-check-outline
        value_template: "{{ value_json.get('last_run_outcome') or 'none' }}"
      - name: Irrigation last run started
        unique_id: zanjerito_last_run_started_at
        device_class: timestamp
        availability: "{{ value_json is defined and value_json.get('last_run_started_at') is not none }}"
        value_template: "{{ value_json.get('last_run_started_at') }}"
      - name: Irrigation last run ended
        unique_id: zanjerito_last_run_ended_at
        device_class: timestamp
        availability: "{{ value_json is defined and value_json.get('last_run_ended_at') is not none }}"
        value_template: "{{ value_json.get('last_run_ended_at') }}"
    binary_sensor:
      - name: Irrigation paused
        unique_id: zanjerito_paused
        availability: "{{ value_json is defined }}"
        icon: mdi:pause-circle
        value_template: "{{ value_json.get('paused') is sameas true }}"
      - name: Irrigation lockout
        unique_id: zanjerito_lockout
        availability: "{{ value_json is defined }}"
        device_class: problem
        value_template: "{{ value_json.get('lockout') is sameas true }}"
      - name: Irrigation error
        unique_id: zanjerito_has_error
        availability: "{{ value_json is defined }}"
        device_class: problem
        value_template: "{{ value_json.get('has_error') is sameas true }}"
      - name: Irrigation next run skipped by pause
        unique_id: zanjerito_next_run_skipped_by_pause
        availability: "{{ value_json is defined }}"
        icon: mdi:calendar-remove
        value_template: "{{ value_json.get('next_run_skipped_by_pause') is sameas true }}"
      - name: Irrigation rain feed enabled
        unique_id: zanjerito_rain_enabled
        availability: "{{ value_json is defined }}"
        icon: mdi:weather-rainy
        value_template: "{{ value_json.get('rain_enabled') is sameas true }}"
      - name: Irrigation rain feed unavailable
        unique_id: zanjerito_rain_unavailable
        availability: "{{ value_json is defined }}"
        device_class: problem
        value_template: "{{ value_json.get('rain_unavailable') is sameas true }}"
      - name: Irrigation zone 1
        unique_id: zanjerito_zone_1
        device_class: running
        availability: "{{ value_json is defined and (value_json.get('zones') or []) | selectattr('id', 'eq', 'zone_1') | list | count == 1 }}"
        value_template: "{{ (value_json.get('zones') or []) | selectattr('id', 'eq', 'zone_1') | selectattr('on') | list | count > 0 }}"
      - name: Irrigation zone 2
        unique_id: zanjerito_zone_2
        device_class: running
        availability: "{{ value_json is defined and (value_json.get('zones') or []) | selectattr('id', 'eq', 'zone_2') | list | count == 1 }}"
        value_template: "{{ (value_json.get('zones') or []) | selectattr('id', 'eq', 'zone_2') | selectattr('on') | list | count > 0 }}"
      - name: Irrigation zone 3
        unique_id: zanjerito_zone_3
        device_class: running
        availability: "{{ value_json is defined and (value_json.get('zones') or []) | selectattr('id', 'eq', 'zone_3') | list | count == 1 }}"
        value_template: "{{ (value_json.get('zones') or []) | selectattr('id', 'eq', 'zone_3') | selectattr('on') | list | count > 0 }}"
      - name: Irrigation zone 4
        unique_id: zanjerito_zone_4
        device_class: running
        availability: "{{ value_json is defined and (value_json.get('zones') or []) | selectattr('id', 'eq', 'zone_4') | list | count == 1 }}"
        value_template: "{{ (value_json.get('zones') or []) | selectattr('id', 'eq', 'zone_4') | selectattr('on') | list | count > 0 }}"

template:
  - binary_sensor:
      - name: Irrigation controller online
        unique_id: zanjerito_controller_online
        device_class: connectivity
        state: "{{ not is_state('sensor.irrigation_phase', 'unavailable') }}"
```

### Entities

Home Assistant derives entity ids from the names.

| Field | Entity | Notes |
|---|---|---|
| `phase` | `sensor.irrigation_phase` | text: `Idle`, `PowerUp`, `StationOn`, `Overlap`, `PowerDown`, `Fault` |
| `paused` | `binary_sensor.irrigation_paused` | |
| `pause_source` | `sensor.irrigation_pause_source` | `none` when not paused |
| `paused_until` | `sensor.irrigation_paused_until` | timestamp; unavailable when null |
| `lockout` | `binary_sensor.irrigation_lockout` | problem; always off today |
| `has_error` | `binary_sensor.irrigation_error` | problem; the error text is not exposed |
| `active_zone` | `sensor.irrigation_active_zone` | `none` when no zone is on |
| `zones_on` | `sensor.irrigation_zones_on` | |
| `step_remaining_sec` | `sensor.irrigation_step_remaining` | duration, s |
| `run_remaining_sec` | `sensor.irrigation_run_remaining` | duration, s |
| `next_run_at` | `sensor.irrigation_next_run` | timestamp; unavailable when null |
| `next_run_ends_at` | `sensor.irrigation_next_run_ends` | timestamp; unavailable when null |
| `next_run_total_min` | `sensor.irrigation_next_run_length` | duration, min |
| `next_run_skipped_by_pause` | `binary_sensor.irrigation_next_run_skipped_by_pause` | |
| `rain_enabled` | `binary_sensor.irrigation_rain_feed_enabled` | |
| `rain_unavailable` | `binary_sensor.irrigation_rain_feed_unavailable` | problem |
| `rain_last_ok_at` | `sensor.irrigation_rain_last_update` | timestamp; unavailable when null |
| `rain_24h_in` | `sensor.irrigation_rain_24h` | precipitation, in |
| `rain_72h_in` | `sensor.irrigation_rain_72h` | precipitation, in |
| `last_run_kind` | `sensor.irrigation_last_run_kind` | `none` with no history |
| `last_run_outcome` | `sensor.irrigation_last_run_outcome` | `none` with no history |
| `last_run_started_at` | `sensor.irrigation_last_run_started` | timestamp |
| `last_run_ended_at` | `sensor.irrigation_last_run_ended` | timestamp |
| `zones` | `binary_sensor.irrigation_zone_1` .. `binary_sensor.irrigation_zone_4` | running = that zone's valve is on; a `zone_N` the controller does not report is unavailable |
| (none) | `binary_sensor.irrigation_controller_online` | optional template, connectivity |

If an entity id already exists, Home Assistant appends a suffix such as `_2`; the `unique_id`s let you rename entities in the UI.

### Null, missing, and unreachable

- Controller unreachable: when the fetch itself fails (timeout, connection refused, DNS failure), the RESTful integration marks every entity from the resource unavailable on its own, and they recover on the next successful poll.
- HTTP error reply: Home Assistant does not treat a reply such as the 500 above as a failed fetch; it hands the body to the templates. So every entity has `availability: "{{ value_json is defined }}"`: a reply that is not JSON makes the entity unavailable instead of showing stale or default values and logging template errors.
- Null fields: nullable times add `and value_json.get(...) is not none` to `availability`, so they are unavailable instead of logging an invalid timestamp. Nullable text shows `none`. Numbers fall back to `0` and booleans to off. A `zone_N` the controller does not report is unavailable.
- Missing fields: templates read fields with `value_json.get(...)`, so a field missing from an older or newer controller does not raise template errors.
- `binary_sensor.irrigation_controller_online` is `off` whenever the entities are unavailable for either reason; use it for automations or alerts on connectivity.
- Home Assistant reads the controller only; nothing in this config can start, stop, or pause watering.

### Install

1. Paste the block into `configuration.yaml`. If it already has a top-level `rest:` or `template:` key, add these list items under the existing key instead of adding a second key. Alternatively use a package: add the following to `configuration.yaml` and save the block as `packages/zanjerito.yaml`.

   ```yaml
   homeassistant:
     packages: !include_dir_named packages
   ```

2. Replace `zanjerito.local:8080` with your controller's host and port.
3. Developer tools > YAML > Check configuration. Fix anything it reports.
4. Restart Home Assistant (Settings > System > Restart). A full restart is needed the first time the `rest:` integration is added; afterwards Developer tools > YAML > "RESTful entities" reloads changes.
5. Check Settings > Devices & services > Entities, search "irrigation", and confirm the states match `curl http://zanjerito.local:8080/api/ha`.
6. Add the entities to a dashboard: edit a dashboard, Add card > Manual, paste the example card below.

### Example dashboard card

```yaml
type: entities
title: Irrigation
state_color: true
entities:
  - entity: binary_sensor.irrigation_controller_online
    name: Controller
  - entity: sensor.irrigation_phase
    name: Phase
  - entity: sensor.irrigation_active_zone
    name: Active zone
  - entity: sensor.irrigation_run_remaining
    name: Run remaining
  - entity: sensor.irrigation_next_run
    name: Next run
    format: relative
  - entity: binary_sensor.irrigation_paused
    name: Paused
  - entity: sensor.irrigation_paused_until
    name: Paused until
  - entity: binary_sensor.irrigation_error
    name: Error
  - entity: sensor.irrigation_rain_72h
    name: Rain (72 h)
  - entity: sensor.irrigation_last_run_outcome
    name: Last run
  - type: section
    label: Zones
  - binary_sensor.irrigation_zone_1
  - binary_sensor.irrigation_zone_2
  - binary_sensor.irrigation_zone_3
  - binary_sensor.irrigation_zone_4
```

Rename zones in the UI (entity settings) rather than in this file; `zone_N` follows config order (see above).
