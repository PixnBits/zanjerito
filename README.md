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
- [docs/home-assistant.md](docs/home-assistant.md) — read-only `GET /api/ha` for Home Assistant, with a ready-to-paste RESTful sensor config
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

If the LAN address is not assigned yet at boot (for example `LISTEN=192.0.2.10:8080` while wlan0 is still coming up), the API retries the bind in the background (backoff 1s, capped at 30s; logs `address not yet available`) while schedules and valves keep running. The kiosk/phone can connect once the address is up.

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
| `poll_minutes` | 30 | How often to fetch after a successful fetch. A failed fetch (network not up yet, DNS, timeout) retries at 1, 2, 4, … minutes, never longer than this interval; the normal interval resumes after the first success. Stale or implausible data is not a fetch failure. Set `poll_seconds` to override (for example `2`). |
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

Home shows a rain strip under the status card only when the last good gauge samples total at least 0.05 in over 72 hours, the feed is available, and an automatic rain pause is not already active. The strip uses the 24 hour total when that is at least 0.05 in; otherwise it uses the 72 hour total. Its meter runs from 0 to 1 in and is full at 1 in or more; the text shows the real total, for example "0.4 in fell in the last 24 hours". A manual pause can add the 72 hour total on the pause banner. The strip stays off on drier stretches, when rain is disabled, and when rain data is unavailable. `rain` includes `total_24h_inches` and `total_72h_inches` (hundredths of an inch), omitted when there is no usable total. `rain_strip` is `show`, `inches`, and `hours` (`24` or `72`).

## Soil water estimate (display only)

Home can show a per-zone soil-water **estimate**. It never starts, stops, skips, or pauses watering. Rain pause, lockout, STOP, the scheduler, and run history behave exactly as they did without this feature.

The model is a capped daily bucket. For each zone and each local day `d` in a rolling window (oldest to newest, including today):

```text
balance(d) = max(0, min(capacity, balance(d-1) + rain(d) + watering(d) - ETo(d) * crop_factor))
```

`balance` before the first modelled day is **capacity** (the spin-up assumption: the soil is treated as full at the start of the oldest modelled day). Until that window has filled with real rain, ET, and watering, the number is a starting guess, not a measurement. Home does not show that guess as a percent until ET for the window is known.

- **ETo** is daily reference ET from AZMET (`eto_azmet` millimetres ÷ 25.4; `eto_azmet_in` if millimetres are missing). Today's row is often unpublished; the last earlier good day is carried forward. A day with no earlier ET is treated as 0 ET and marked unknown. A daily ETo below 0 or above `max_daily_et_inches` rejects that fetch; the previous good cache is kept. Until a fetch has succeeded and at least one ET day falls in the window, `percent` and `balance_inches` are null.
- **Rain** reuses the last good FCDMC gauge fetch (no second gauge request). If rain pause is off or has no good data, rain is 0 and marked unknown.
- **Watering** sums `completed` and `stopped` history `ActualSec` for that station on the local `StartedAt` date: `ActualSec / 3600 * inches_per_hour`. `skipped`, `refused`, and `error` do not add water. If `inches_per_hour` is null, watering is omitted. The station dialog then shows that zone's rain and crop-ET totals for the modelled window (for example "Rain 0.40 in · ET (plant-adjusted) 1.68 in, last 14 days", using `window_days`) and "Measure sprinkler output to enable".

Copy `config/soil.local.example.json` to `soil.local.json` in the same directory as your config file. Set `azmet_station` to your AZMET id (the example uses `azXX`). Do not commit `soil.local.json`. `ZANJERITO_SOIL_CONFIG` overrides that path. If the file is missing, or `enabled` is false, the estimate is off: no polling, `GET /api/soil` returns `enabled: false` with reason `no soil.local.json` and no `config_error`, and Home shows nothing for soil. If the file is invalid (malformed or empty JSON, wrong types, missing station, bad URL), the error is logged, polling stays off, and `GET /api/soil` returns `enabled: false`, reason `soil.local.json invalid`, and `config_error` with a short parse message. That message does not include the station id, the URL, or a filesystem path. The station dialog shows "Soil settings file has an error (see log)", and Home shows one muted line: "Soil estimate off: settings file has an error (see log)".

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

`et_known` is true only when `et.last_ok_at` is set and at least one ET day in the window is known. Otherwise it is false. `et_reason` is `waiting for first ET fetch` while that first attempt has not finished, and `no ET data yet` after a failed or empty fetch (optionally with a short class such as AZMET unavailable). While `et_known` is false, each zone's `percent` and `balance_inches` are JSON null. Per-zone rain and ET inputs are still present, and the station dialog may still show rain totals. The dialog says "No ET data yet" and does not show a percent, plus "ET unavailable since …" when the feed is down. When ET is known, the zone's rate is measured, and `percent` is set, that station's Home tile shows a thin bar ("Soil about 62%"). Otherwise the tile has no bar. `et_stale` is true when the last successful ET fetch is more than 48 hours old; tiles hide the soil bars when it is, and the dialog says "ET data is stale" instead of a percent. The dialog keeps a "Soil estimate: …" line, including the zone's rain and ET over the window when those inputs exist. "Updated …" stays on that dialog line. A missing settings file shows nothing on Home or in the dialog. `updated_at` is the last successful ET fetch, the same instant as `et.last_ok_at`, or null if there has never been one. It is not the current time and not the time of a failed attempt.

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

