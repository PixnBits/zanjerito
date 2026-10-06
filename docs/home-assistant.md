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

`rain_24h_in` and `rain_72h_in` are `0` when rain is disabled or the totals are not known. `has_error` is true when the engine has a last error; the text is never included. `lockout` is always `false` today, matching `/api/kiosk`.

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
| `last_run_kind` | string or null | newest history row's kind; null if there is no history |
| `last_run_outcome` | string or null | `completed`, `stopped`, `skipped`, `refused`, or `error`; null if none |
| `last_run_started_at` | string or null | when that run started |
| `last_run_ended_at` | string or null | when that run ended |
| `zones` | array | `{"id":"zone_N","on":bool}` for every configured station, config order |

Calling this route does not start or stop a run, set or clear a pause, or rewrite config, history, or `pause.json`. An expired timed pause is reported as not paused and is left for the status route or the scheduler to clear.

## Example

Replace the host and port. `scan_interval: 30` is seconds. Null fields (`active_zone`, `next_run_at`, `last_run_outcome`, and the other nullable times) are JSON `null` when they do not apply; the next-run sensor stays unavailable in that case.

```yaml
rest:
  - resource: http://<controller-host>:<port>/api/ha
    scan_interval: 30
    sensor:
      - name: Irrigation phase
        value_template: "{{ value_json.phase }}"
      - name: Irrigation active zone
        value_template: "{{ value_json.active_zone }}"
      - name: Irrigation run remaining
        value_template: "{{ value_json.run_remaining_sec }}"
        unit_of_measurement: s
      - name: Irrigation next run
        device_class: timestamp
        availability: "{{ value_json.next_run_at is not none }}"
        value_template: "{{ value_json.next_run_at }}"
      - name: Irrigation last run outcome
        value_template: "{{ value_json.last_run_outcome }}"
    binary_sensor:
      - name: Irrigation paused
        value_template: "{{ value_json.paused }}"
      - name: Irrigation rain unavailable
        value_template: "{{ value_json.rain_unavailable }}"
      - name: Irrigation has error
        value_template: "{{ value_json.has_error }}"
```
