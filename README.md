# zanjerito
A little irrigation controller.

> Zanjero is Spanish for "ditch rider." Since the late 1800s, zanjeros have played a vital role in the control and flow of water in the Valley. They traveled hundreds of miles along canals (first by horse, then by truck) and opened head gates to release water from the major canals into smaller canals and pipes that deliver the water that eventually comes out of our faucets and grows our food.

https://www.srpnet.com/water/canals/azfallstour/Zanjero.aspx

I had a name-brand commercial drip irrigation system controller, but first the WiFi system stopped working and then it stopped turning on valves. Raspberry Pis are easy to switch out, and Open-Source Software is great for fixing usability issues. Here's an attempt to do it "right".

## Production (v2 Go on the Pi)

**Live actuator** is the Go binary on the Pi with `DRIVER=gpiocdev` (cutover ~2026-09-22). Front bash cron is commented; bash scripts stay on disk for rollback.

- [docs/deploy.md](docs/deploy.md) — build, install, systemd, env
- [docs/cutover.md](docs/cutover.md) — completed flip notes + bash rollback copy-paste
- [docs/prd.md](docs/prd.md) — product vision / PRD
- [docs/decisions.md](docs/decisions.md) — prioritized decisions
- [docs/architecture.md](docs/architecture.md) — runtime layers, state machine, API sketch
- [docs/pin-map.md](docs/pin-map.md) — this hardware’s pin table + example config
- [docs/ui-directions.md](docs/ui-directions.md) — UI directions (Direction D selected) + v1 explore
- [docs/impl-plan.md](docs/impl-plan.md) — scoped PR implementation plan

### Go binary + systemd

See [docs/deploy.md](docs/deploy.md). Lean path is `/opt/zanjerito`.

```sh
make build          # static; arch from uname -m (or GOARCH=arm64)
sudo make install   # binary + copy-once config/env + unit
sudo systemctl enable --now zanjerito
# phone: http://<pi-lan>:8080/   (set LISTEN in /opt/zanjerito/zanjerito.env)
```

`systemctl stop` sends SIGTERM; the binary `engine.Stop()`s (all-off) before exit. With `DRIVER=gpiocdev`, that de-energizes valves (inactive-on-release).

## Developing

Primary path is the Go binary (same as production):

```sh
git clone https://github.com/PixnBits/zanjerito.git
cd zanjerito
make build
./zanjerito -config config/config.example.json -driver=fake -listen 127.0.0.1:8080
# phone UI: http://127.0.0.1:8080/
```

The older Node 14 / GraphiQL app under this tree (`server/`, `client/`) is a **behavioral reference** only (not the production UI or Developing default). Prefer the embedded Direction D UI served by the Go binary.

## Run history

Each finished, stopped, skipped (paused), refused (lockout), or failed run is appended to `history.json` beside the config file (same directory as `config.json` and `pause.json`). Outcomes are `completed`, `stopped`, `skipped`, `refused`, and `error`. The log keeps the newest 200 entries from the last 60 days. Home shows the latest few; `GET /api/history?limit=N` returns newest-first (`limit` defaults to 50 and caps at 200).

## Automatic rain pause

Zanjerito can hold watering when a Flood Control District of Maricopa County (FCDMC) ALERT rain gauge reports enough rain. The gauge id stays in a private file that is gitignored.

Copy `config/rain.local.example.json` to `rain.local.json` in the same directory as your config file (next to `config.json`). Replace `<GAUGE_ID>` with your nearest FCDMC ALERT precipitation gauge id. Do not commit `rain.local.json`.

`ZANJERITO_RAIN_CONFIG` overrides that path. If the file is missing, or `enabled` is false, rain pause does nothing: no polling and no Home hint. A malformed file is logged and the feature stays off.

The controller POSTs the form body (`ID1={gauge}&ST=rain&NM=200` by default; `{gauge}` is the id, `NM` is how many samples to ask for). Reports are read as America/Phoenix wall time. Defaults:

