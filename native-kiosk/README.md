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
| `make test` | Host unit tests (`test_json`, `test_logic`, `test_layout`, `test_http`, `test_data`, `test_stop_latency`) |
| `make e2e` | Build the host binary, then `tests/e2e.sh` |
| `make check` | `test`, then `e2e`, then `deploy-check` |
| `make deploy-check` | `tests/deploy_test.sh` (unit, installer, wrapper). No LVGL build |
| `make shots` | Host build, then `zan-kiosk-host --shot-all build/shots --fixtures-root tests/fixtures` |

`make arm` links `-static`. The linker warns that `getaddrinfo` in a statically linked glibc binary still needs the matching NSS shared libraries at runtime. A numeric `--api` host such as `127.0.0.1` is parsed with `inet_pton` and does not use that path. A hostname is resolved with `getaddrinfo`, which has no timeout: a slow resolver stalls that request until the lookup returns. On the kiosk, use an IPv4 literal.

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
| `test_json` | Parse every fixture; garbage and defaults; non-finite or huge rain totals hide the strip; wall-clock parse and advance |
| `test_logic` | Next run, rain strip, soil percent, pause-preview bodies, run inference, pause title and until text; inches clamp for NaN, inf, and huge values |
| `test_layout` | Every screen, no overlap, hit testing, STOP 264/304 px tall and tallest, other targets at least 107 px, picker chips 255×188, 267 px/in |
| `test_http` | GET and POST to a localhost stub; read-only stop does not connect; cancel, pause, and resume paths; timeout; oversized body; non-`http` URL rejected |
| `test_data` | Status poll and slow polls; stale after the stub stops; fixture load opens no socket; read-only actions do not POST; writes POST cancel, pause, and resume and repoll status; `zk_data_get` does not block on a slow stub |
| `test_stop_latency` | STOP is posted while a GET is in flight; a repeated STOP or resume is one POST; identical pause bodies collapse; a different pause is still sent; the queue still overflows; read-only sends no POST |

`ZK_POLL_MS_STATUS` overrides the 2 second status poll inside those data tests. It is not a user-facing flag.

`make e2e` runs `tests/e2e.sh` against `tools/stub_server.py` on an ephemeral localhost port. It checks read-only taps, the three write POSTs, a confirm modal that ignores the rail, an unreachable API (stale pill, exit 0), and exit 2 when no API is configured. It also checks that extra taps just after pause OK do not send a second pause, that two STOP confirms about a second apart are one cancel when that POST is slow, and that STOP is logged while GETs are delayed. The stub takes `--get-delay SEC` and `--post-delay SEC` and serves each connection on its own thread so a slow GET does not hold a POST. Timestamps for logged writes are appended to `<log>.ts` as monotonic seconds; the request log lines themselves stay `METHOD path body`.

## Behaviour

In HTTP mode a poll thread GETs `/api/status` about every 2 seconds and `/api/stations`, `/api/schedules`, and `/api/soil` about every 30 seconds. STOP, pause, and resume run on a second thread. That thread opens its own sockets and does not wait for a poll to finish, so STOP is not stuck behind a slow GET. After a write succeeds, status is polled again.

A second STOP is not posted while one is already queued or in flight. Resume is treated the same way. A pause is skipped only when the same body is already queued or in flight; a different pause body is sent. The screen still gets one result for the submit it is waiting on. The queue holds 8 actions; one more reports overflow and sends nothing.

Read-only mode does not open a socket for those POSTs. Fixture mode loads the scenario from disk: no threads and no socket.

STOP and pause are sent only after OK, and only with `--allow-writes`. Pause OK closes the picker and returns to the base screen. For 700 ms after OK, taps on a pause chip or on OK/Cancel are ignored, so a stray tap cannot open or confirm another pause or STOP. Resume sends as soon as Resume is tapped. Cancel sends nothing.

A hostname in `--api` is resolved with `getaddrinfo`, which has no timeout. A slow resolver stalls that request. Use an IPv4 literal on the kiosk (`inet_pton` handles numeric hosts).

## Safety

The default is read-only. Mutating calls are not sent, and the screen says so.

The only writes are `POST /api/run/cancel`, `POST /api/pause`, and `POST /api/pause/resume`. The URL is never compiled in. This tree does not change the daemon. Writes are tested only against the local stub (`tools/stub_server.py`, `test_http`, `test_data`, and `test_stop_latency`), not against a real controller.

## Raspberry Pi

Target board is a Pi 3B (armv7). The display is the legacy 800×480 32 bpp framebuffer: pass `--fb /dev/fb0`. Touch is evdev. Autodetect prefers a device whose name contains `raspberrypi-ts`, and the client maps that device's reported absolute range onto the framebuffer with no rotation. For that panel the mapping is identity (screen pixels). `--touch-swap` and the flip flags stay off unless you pass them.

