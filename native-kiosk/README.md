# Native kiosk client

A native C and [LVGL](https://lvgl.io/) client of the irrigation daemon's HTTP JSON API. It draws an 800×480 wall screen and talks to the daemon the same way the phone UI does. It is not a browser, and it is not a second source of truth: schedules, pause, and valves stay in the daemon.

## Design

Visual language is **Cards + desert light** (Style A) from [docs/ui-directions.md](../docs/ui-directions.md): flat sand page, cream cards, terracotta STOP, canal teal for an active run, dusk plum for pause. No photo thumbnails.

Screens the client actually builds:

| Screen | What you see |
|---|---|
| Home | Next-run header, optional rain strip, station tiles, STOP and Pause |
| Running | Step countdown, taller STOP, Pause |
| Paused | Pause banner, info card, STOP and Resume |
| Pause picker | Tomorrow morning, 2 days, 1 week, until further notice |
| Schedules | Read-only rows. Hold to edit shows a toast; it does not edit |
| Confirm STOP / Confirm pause | Modal over the screen. Only OK and Cancel are tappable |

Next-run text and remaining time are inferred on the device from `/api/schedules` and `/api/status`. The daemon does not yet send those as kiosk fields.

## Layout

The framebuffer is 800×480. Hit targets use **267 px/in** (`ZK_PX_PER_INCH`). `tests/test_layout` checks the heights and the 107 px floor.

| Target | Size | At 267 px/in |
|---|---|---|
| Screen | 800×480 | — |
| STOP (home, paused, picker, schedules) | 236×264 | height 0.99 in |
| STOP while running | 236×304 | height 1.14 in |
| Every other hit target | at least 107×107 | at least 0.4 in |

## Build

LVGL is not vendored. The pin is `tools/lvgl.pin` (tag `v9.2.2`, commit `7f07a129e8d77f4984fff8e623fd5be18ff42e74`). `make fetch-lvgl` shallow-clones it into `.cache/lvgl` and checks that commit.

From `native-kiosk/`:

| Target | What it does |
|---|---|
| `make fetch-lvgl` | Clone the pinned LVGL tree if `.cache/lvgl` is missing |
| `make host` | `build/zan-kiosk-host` for this machine |
| `make arm` | Static 32-bit ARM hard-float binary `build/zan-kiosk-arm` via Docker (`debian:bookworm-slim`, `gcc-arm-linux-gnueabihf`, `-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard -static`) |
| `make test` | Host unit tests (`test_json`, `test_logic`, `test_layout`, `test_http`, `test_data`) |
| `make e2e` | Build the host binary, then `tests/e2e.sh` |
| `make check` | `test` then `e2e` |
| `make shots` | Host build, then `zan-kiosk-host --shot-all build/shots --fixtures-root tests/fixtures` |

`make arm` links `-static`. The linker warns that `getaddrinfo` in a statically linked glibc binary still needs the matching NSS shared libraries at runtime. A numeric `--api` host such as `127.0.0.1` is parsed with `inet_pton` and does not use that path.

### Regenerate assets

Committed C under `generated/` comes from:

- `tools/gen-fonts.sh` — `npx lv_font_conv@1.5.3` on `fonts/*.ttf`
- `tools/gen-icons.js` — rasterize `tools/icons.json`. Set `PLAYWRIGHT_CORE` (path to the `playwright-core` module) and `CHROMIUM_PATH` (Chromium binary). Neither path is built in.

Do not hand-edit `generated/`.

## Run

The base URL is never compiled in. With no `--api`, no `ZAN_API`, and no `--fixture` or `--fixtures-root`, the process exits 2.

| Flag | Meaning |
|---|---|
| `--api URL` | API base URL. Otherwise the `ZAN_API` environment variable |
| `ZAN_API` | Same as `--api` when that flag is omitted |
| `--fixture DIR` | Load one scenario directory from disk. No socket |
| `--live-clock` | Advance the clock from the system clock. Fixture mode otherwise freezes `status.now` |
| `--allow-writes` | Permit the three mutating POSTs. Default is read-only |
| `--fb PATH` | Framebuffer to open. There is no default device |
| `--touch PATH` | evdev device. Default is autodetect |
| `--touch-swap` | Swap X and Y after open |
| `--touch-flip-x` | Flip X using the device's reported range |
| `--touch-flip-y` | Flip Y using the device's reported range |
| `--script SPEC` | Headless memory display. `tap:X,Y;wait:MS;shot:FILE` separated by `;` |
| `--shot NAME=FILE` | Render the current screen once to `FILE` |
| `--shot-all OUTDIR` | Write one PNG per scenario. Requires `--fixtures-root` |
| `--fixtures-root DIR` | Fixture tree for `--shot-all` |
| `--duration SEC` | Exit this many seconds after process start |
| `--stats` | One JSON stats line on stderr |
| `--help` | Usage, exit 0 |

Example against a daemon already listening on localhost (nothing in this tree starts one):

```sh
./build/zan-kiosk-host --api "$ZAN_API" --fb /dev/fb0
```

## Fixtures and shots

`tests/fixtures/<scenario>/` holds `status.json`, `stations.json`, `schedules.json`, and `soil.json`. Scenarios: `home-rain`, `home-norain`, `home-nosoil`, `home-stale`, `home-fault`, `running`, `paused-rain`, `paused-manual`. Names and station ids in those files are generic.

`make shots` writes `build/shots/`:

| File | Scenario | Screen |
|---|---|---|
| `01-home-idle-rain.png` | home-rain | Home |
| `01b-home-idle-no-rain.png` | home-norain | Home |
| `01c-home-no-soil.png` | home-nosoil | Home |
| `01d-home-soil-stale.png` | home-stale | Home |
| `01e-home-fault.png` | home-fault | Home |
| `02-running.png` | running | Running |
| `03-paused-rain.png` | paused-rain | Paused |
| `03b-paused-manual.png` | paused-manual | Paused |
| `04-pause-picker.png` | home-rain | Pause picker |
| `05-schedules.png` | home-rain | Schedules |
| `06-confirm-stop.png` | home-rain | Confirm STOP |
| `07-confirm-pause.png` | home-rain | Confirm pause (tomorrow-morning chip) |
| `08-home-stop-pressed.png` | home-rain | Home, STOP pressed |
| `09-offline.png` | home-rain | Home with the stale pill |

`--shot` and `--shot-all` use the memory display. They do not open a framebuffer.

## Host tests

`make test` builds with `-Wall -Wextra -Werror` and ASan/UBSan when the compiler accepts them.

| Binary | Covers |
|---|---|
| `test_json` | Parse every fixture; garbage and defaults; wall-clock parse and advance |
| `test_logic` | Next run, rain strip, soil percent, pause-preview bodies, run inference, pause title and until text |
| `test_layout` | Every screen, no overlap, hit testing, STOP 264/304 px tall and tallest, other targets at least 107 px, picker chips 255×188, 267 px/in |
| `test_http` | GET and POST to a localhost stub; read-only stop does not connect; cancel, pause, and resume paths; timeout; oversized body; non-`http` URL rejected |
| `test_data` | Status poll and slow polls; stale after the stub stops; fixture load opens no socket; read-only actions do not POST; writes POST cancel, pause, and resume and repoll status; `zk_data_get` does not block on a slow stub |

`ZK_POLL_MS_STATUS` overrides the 2 second status poll inside those data tests. It is not a user-facing flag.

`make e2e` runs `tests/e2e.sh` against `tools/stub_server.py` on an ephemeral localhost port. It checks read-only taps, the three write POSTs, a confirm modal that ignores the rail, an unreachable API (stale pill, exit 0), and exit 2 when no API is configured.

## Safety

The default is read-only. Mutating calls are not sent, and the screen says so.

The only writes are `POST /api/run/cancel`, `POST /api/pause`, and `POST /api/pause/resume`. They are sent only with `--allow-writes`. STOP and pause are sent only after the on-screen OK. Cancel sends nothing. Resume is sent when the Resume button is tapped. The URL is never compiled in. This tree does not change the daemon. Writes are tested only against the local stub (`tools/stub_server.py` and `test_http` / `test_data`), not against a real controller.

## Raspberry Pi

Target board is a Pi 3B (armv7). The display is the legacy 800×480 32 bpp framebuffer: pass `--fb /dev/fb0`. Touch is evdev. Autodetect prefers a device whose name contains `raspberrypi-ts`, and the client maps that device's reported absolute range onto the framebuffer with no rotation. For that panel the mapping is identity (screen pixels). `--touch-swap` and the flip flags stay off unless you pass them.

On the Raspberry Pi OS desktop image, X owns `/dev/fb0` and the touch device. Do not run this client on top of a live desktop. Nothing here installs a systemd unit or a boot hook.

## Known gaps

- No daemon `GET /api/kiosk`. The client polls `/api/status`, `/api/stations`, `/api/schedules`, and `/api/soil`.
- Next-run and remaining time are computed in the client. The daemon does not send those fields.
- Hold to edit is not implemented (toast only).
- No systemd unit.

## Licenses

Fonts are SIL OFL 1.1 (`fonts/`). cJSON is MIT. stb_image_write is public domain (Unlicense) or MIT. LVGL is MIT and is fetched, not vendored. See [THIRD_PARTY.md](THIRD_PARTY.md).
