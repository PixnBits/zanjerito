# Native kiosk client

A native C and [LVGL](https://lvgl.io/) client of the irrigation daemon's HTTP JSON API. It draws an 800×480 wall screen and talks to the daemon the same way the phone UI does. It is not a browser, and it is not a second source of truth: schedules, pause, and valves stay in the daemon.

## Design

Visual language is **Cards + desert light** (Style A) from [docs/ui-directions.md](../docs/ui-directions.md): flat sand page, cream cards, terracotta STOP, canal teal for an active run, dusk plum for pause. No photo thumbnails.

Screens the client actually builds:

| Screen | What you see |
|---|---|
| Home | Next-run header, Schedules button, optional rain strip, station tiles, STOP and Pause |
| Running | Step countdown from `run.*`, taller STOP, Pause |
| Paused | Pause banner, ON HOLD card from `next_effective_run`, Schedules button, STOP and Resume |
| Station sheet | Read-only detail for one station. Close on the rail; STOP stays |
| Needs update | Card when `GET /api/kiosk` is HTTP 404. STOP stays; Pause and Schedules are hidden |
| Pause picker | Tomorrow morning, 2 days, 1 week, until further notice |
| Schedules | Read-only rows. Hold to edit shows a toast; it does not edit |
| Confirm STOP / Confirm pause | Modal over the screen. Only OK and Cancel are tappable |

The daemon owns next run, run progress, rain-strip visibility, and soil percents. The client formats those fields. Pause-picker preview still uses `/api/schedules` (`zk_pause_preview` / `zk_morning_cutoff`).

## Layout

The framebuffer is 800×480. Hit targets use **267 px/in** (`ZK_PX_PER_INCH`). `tests/test_layout` checks the heights and the 107 px floor.

| Target | Size | At 267 px/in |
|---|---|---|
| Screen | 800×480 | — |
| STOP (home, paused, picker, schedules, station, needs-update) | 236×264 | height 0.99 in |
| STOP while running | 236×304 | height 1.14 in |
| Schedules button (Home idle, Paused) | 120×108 | at least 0.4 in |
| Every other hit target | at least 107×107 | at least 0.4 in |

## Build

LVGL is not vendored. The pin is `tools/lvgl.pin` (tag `v9.2.2`, commit `7f07a129e8d77f4984fff8e623fd5be18ff42e74`). `make fetch-lvgl` shallow-clones it into `.cache/lvgl` and checks that commit.

From `native-kiosk/`:

| Target | What it does |
|---|---|
| `make fetch-lvgl` | Clone the pinned LVGL tree if `.cache/lvgl` is missing |
| `make host` | `build/zan-kiosk-host` for this machine |
| `make arm` | Static 32-bit ARM hard-float binary `build/zan-kiosk-arm` via Docker (`debian:bookworm-slim`, `gcc-arm-linux-gnueabihf`, `-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard -static`) |
| `make test` | Host unit tests (`test_json`, `test_logic`, `test_layout`, `test_http`, `test_data`, `test_stop_latency`, `test_boot`) |
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
| `--live-clock` | Advance the clock from the system clock. Fixture mode otherwise freezes `kiosk.now` |
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

`tests/fixtures/<scenario>/` holds `kiosk.json` (same keys as `GET /api/kiosk`) and `schedules.json` when the Schedules screen or pause preview needs it. Scenarios: `home-rain`, `home-norain`, `home-nosoil`, `home-stale`, `home-fault`, `running`, `paused-rain`, `paused-manual`, `paused-long`, `home-longnames`, `running-long`. Names and station ids are generic (`Test Station N`, timezone `America/Denver`).

`make shots` writes `build/shots/`:

| File | Scenario | Screen |
|---|---|---|
| `01-home-idle-rain.png` | home-rain | Home (includes Schedules button) |
| `01b-home-idle-no-rain.png` | home-norain | Home |
| `01c-home-no-soil.png` | home-nosoil | Home |
| `01d-home-soil-stale.png` | home-stale | Home |
| `01e-home-fault.png` | home-fault | Home |
| `02-running.png` | running | Running (`run.*` from the fixture) |
| `03-paused-rain.png` | paused-rain | Paused |
| `03b-paused-manual.png` | paused-manual | Paused |
| `04-pause-picker.png` | home-rain | Pause picker |
| `05-schedules.png` | home-rain | Schedules |
| `06-confirm-stop.png` | home-rain | Confirm STOP |
| `07-confirm-pause.png` | home-rain | Confirm pause (tomorrow-morning chip) |
| `08-home-stop-pressed.png` | home-rain | Home, STOP pressed |
| `09-offline.png` | home-rain | Home with the stale pill |
| `10-station-sheet-soil.png` | home-rain | Station sheet, tile 0 |
| `10b-station-sheet-soil-stale.png` | home-stale | Station sheet |
| `10c-station-sheet-nosoil.png` | home-nosoil | Station sheet |
| `10d-station-sheet-running.png` | running | Station sheet, running station |
| `10e-station-sheet-exempt.png` | home-rain | Station sheet, rain-exempt tile |
| `11-controller-needs-update.png` | home-rain | Needs-update card |
| `12-home-schedules-button.png` | home-rain | Same Home as `01` |
| `13-paused-schedules-button.png` | paused-rain | Paused with Schedules button |

`--shot` and `--shot-all` use the memory display. They do not open a framebuffer.

## Host tests

`make test` builds with `-Wall -Wextra -Werror` and ASan/UBSan when the compiler accepts them.

| Binary | Covers |
|---|---|
| `test_json` | Parse every fixture kiosk.json; nulls and missing optional; garbage, truncated, wrong types; 1e999 / huge arrays; `rain_strip.show` is the only visibility input; console cursor write to `ZK_TTY` |
| `test_logic` | Formatting (relative day, fire when, rain strip text, inches clamp); remaining pause-preview from `/api/schedules`; pause title and until text |
| `test_layout` | Every screen including station sheet and needs-update; no overlap; STOP 264/304; Schedules button and tiles ≥107; 1..8 stations |
| `test_http` | GET and POST to a localhost stub; read-only stop does not connect; cancel, pause, and resume paths; timeout; oversized body; non-`http` URL rejected |
| `test_data` | `/api/kiosk` poll and `/api/schedules` slow poll; 404 => NEEDS_UPDATE and no `/api/status`; stale after the stub stops; fixture load opens no socket; writes POST and repoll kiosk |
| `test_stop_latency` | STOP is posted while a GET is in flight; a repeated STOP or resume is one POST; identical pause bodies collapse; a different pause is still sent; the queue still overflows; read-only sends no POST |
| `test_boot` | Forced redraw after first frame is not due before 2 s, due once at 2 s, due once more at 5 s, never again; unsigned elapsed still works across a clock wrap |

`ZK_POLL_MS_STATUS` overrides the 2 second `/api/kiosk` poll inside those data tests. It is not a user-facing flag.

`make e2e` runs `tests/e2e.sh` against `tools/stub_server.py` on an ephemeral localhost port. It checks read-only taps, the three write POSTs, a confirm modal that ignores the rail, an unreachable API (stale pill, exit 0), and exit 2 when no API is configured. It also checks tile tap (station sheet, zero non-GET writes), Schedules button, STOP from the sheet, `--kiosk-404` needs-update with STOP still working, and display power against a temp backlight directory (`ZK_POWER_FORCE=1`: dim, off, swallowed wake, STOP while dimmed, watering and fault stay on, pause dims but does not blank, SIGTERM and SIGINT restore brightness). The stub takes `--get-delay SEC`, `--post-delay SEC`, and `--kiosk-404`. Each connection is its own thread so a slow GET does not hold a POST. Timestamps for logged writes are appended to `<log>.ts` as monotonic seconds; the request log lines themselves stay `METHOD path body`.

## Behaviour

In HTTP mode a poll thread GETs `/api/kiosk` about every 2 seconds and `/api/schedules` about every 30 seconds. It does not poll `/api/status`, `/api/stations`, or `/api/soil`. HTTP 404 from `/api/kiosk` is the needs-update state: the main area shows a card, Pause and Schedules are hidden, STOP still works, and the client does not fall back to `/api/status` or recompute. Transport, 5xx, and malformed bodies are unreachable (existing stale pill). STOP, pause, and resume run on a second thread. That thread opens its own sockets and does not wait for a poll to finish, so STOP is not stuck behind a slow GET. After a write succeeds, `/api/kiosk` is polled again.

A station tile opens a read-only sheet (title, colour, Running/Queued/Idle, rain-exempt line, soil bar only when `soil.show_bars` and `soil_percent` are set). Last run is not in `/api/kiosk` and is omitted; adding `last_run` to that snapshot later would fill it. The sheet never POSTs. If the station disappears from a later snapshot, the sheet closes.

A visible Schedules button (icon + label, at least 107×107) is on Home idle and Paused. Next-run header and the paused info card remain shortcuts. Running has no Schedules button. The Home brand mark is dropped so that button fits in the header's right slot without shrinking STOP or Pause.

A second STOP is not posted while one is already queued or in flight. Resume is treated the same way. A pause is skipped only when the same body is already queued or in flight; a different pause body is sent. The screen still gets one result for the submit it is waiting on. The queue holds 8 actions; one more reports overflow and sends nothing.

Read-only mode does not open a socket for those POSTs. Fixture mode loads the scenario from disk: no threads and no socket.

STOP and pause are sent only after OK, and only with `--allow-writes`. Pause OK closes the picker and returns to the base screen. For 700 ms after OK, taps on a pause chip or on OK/Cancel are ignored, so a stray tap cannot open or confirm another pause or STOP. Resume sends as soon as Resume is tapped. Cancel sends nothing.

A hostname in `--api` is resolved with `getaddrinfo`, which has no timeout. A slow resolver stalls that request. Use an IPv4 literal on the kiosk (`inet_pton` handles numeric hosts).

## Safety

The default is read-only. Mutating calls are not sent, and the screen says so.

The only writes are `POST /api/run/cancel`, `POST /api/pause`, and `POST /api/pause/resume`. The URL is never compiled in. This tree does not change the daemon. Writes are tested only against the local stub (`tools/stub_server.py`, `test_http`, `test_data`, and `test_stop_latency`), not against a real controller.

## Raspberry Pi

Target board is a Pi 3B (armv7). The display is the legacy 800×480 32 bpp framebuffer: pass `--fb /dev/fb0`. With `--fb`, the process best-effort hides the VT cursor on `/dev/tty1` (override `ZK_TTY`) at start and shows it again on normal, `--duration`, SIGINT, and SIGTERM exit. A missing or unwritable tty logs one stderr line and continues. The client does not call `KD_SETMODE` / `KD_GRAPHICS` (a crash could leave the console dead). Host, memory, and fixture modes never open a tty. Touch is evdev. Autodetect prefers a device whose name contains `raspberrypi-ts`, and the client maps that device's reported absolute range onto the framebuffer with no rotation. For that panel the mapping is identity (screen pixels). `--touch-swap` and the flip flags stay off unless you pass them.

On the Raspberry Pi OS desktop image, X owns `/dev/fb0` and the touch device. Do not run this client on top of a live desktop. Boot-persistent install is opt-in; `make` does not enable it. See below.

## Display power

Idle dim and backlight-off run only with `--fb`, or when `ZK_POWER_FORCE=1` (test-only, so the memory display used by `tests/e2e.sh` can exercise the same state machine). Shot and fixture renders do not touch a backlight unless that variable is set. Nothing in this tree is applied to a panel; installing the unit on a board is a separate step.

| Flag | Env (used only when the flag is omitted) | Default |
|---|---|---|
| `--dim-after SEC` | `ZAN_DIM_AFTER_SEC` | 120 |
| `--off-after SEC` | `ZAN_OFF_AFTER_SEC` | 600 |
| `--dim-level N` | `ZAN_DIM_LEVEL` | 51 (about 20% of 255) |
| `--backlight DIR` | `ZAN_BACKLIGHT` | `/sys/class/backlight/rpi_backlight` |

Flags win, including an explicit `0`. Junk and values above the cap (seconds `1000000000`, level `255`) exit 2 with the usage text. A bad env value does the same.

`0` for `--dim-after` disables dimming and off. The directory is not opened. `0` for `--off-after` disables off only. If off is positive and not strictly greater than dim, off is disabled (not bumped by one second) so a bad pair cannot black the screen. Off is measured from the last touch, not from the moment of dim. The threshold is `idle >= N` seconds. A configured dim level of 0 is written as 1 when that would not raise the panel. The written dim is the minimum of that level (at least 1), the brightness read at startup, and the panel max. A dim never raises brightness. If the saved brightness is below 1, dimming writes nothing and wake or exit restores `max_brightness`. If the original could not be read, the dim write is min(level, max).

```
 blockers (watering, lockout, fault, unreachable, confirm modal)
 hold the idle clock at 0 and force ACTIVE immediately
        |
        v
    +--------+   no touch for dim_after    +--------+
    | ACTIVE | --------------------------> | DIMMED |
    +--------+ <-------------------------- +--------+
        ^          wake tap (see table)         |
        |                                       | no touch until off_after
        |                                       | from the last touch
        |                                       v
        |                                    +-----+
        +----------------------------------- | OFF |
                     wake tap                +-----+

 paused: ACTIVE may go to DIMMED, and never to OFF
         (an already-OFF screen moves to DIMMED)
 dim_after 0: stay ACTIVE, no sysfs and no FBIOBLANK
```

Wake taps. The choice is latched on the press, so the UI sees both the press and the release, or neither. A swallowed touch cannot leave the pointer stuck down. Samples with no finger down are not touches and do not reset the idle clock. `now < release+300 ms` is still swallowed; the sample at exactly +300 ms is delivered. STOP does not arm that guard.

| State when the finger goes down | Where | What the UI sees |
|---|---|---|
| ACTIVE | anywhere | PASS, idle clock resets |
| DIMMED | STOP | PASS, and the screen wakes to ACTIVE |
| DIMMED | anywhere else | SWALLOW until release + 300 ms, wake to ACTIVE |
| OFF | anywhere, including STOP | SWALLOW until release + 300 ms, wake to ACTIVE |

Blockers, any one of them: watering (run active, `current_station` set, or stations on), lockout, fault (`phase` Fault or `last_error`), unreachable (no fresh snapshot, offline, or needs-update), and an open STOP or pause confirm. A blocker that arrives while dimmed or off wakes the screen immediately. Pause is not a blocker: it may dim, and it must not turn the backlight off. A run starting, or a fault appearing, is a blocker, so it wakes the screen.

Backlight writes, in order:

- ACTIVE to DIMMED: `brightness` only, and only when the new level would not be brighter. A saved brightness below 1 writes nothing; the state still changes.
- Into OFF: `brightness` 0, then `bl_power` 1 if that file exists.
- Wake, and restore on exit: `bl_power` 0, then the saved brightness. A saved value below 1, or an original that could not be read, restores `max_brightness` (255 if that file cannot be read).

The original brightness and `max_brightness` are read in `zk_power_init` when dimming is enabled and the backlight directory is usable, and restored by `zk_power_shutdown`. They are not read again on the first dim. Shutdown that never dimmed or blanked writes nothing. Normal exit and SIGINT/SIGTERM both get there: the signal handler only sets a flag, and the main loop calls shutdown. A second shutdown does not write again.

`brightness` on the Pi panel is `root:root` mode `0644`. `prepare` in `deploy/zan-kiosk-run.sh` (root, `ExecStartPre=+`) runs `chgrp video` and `chmod g+w` on `brightness` and `bl_power` in the configured directory only (`ZAN_BACKLIGHT`, or `rpi_backlight`) when the file exists. It writes `0` to `bl_power` (panel on) in that same configured directory when the file exists; absence or a failed write is logged and ignored. If `BACKLIGHT` is unset it also writes `max_brightness` there, so the client restores full brightness. If `BACKLIGHT` is set, that fixed level is written to that same directory only, not to every backlight device, and it is the saved original. The unit's `ExecStopPost` is `reset-backlight` then `restore-cursor`. `reset-backlight` writes the panel max and `bl_power` 0 on that same directory. Rollback calls it best effort and still does not fail the desktop restore. This repository does not run those steps on a machine.

Failure modes, all non-fatal, each logged once on stderr:

- Directory missing: the state machine still runs. No crash. Dim does not write. Off uses `FBIOBLANK` (`FB_BLANK_POWERDOWN` / `FB_BLANK_UNBLANK`) only when the fb path is a real `/dev/fbN` and a blank function was supplied.
- `brightness` present but not writable: same fallback. The process keeps running.
- `bl_power` missing: that write is skipped. Brightness writes still happen.
- Dimming with no backlight and no real framebuffer: no-op. The state still changes.

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

`--switch` sets the default target to `multi-user.target`, disables `lightdm`, and enables `zan-kiosk`. `--now` also stops `lightdm` and starts the kiosk. The unit `Conflicts=` with `lightdm.service` and `getty@tty1.service`. It is `After=plymouth-quit.service` (and still `network-online.target` and `zanjerito.service`) so splash teardown cannot clear `/dev/fb0` under the first frame. It does not `Wants=` or `Requires=` Plymouth, and it does not `After=plymouth-quit-wait.service` (that wait unit can be `After=multi-user.target`, which would loop with `WantedBy=multi-user.target`). The client also redraws the screen once at about 2 s after the first frame and once more at about 5 s; those paints are skipped while dimmed or off.

It does not change the daemon, `config.json`, `zanjerito.env`, cron, or boot `config.txt` / `cmdline.txt`. It does not stop or restart `zanjerito.service`.

There is no `ExecStop=` that sends STOP or all-off. The kiosk is only a client. Valve safety stays in `zanjerito.service` (SIGTERM runs engine all-off, and `max_on` is enforced there). A STOP on every kiosk stop or crash-restart would cancel a scheduled or manual run, and it would depend on the kiosk being able to POST.

Writes stay off unless `ZAN_ALLOW_WRITES=1`. An existing install may already have writes on. The installed example leaves them off.

`prepare` (root, each start) writes `0` to `fbcon/cursor_blink` when that file exists, sends `ESC[?25l` and `ESC[9;0]` to `/dev/tty1`, and runs `setterm --blank 0 --powerdown 0 --cursor off` when `setterm` exists. It also `chgrp video` and `chmod g+w` on the configured backlight directory (not every device), writes `0` to `bl_power` there (configured path only), and writes either `max_brightness` or the optional `BACKLIGHT` level. Missing files are skipped. Idle dim and off are the client's job; see Display power above. `ZAN_DIM_AFTER_SEC=0` turns both off. After the client exits, `ExecStopPost` runs `reset-backlight` and then `restore-cursor`. Applying this on a board is a separate step from the files in this tree.

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

- Last run is omitted on the station sheet because it is not in `GET /api/kiosk`. Recommend adding `last_run` to that snapshot later.
- Pause-picker preview is still computed on the client from `/api/schedules`.
- `getaddrinfo` has no timeout. A hostname in `--api` can stall one request for as long as the resolver takes. Use an IPv4 literal on the kiosk.
- Hold to edit is not implemented (toast only).
- The client does not set the tty to KD_GRAPHICS. Kernel messages can still draw on tty1.

## Licenses

Fonts are SIL OFL 1.1 (`fonts/`). cJSON is MIT. stb_image_write is public domain (Unlicense) or MIT. LVGL is MIT and is fetched, not vendored. See [THIRD_PARTY.md](THIRD_PARTY.md).
