# Zanjerito — Architecture (v2)

**Status:** Draft v0.4 · 2026-09-12  
**Companions:** [prd.md](./prd.md) · [decisions.md](./decisions.md) · [pin-map.md](./pin-map.md)

## Layers

```
[ Phone on LAN ]              [ Wall screen — v1, not MVP ]
        \                         /
         \                       /
          v                     v
     static UI (embedded in the binary)
                 |
                 v
     HTTP JSON + SSE   (LAN bind)
                 |
                 v
     engine: scheduler + one run-queue + safety
        |                    |
        |                    +-- store (atomic JSON)
        |
        v
     gpio driver (real | fake | lockout)
                 |
                 v
     /dev/gpiochip0  →  relay board  →  24VAC valves
```

The UI never toggles pins. Everything that waters goes through the engine.

## Runtime

- Language: Go (see D1).
- GPIO: Linux character device, `github.com/warthog618/go-gpiocdev`.
- Hardware now: Raspberry Pi 3, typically `gpiochip0`. Confirm with `gpiodetect` and `uname -m`.
- Cross-compile from amd64:
  - 64-bit Pi OS: `GOOS=linux GOARCH=arm64 CGO_ENABLED=0`
  - 32-bit Pi OS: `GOOS=linux GOARCH=arm GOARM=7 CGO_ENABLED=0`

Suggested layout on `rewrite/go`:

```
cmd/zanjerito/
internal/gpio/        # Driver interface + gpiocdev + fake
internal/engine/      # state machine + run-queue
internal/schedule/    # start times + itinerary + date windows
internal/store/       # atomic JSON
internal/api/         # REST + SSE
ui/                   # static assets, go:embed
deploy/zanjerito.service
config/pinmap.example.json
```

## State machine

```
Idle → PowerUp → StationOn → (optional Overlap) → PowerDown → Idle
              \_____________________________________/
                         Fault, Lockout, STOP
```

Invariants:

1. Exactly one writer to GPIO.
2. Power enable is first-class, not another station.
3. Polarity lives in config (`active_low: true`).
4. Start / SIGTERM / SIGINT / panic / systemd stop → all stations off, then PSU off.
5. **Fault → all-off:** any engine fault path (GPIO error, invalid transition, watchdog trip, unhandled panic recovery) → all stations off, then PSU off — same as stop/signals.
6. Line request uses inactive-on-release so process death de-energizes.
7. Independent `max_on_sec` cap (default 900, matching bash's 15 min).
8. PSU on only while a station is on or inside the documented overlap/settle window.
9. Dry-run / lockout is a driver mode.
10. **Overlap ceiling:** at most **two** stations ON at once (current + previous during `overlap_ms`). Never three.
11. **Config while watering:** reject or defer pin-map / station / schedule writes that would change the active run; return a clear error (or queue until Idle). Read APIs stay allowed.

## Sequencing (D2)

Default `overlap` (anti-hammer): keep 24VAC up for the itinerary; open next station, then close previous; `overlap_ms` default 2000. If duration < overlap, shrink overlap. **Hard ceiling: ≤2 stations ON** (invariant 10).

Alternate `isolate` (bash-like): drop 24VAC, all off, raise 24VAC, one station. For commissioning or a weak transformer.

`front.sh` parity is stations + durations, not bash dead-time.

## Collision

One run-queue globally. Never two programs on GPIO at once.

| Event | Policy |
|---|---|
| Second schedule fires while one is running | **Skip + log** (D13). Do not interleave itineraries. Optional single-slot queue later if we miss programs. |
| Manual run while a schedule is watering | Warning dialog, then abort remaining itinerary and start the manual run. |
| Config write (pin map / station / schedule) while watering | **Reject or defer** until Idle; clear error to client. Reads OK. |
| STOP | Immediate all-off + PSU off. No confirm if already watering. |
| Fault | All stations off, then PSU off (invariant 5). |

## Store

Atomic JSON (temp + rename) for pin map, stations, schedules. SQLite later for history (D10).

Timezone for all wall-clock math: `America/Phoenix` (no DST).

## HTTP API (v1 sketch)

| Method | Path | Purpose |
|---|---|---|
| GET | `/api/status` | now / next / last error / lockout |
| GET, PATCH | `/api/stations/:id` | list/edit |
| POST | `/api/stations/:id/run` | `{durationSec}` — may preempt |
| POST | `/api/run/cancel` | STOP |
| GET, PUT | `/api/schedules/:id` | named programs + itinerary + date window |
| GET | `/api/events` | SSE: status / station / fault |

Auth: LAN bind only for MVP. No GraphiQL.

## Packaging

systemd system unit + static binary. `ExecStop=` must force all-off. Dual-run with bash before flip (D9).