On the Raspberry Pi OS desktop image, X owns `/dev/fb0` and the touch device. Do not run this client on top of a live desktop. Boot-persistent install is opt-in; `make` does not enable it. See below.

## Boot-persistent kiosk (full kiosk mode)

`deploy/` can install a systemd unit that starts `/opt/zanjerito/zan-kiosk` on `/dev/fb0` at boot and keeps the desktop off that framebuffer. Copying the files does not enable the unit and does not change the boot target. The procedure, the framebuffer dump, and the rollback commands are in [docs/native-kiosk-boot.md](../docs/native-kiosk-boot.md).

Build the binary with `make -C native-kiosk arm` (daemon already running; do not restart it for this). Then, on the Pi:

```sh
sudo native-kiosk/deploy/install-kiosk.sh --bin native-kiosk/build/zan-kiosk-arm
# edit /etc/default/zan-kiosk — set ZAN_API; the placeholder is refused
sudo native-kiosk/deploy/install-kiosk.sh --switch
sudo reboot
# or, without a reboot: sudo native-kiosk/deploy/install-kiosk.sh --switch --now
```

`--switch` sets the default target to `multi-user.target`, disables `lightdm`, and enables `zan-kiosk`. `--now` also stops `lightdm` and starts the kiosk. The unit `Conflicts=` with `lightdm.service` and `getty@tty1.service`.

It does not change the daemon, `config.json`, `zanjerito.env`, cron, or boot `config.txt` / `cmdline.txt`. It does not stop or restart `zanjerito.service`.

There is no `ExecStop=` that sends STOP or all-off. The kiosk is only a client. Valve safety stays in `zanjerito.service` (SIGTERM runs engine all-off, and `max_on` is enforced there). A STOP on every kiosk stop or crash-restart would cancel a scheduled or manual run, and it would depend on the kiosk being able to POST.

Writes stay off unless `ZAN_ALLOW_WRITES=1`. An existing install may already have writes on. The installed example leaves them off.

`prepare` (root, each start) writes `0` to `fbcon/cursor_blink` when that file exists, sends `ESC[?25l` and `ESC[9;0]` to `/dev/tty1`, and runs `setterm --blank 0 --powerdown 0 --cursor off` when `setterm` exists. Missing files are skipped. `BACKLIGHT` (0–255) is a fixed level written to `rpi_backlight`; there is no idle dim. A screen timeout needs client support.

Kernel printk can still draw on tty1. `sudo dmesg -n 1` is optional and not persistent. Putting the tty in `KD_GRAPHICS` is a future client change.

Check with `systemctl is-active zan-kiosk`, `journalctl -u zan-kiosk`, and confirm `lightdm` is inactive while `zanjerito` is still active.

If the panel is blank after reboot, SSH in and roll back. The daemon keeps running either way.

```sh
sudo /opt/zanjerito/rollback-desktop.sh
```

If the script is gone:

```sh
sudo systemctl disable --now zan-kiosk && sudo systemctl enable lightdm && sudo systemctl set-default graphical.target && sudo systemctl start lightdm
```

The old transient unit (not this boot unit) is cleared with `sudo systemctl stop zan-kiosk && sudo systemctl start lightdm`.

Rollback restores lightdm and `graphical.target`. Installed files can stay, or be removed with `rm` of `/opt/zanjerito/zan-kiosk`, `zan-kiosk-run.sh`, `rollback-desktop.sh`, `zan-kiosk.bak.*`, `/etc/systemd/system/zan-kiosk.service`, and `/etc/default/zan-kiosk`, then `systemctl daemon-reload`. That list does not include the daemon binary or its config.

## Known gaps

- No daemon `GET /api/kiosk`. The client polls `/api/status`, `/api/stations`, `/api/schedules`, and `/api/soil`.
- `getaddrinfo` has no timeout. A hostname in `--api` can stall one request for as long as the resolver takes. Use an IPv4 literal on the kiosk.
- Next-run and remaining time are computed in the client. The daemon does not send those fields.
- Hold to edit is not implemented (toast only).
- No idle dim or screen timeout. The boot unit can set a fixed backlight only.
- The client does not set the tty to KD_GRAPHICS. Kernel messages can still draw on tty1.

## Licenses

Fonts are SIL OFL 1.1 (`fonts/`). cJSON is MIT. stb_image_write is public domain (Unlicense) or MIT. LVGL is MIT and is fetched, not vendored. See [THIRD_PARTY.md](THIRD_PARTY.md).