| Field | Default | Meaning |
|---|---|---|
| `poll_minutes` | 30 | How often to fetch. Set `poll_seconds` to override (for example `2`). |
| `trigger_inches` | 0.25 | Pause when the last `window_hours` add up to at least this. |
| `window_hours` | 24 | Rolling total of incremental rainfall. |
| `dry_days` | 2 | Hold until this many days after the newest rain in the window. Values above 14 are capped to 14. |
| `heavy_inches` | 1.0 | At or above this total, use `heavy_dry_days` instead. |
| `heavy_dry_days` | 4 | Longer hold after a heavier total. Values above 14 are capped to 14. |
| `stale_hours` | 7 | Newest sample older than this is ignored. |
| `max_increment_inches` | 2.0 | One in-window increment above this is implausible. Missing or ≤ 0 uses 2.0. |
| `max_window_inches` | 6.0 | An in-window total above this is implausible, even when every increment is under the cap. Missing or ≤ 0 uses 6.0. |

Gauges send a 0.00 reading about every 6 hours when it is dry, so a stale window of a few hours would mark healthy data unavailable. The default is 7 hours and is configurable. On load, `dry_days` and `heavy_dry_days` above 14 are capped to 14 and one line is logged. Bad, stale, or implausible data never pauses, never extends, and never clears a pause that is already set. A value sitting exactly on either cap still counts.

Samples more than 15 minutes ahead of the controller clock are ignored. They are not added to the window and they do not make the feed look fresh. Staleness uses the newest sample that is not that far ahead. If every sample is that far ahead, the feed is unavailable. A sample up to 15 minutes ahead is kept.

A manual pause (any reason, including an indefinite one) is left alone: automatic rain will not override it or shorten it. Resume remembers the time so the same rain does not immediately pause again. Later rain that crosses the threshold in the rolling window can.

A short manual pause can end while qualifying rain is still inside the rolling window (24 hours by default). If an automatic pause already covered that rain and the user replaced it or pressed Resume, the event is remembered and does not pause again; that memory is still there after a restart. If the rain fell while a manual pause was already active, so this rain never became an automatic pause, the next poll after the manual pause ends starts the automatic pause. A restart in between restores the manual pause, and the poll after that pause ends can still start the automatic one.

While an automatic rain pause is active, schedules water only stations with `rain_pause_exempt: true` and skip the rest (recorded as skipped, reason `rain`). The example drip station is exempt. Home still allows a manual run of an exempt station (for example drip). Any other station is refused with HTTP 409 until Resume. A manual pause still refuses every station, including exempt ones. Home shows the automatic pause on the same banner as a manual pause, for example "Paused for rain (0.4 in) until Tue morning". A small "Rain data unavailable" line appears on Home only when the feature is on and the latest fetch failed, is stale, or is implausible. Status JSON adds `pause_source`, `rain_inches`, `last_rain_at`, and a `rain` object. It does not include the gauge id or the gauge URL.

## Soil water estimate (display only)

Home can show a per-zone soil-water **estimate**. It never starts, stops, skips, or pauses watering. Rain pause, lockout, STOP, the scheduler, and run history behave exactly as they did without this feature.

The model is a capped daily bucket. For each zone and each local day `d` in a rolling window (oldest to newest, including today):

```text
balance(d) = max(0, min(capacity, balance(d-1) + rain(d) + watering(d) - ETo(d) * crop_factor))
```

`balance` before the first modelled day is **capacity** (the spin-up assumption: the soil is treated as full at the start of the oldest modelled day). Until that window has filled with real rain, ET, and watering, the number is a starting guess, not a measurement. Home does not show that guess as a percent until ET for the window is known.

