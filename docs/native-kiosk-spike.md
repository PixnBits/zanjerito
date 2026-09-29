# Native kiosk client: spike write-up

Status: SPIKE on branch `spike/native-kiosk`. Not for merge. Nothing here changes the daemon,
its config, or any service on the device.

## Goal

A compiled, native touchscreen client for a 3.5" 800x480 capacitive panel on a Raspberry Pi 3B
(armv7, 32-bit). It is a client of the daemon's HTTP JSON API, like the web UI. It is not a browser
and holds no state of its own. STOP dominates, edits are tucked away.

## What the device has (read-only inspection)

- Pi 3B, 4 cores, ~920 MB RAM, kernel 5.10 armv7l, Raspberry Pi OS (buster) **desktop** image, not Lite.
- The panel is driven by the firmware framebuffer (`bcm2708_fb`, `/dev/fb0`, 800x480, 32 bpp, stride 3200).
  There is no fbtft or SPI overlay in the boot config, and no `/dev/dri` (no KMS/DRM).
- Touch is an evdev multitouch device (`raspberrypi-ts`): X 0-799, Y 0-479, 10 slots. Rotation 0, so
  raw touch coordinates equal framebuffer pixels: no swap, flip or calibration needed.
- Xorg (fbturbo driver) owns fb0 and the touch device through lightdm/openbox/lxpanel. Any client that draws
  to fb0 or reads touch directly would fight the desktop unless the session is stopped or replaced.
  The spike did not stop it (out of scope), so **nothing was drawn on the real panel**.
- Installed: gcc, python3, mesa GLES/EGL runtime, SDL2 runtime (no headers). Not installed: Go, evtest.
- The daemon is ~11 MB RSS. The desktop session is ~250 MB RSS.
- The build host has no arm C cross-compiler. `gcc-arm-linux-gnueabihf` was obtained inside a throwaway
  Docker container (Debian bookworm) and the C binary was linked fully static, so it does not depend on the
  device's older glibc.

## Options tried

| | (a) Go + Ebitengine | (b) Go, own software renderer + fb0 + evdev | (c) C + LVGL 9.2 (fb + evdev) |
|---|---|---|---|
| Cross-build | `CGO_ENABLED=0 GOARCH=arm GOARM=7` builds (10.9 MB) | builds, 6.9 MB stripped (9.7 MB default) | Docker arm gcc, static, 0.65 MB |
| Runs on device | **No**: `symbol lookup error: pthread_attr_getstacksize` (purego fakecgo on glibc 2.28), same with the `fakecgo=-std` flag. It also needs X11+GL, and the fbturbo X driver has no GPU GL. Stopped there. | yes | yes |
| Code | ~10 lines (empty game) | ~2.4k lines non-test (incl. model, next-run, HTTP client, evdev, fb ioctl) | ~1.1k lines C (hand-rolled HTTP/JSON, fixture parser) |
| Idle RSS | not measured | 8.8 MB | 1.9 MB |
| Idle CPU (fixture, no animation) | n/a | ~0.5 % | ~0.1 % |
| Live poll (GET only, 2 s) CPU / RSS | n/a | ~4 % / 13.5 MB | ~0.4 % / 1.9 MB |
| Animating (30 fps target) | n/a | full-frame software redraw, ~40 ms/frame on Pi 3B: ~376 frames in 15 s (~25 fps) using ~100 % of one core (needs dirty rects) | 444 frames in 15 s (~30 fps) at ~19 % of one core (~6 ms/frame, derived) |
| Cold start to first frame | n/a | ~330-360 ms from exec (47-77 ms after main starts) | ~11-20 ms |
| Touch-to-frame | see note | see note | see note |

All numbers are from a real Pi 3B, but with the **memory sink** (frame rendered and converted, then discarded),
not the real panel. Numbers are one 15 s run each (idle-gated: daemon Idle, nothing on, not paused, no lockout,
outside the morning window), so treat them as approximate.

Touch latency note: the tools log the kernel timestamp of each evdev event and compare it to the end of the
next frame write. That could not be exercised on hardware because nobody touched the panel during the run and
the desktop owns the panel. The upper bound is therefore the frame cost: (b) ~40-75 ms at full-frame redraw,
(c) ~6-10 ms (derived from CPU, not measured). Option (b) should be measured again with dirty-rect redraw and the real fb sink.