## Kiosk snapshot

`GET /api/kiosk` is a read-only snapshot (`Cache-Control: no-store`) for the native kiosk and for Home. It does not start watering. Unlike `GET /api/status`, it does not sync an expired pause to disk. A config load error is HTTP 500 `{"error":"..."}`. Clients should use this body instead of recomputing the next fire or the in-run itinerary.

`now` and the schedule instants are RFC3339 in the config timezone (`timezone` is that name; the default is America/Phoenix). `phase`, `last_error`, `current_station`, and `stations_on` match status. `lockout` is false. `current_station` is the engine's current station, which is whichever station map entry is seen last while two relays overlap. The run object below names the itinerary step separately.

`pause` is `paused`, `until`, `label`, `reason`, `source`, `rain_inches`, and `last_rain_at`. Those are the same facts as status (`until` is `paused_until`, `source` is `pause_source`). `rain_strip` is exactly the Home strip: `show`, `inches`, and `hours` (`24` or `72`). Inches are rounded to the nearest hundredth before the 0.05 in cutoff, so 0.044 in stays hidden (shown as 0.04 if it were displayed) and 0.045 through 0.049 in display as 0.05. The 24 hour total is used when that rounded total is at least 0.05 in; otherwise the strip uses 72 hours. An automatic rain pause hides the strip. `rain` is `enabled`, `unavailable`, `total_24h_inches`, `total_72h_inches`, and `have_totals` (true only when both totals exist). The gauge id is not included.

`next_run` is the next enabled fire strictly after the current local minute, or null when nothing qualifies in 400 local dates (no schedules, or none enabled). It ignores an active pause. `skipped_by_pause` is true when a pause is active and that pause is indefinite or this fire's `at` is before `until`. A fire exactly at `until` is not skipped: pause-until is exclusive, matching the scheduler. `next_effective_run` is the first fire that still runs. It is the same instant as `next_run` when nothing is paused, null when the pause is indefinite, and otherwise the first fire at or after `until`. On that object `skipped_by_pause` is false.

Eligibility matches the scheduler's calendar rules, not its "this minute is due" check: enabled, a valid `HH:MM`, at least one step, weekday (an empty list means every day), and `starts_on` / `ends_on`. The earliest instant wins. Equal instants keep schedule list order. A same-day start is included only when its `HH:MM` is later than now's `HH:MM`, so a fire in the current minute has already passed. Each fire is `schedule_id`, `name` (the note, or the id), `at`, `ends_at` (`at` plus the sum of step minutes), `total_min`, and `skipped_by_pause`.

Instants are built with `time.Date` in the config zone. A skipped civil time is not a fire: America/Denver 2026-03-08 02:00–02:59 does not occur, and `time.Date` normalizes 02:30 to 01:30 MST, which is not used. A repeated civil time (America/Denver 2026-11-01 01:00–01:59) keeps both real occurrences, earliest first. The scan assumes a one-hour fold. America/Phoenix has no fold.

`run` is null when nothing is watering. Otherwise it is `kind`, `program_id`, `program`, `started_at`, `step_index` (`-1` until the first station turns on), `step_count`, `current_station`, `next_station`, `step_elapsed_sec`, `step_remaining_sec`, `run_remaining_sec`, `run_total_sec`, and `steps`. Each step is `station_id`, `title`, `planned_sec`, `elapsed_sec`, `remaining_sec`, and `state` (`done`, `active`, or `pending`). Seconds are truncated so elapsed plus remaining equals planned. Overlap timing is approximate: remaining time is the current step's remainder plus later planned steps. The previous station can still be energized during the overlap window after its step is `done`. Isolate mode uses the same station-on total and does not add the power-down gap.

`stations` is `id`, `title`, `color`, `on`, `state` (`running`, `queued`, or `idle`), `rain_pause_exempt`, and `soil_percent`. There is no BCM, physical, or wiringPi pin, and the power relay is not a station. `running` means the relay is on, including a station whose step is already `done` but still inside the overlap window. `soil` is `enabled`, `et_known`, `et_stale`, `show_bars`, and `updated_at`. `show_bars` is true only when the estimate is on, ET is known and fresh, and at least one zone has a percent. `soil_percent` is null whenever `show_bars` is false, including when ET is stale. `GET /api/soil` can still return those percents.

`GET /api/events` status events keep `Phase`, `CurrentStation`, `StationsOn`, and `LastError`, and also send `phase`, `current_station`, `stations_on`, and `last_error`.

Home's Next line uses `next_run` from this endpoint when that fetch succeeds, and otherwise keeps the in-page scan. While paused, the line still says "Schedules skip while paused" rather than `next_effective_run`. Each schedule card still computes its own next fire in the page. The in-run countdown on Home is still that page's guess, not `run`.

## Native kiosk client (experimental)

A native C and LVGL client of the daemon's HTTP JSON API is in [native-kiosk/README.md](native-kiosk/README.md). It is not a browser and not a second source of truth. It stays read-only unless `--allow-writes` is passed.
Boot-persistent kiosk (install only; nothing is enabled): [docs/native-kiosk-boot.md](docs/native-kiosk-boot.md).