- **ETo** is daily reference ET from AZMET (`eto_azmet` millimetres ÷ 25.4; `eto_azmet_in` if millimetres are missing). Today's row is often unpublished; the last earlier good day is carried forward. A day with no earlier ET is treated as 0 ET and marked unknown. A daily ETo below 0 or above `max_daily_et_inches` rejects that fetch; the previous good cache is kept. Until a fetch has succeeded and at least one ET day falls in the window, `percent` and `balance_inches` are null.
- **Rain** reuses the last good FCDMC gauge fetch (no second gauge request). If rain pause is off or has no good data, rain is 0 and marked unknown.
- **Watering** sums `completed` and `stopped` history `ActualSec` for that station on the local `StartedAt` date: `ActualSec / 3600 * inches_per_hour`. `skipped`, `refused`, and `error` do not add water. If `inches_per_hour` is null, watering is omitted. The card then shows that zone's rain and crop-ET totals for the modelled window (for example "Rain 0.40 in · ET (plant-adjusted) 1.68 in, last 14 days", using `window_days`) and "Measure sprinkler output to enable".

Copy `config/soil.local.example.json` to `soil.local.json` in the same directory as your config file. Set `azmet_station` to your AZMET id (the example uses `azXX`). Do not commit `soil.local.json`. `ZANJERITO_SOIL_CONFIG` overrides that path. If the file is missing, or `enabled` is false, the estimate is off: no polling, `GET /api/soil` returns `enabled: false` with reason `no soil.local.json` and no `config_error`, and Home hides the card. If the file is invalid (malformed or empty JSON, wrong types, missing station, bad URL), the error is logged, polling stays off, and `GET /api/soil` returns `enabled: false`, reason `soil.local.json invalid`, and `config_error` with a short parse message. That message does not include the station id, the URL, or a filesystem path. Home shows "Soil settings file has an error (see log)".

Defaults (also the example file):

| Field | Default | Meaning |
|---|---|---|
| `crop_factor` | 0.6 | Crop coefficient. Generic starting point; adjust per zone. Valid `(0, 1.5]`. |
| `capacity_inches` | 1.0 | Bucket size in inches. Generic starting point; adjust per zone. Valid `(0, 12]`. |
| `window_days` | 14 | Local days modelled, ending today (oldest = today − `window_days` + 1). Fetch asks for `window_days+1` days starting `today - window_days` so one extra day is available for spin-up. |
| `max_daily_et_inches` | 0.6 | Daily ETo above this (or below 0) is implausible. |
| `poll_hours` | 6 | How often to fetch. Set `poll_seconds` to override in tests. |
| `timeout_seconds` | 15 | HTTP client timeout. |
| `inches_per_hour` | `null` | Sprinkler output. Null/absent is unknown — never guessed. Valid `(0, 10]`. |

`GET /api/soil` is computed from the in-memory ET cache, the rain poller's last good samples, and `history.List`. The handler does not call AZMET. The AZMET station id is not in API output or info logs. The phone UI only requests `/api/soil` (same origin).

`et_known` is true only when `et.last_ok_at` is set and at least one ET day in the window is known. Otherwise it is false. `et_reason` is `waiting for first ET fetch` while that first attempt has not finished, and `no ET data yet` after a failed or empty fetch (optionally with a short class such as AZMET unavailable). While `et_known` is false, each zone's `percent` and `balance_inches` are JSON null. Per-zone rain and ET inputs are still present, and rain totals may still be shown. Home shows one line, "No ET data yet", and no percent or bar, plus "ET unavailable since …" when the feed is down. The run dialog says "Soil estimate: no ET data yet". `updated_at` is the last successful ET fetch, the same instant as `et.last_ok_at`, or null if there has never been one. It is not the current time and not the time of a failed attempt.

`window_days` is the configured window. Each zone has `rain_total_inches` and `et_total_inches`: rain and crop ET summed over the modelled local days (exactly `window_days` dates ending today).

### Tuna-can test (application rate)

Until you measure a zone, leave `inches_per_hour` null.

1. Place several straight-sided cans (tuna cans work) around the zone.
2. Run that station for 15 minutes.
3. Measure the average depth in the cans, in inches.
4. `inches_per_hour = depth * 4`.

Example zone entry after a 0.20 in catch in 15 minutes:

```json
"front-north": { "inches_per_hour": 0.8, "crop_factor": 0.6, "capacity_inches": 1.0 }
```