Option (c) via Slint or Rust was not attempted: no arm Rust target or linker on the build host, and it would
need the same Docker C toolchain plus a Rust arm target. LVGL was the cheaper representative of that class.

## Ease of writing the Home screen

- Go (b): the layout is a pure function with unit tests, which made the touch-target checks easy (STOP >= 160 px,
  no hit overlap). Text and rounded rects need some renderer code (embedded Go fonts). Everything (model, next-run,
  API, tests) is one language and one `go test`, same as the daemon.
- LVGL (c): the widgets, styles, pressed states and font rendering come free and are small and fast, but there is no
  JSON or HTTP client, so the spike hand-rolled a naive one, and the build needs a C cross toolchain. Tests are harder.

## Layout in physical size

At ~267 px/inch: STOP is 220 px tall (~0.82 in), Pause 128 px (~0.48 in), corner menu button 96 px (~0.36 in).
Tiles are ~2 columns. Tests enforce STOP >= 160 px, STOP >= 1.5x Pause, all targets >= 80 px, no overlapping hit areas.

## Safety in the spike

- The client is read-only by default: `ReadOnlyActions` logs and makes no request. Writes need an explicit
  `-allow-writes` flag that is not to be used on a live daemon.
- The STOP hit-test is proven against a local stub server in tests only.
- Only `GET /api/status` (and fixtures) were read from the real daemon.

## Daemon API gaps for a native client

- No "next run" in `/api/status`: the client fetches `/api/schedules` and computes it (implemented here, and the web UI does the same).
- `/api/events` (SSE) payload uses Go field names (`Phase`, `CurrentStation`, `StationsOn`) while `/api/status`
  uses lowercase keys, and lacks `now`, `timezone`, `lockout`. Fix or document. It is a plain 1 s ticker, not change-driven.
- No compact combined endpoint. A native tile screen needs status + stations (title, colour) + schedules. Suggest
  `GET /api/kiosk` (or `?fields=`) returning tiles (id, title, colour, on, elapsed/remaining), status, pause, rain strip
  and next run in one small document.
- No per-station elapsed or remaining time, and no planned run summary while running, so a progress bar cannot be real.
- `/api/stations` returns hardware pin fields the client does not need.
- Polling `/api/status` is ~0.5 KB and 7-20 ms on the LAN, so every 2 s is fine. SSE saves little at a 1 s tick but is worth
  it once SSE is change-driven and consistent.
- STOP is `POST /api/run/cancel`. Pause is `POST /api/pause` with `duration_sec`, `days`, `until` or `indefinite`, plus `reason`.
  Resume is `POST /api/pause/resume` or `DELETE /api/pause`. There is no auth (LAN trust).
- The daemon listens on the LAN address only, so a client on the same device must use that address, not loopback.

## Recommendation

Prefer **(b)** for the first PR if the client must live in this repo and ship as one Go binary, provided the renderer is
made incremental (dirty rects, animate only what changes) so animation stays under ~10 % of a core. Idle cost is already
low. Switch to **(c) LVGL** only if animation quality or the memory/CPU budget matters more than one toolchain: it is
about 5x smaller in RAM, 10x smaller in binary, and several times cheaper per frame. Ebitengine is not viable on this device.

The first decision that is not about the client: the desktop session currently owns the panel and touch. A kiosk
needs either a dedicated boot target/service that replaces the desktop (a Lite-style boot), or an X11 client path.
This spike did not change it.

## Suggested PR breakdown

1. Client skeleton + Home: `cmd/zan-kiosk`, `internal/kiosk` (model, layout, render, evdev, fb), fixtures and tests. Read-only first, STOP/Pause behind a confirm.
2. Schedules view and edits: tucked behind the corner menu, using `PUT /api/schedules/{id}`. Optional compact endpoint in the daemon.
3. systemd unit + boot target: start the client instead of the desktop, an ExecStop that issues a STOP/all-off, restart policy, and backlight timeout.

## Reproduce

```
gofmt -l . && go vet ./... && go test -race -count=1 ./...
GOOS=linux GOARCH=arm GOARM=7 CGO_ENABLED=0 go build -ldflags="-s -w" -o zan-kiosk ./cmd/zan-kiosk
zan-kiosk -fixture internal/kiosk/testdata -fixture-status running -sink png -png out.png -duration 1s -stats
```

`spike/native-kiosk-lvgl/` holds the C prototype (`build.sh` expects an LVGL 9.2 checkout in `lvgl/` and an arm gcc).
