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
git checkout fancy-vibes
make build
./zanjerito -config config/config.example.json -driver=fake -listen 127.0.0.1:8080
# phone UI: http://127.0.0.1:8080/
```

The older Node 14 / GraphiQL app under this tree is a **behavioral reference** only (not the production UI or Developing default). Prefer the embedded Direction D UI served by the Go binary.

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
| `dry_days` | 2 | Hold until this many days after the newest rain in the window. |
| `heavy_inches` | 1.0 | At or above this total, use `heavy_dry_days` instead. |
| `heavy_dry_days` | 4 | Longer hold after a heavier total. |
| `stale_hours` | 7 | Newest sample older than this is ignored. |

Gauges send a 0.00 reading about every 6 hours when it is dry, so a stale window of a few hours would mark healthy data unavailable. The default is 7 hours and is configurable. Bad or stale data never pauses and never clears a pause that is already set. A manual pause (any reason, including an indefinite one) is left alone: automatic rain will not override it or shorten it. Resume remembers the time so the same rain does not immediately pause again. Later rain that crosses the threshold in the rolling window can.

While an automatic rain pause is active, schedules water only stations with `rain_pause_exempt: true` and skip the rest (recorded as skipped, reason `rain`). The example drip station is exempt. A manual pause still skips every station. Home shows the automatic pause on the same banner as a manual pause, for example "Paused for rain (0.4 in) until Tue morning". A small "Rain data unavailable" line appears on Home only when the feature is on and the latest fetch failed or is stale. Status JSON adds `pause_source`, `rain_inches`, `last_rain_at`, and a `rain` object. It does not include the gauge id or the gauge URL.
