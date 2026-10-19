# Native kiosk client

A native C and [LVGL](https://lvgl.io/) client of the irrigation daemon's HTTP JSON API. It draws an 800×480 wall screen and talks to the daemon the same way the phone UI does. It is not a browser, and it is not a second source of truth: schedules, pause, and valves stay in the daemon.

## Design

Direction C. Flat sand page (`#FBECD6`), cream cards, 2 px borders, corner radius 16 (24 on the full-screen sheet). No gradients, shadows, blur, or animation. Teal (`#0F6E6C`) marks a running station, Resume, and Run. Plum (`#4E3F63`) is the paused hero. STOP is outlined cream with a 2 px `#B5361A` border when nothing is running, and filled `#B5361A` with a 4 px `#7C2411` bottom band only while watering. A manual run wins over every other hero state.

| Screen | What you see |
|---|---|
| Home | Status hero on the left. Station tiles on the right (2×2 up to 4 stations, 2×3 for 5–6; five leaves the last cell empty). Bottom bar: Schedules, Pause for rain or Resume when paused, and STOP |
| Run sheet | "Run {Station} for {N} min?" Stepper 1–60, default the station's usual minutes. Minus is not a target at 1. Run is what starts the valve |
| Run anyway | Same sheet while paused. The pause stays on. Run is teal |
| Running tile | A running station opens "{Station} is watering". The label is "WATERING NOW · {N} MIN LEFT". The body says the program goes on with the next station and to use STOP to end everything. Cancel closes the sheet and does not POST. The bar's STOP stays live |
| Stop all | Full sheet over the bar. Cancel or Stop all. Stop all posts `POST /api/run/cancel`. A manual run says "Ends the manual run on {Station} now. Schedules stay on." Nothing running says "Nothing is watering right now. Schedules stay on." |
| Overlap | The hero is a target. The sheet is read-only, with OK and "Fix on phone" |
| Schedules | Read-only list of programs from `/api/schedules`: name, days, start, steps and minutes, enabled or disabled. Next pages when there is more than one. Editing stays on the phone ("Fix on phone: Zanjerito › Schedules."). Home returns to the hero. STOP stays live |
| Offline | Hero says the controller cannot be reached. Tiles are not targets. Pause is disabled. Schedules stays available when a schedule list is already cached, and is disabled when it is not. STOP still opens the offline Stop all sheet |

A tile never starts or stops a valve on one tap. STOP always confirms.

## Layout

The framebuffer is 800×480, 32 bpp on the panel (the host renderer also writes 32 bpp PNGs). Every live target has a short side of at least 95 px, and live targets stay at least 12 px apart. `tests/test_layout` checks that.

| Region | Rect |
|---|---|
| Hero | 16,16 376×332 |
| Tile column | x=404 and x=600, each 184 wide (12 px gap). 4-up tiles are 160 tall; 6-up tiles are 102 tall |
| Schedules / Home | 16,360 120×104 |
| Pause / Resume | 148,360 244×104 |
| STOP | 404,360 380×104 |
| Schedules list | 16,16 768×332. Next, when there is more than one program, is 400,232 364×96 |

Touch counts on release inside the same rect, with a 300 ms debounce. A pressed target draws a darker 2 px border.

## Soil words

The second line of a tile is today's last-run line unless the controller's soil feature is configured (`GET /api/soil`, or percents already on `/api/kiosk`). Turning soil on does not move any other rect. A running tile shows "{n} min left" instead of soil. A missing input shows "Rate not measured", never 0%. A measured value shows "{n}% · Dry|OK|Wet". A stale value shows "{n}% · {age}d old".

Dry and Wet are display-only placeholder cuts, default below 30 and above 80. They do not change watering. Configure them in `/etc/default/zan-kiosk` or a small JSON file. See `deploy/zan-kiosk.default.example`.

```
ZAN_SOIL_DRY_LT=30
ZAN_SOIL_WET_GT=80
# ZAN_SOIL_THRESHOLDS=/etc/zan-kiosk-soil.json
```

The JSON may set global cuts and per-station cuts: `{"dry_lt":30,"wet_gt":80,"stations":{"lawn-a":{"dry_lt":25,"wet_gt":75}}}`. `ZAN_SOIL_DRY_LT` and `ZAN_SOIL_WET_GT` override the global cuts only.

## Stop

STOP opens "Stop all watering?" from Home or Schedules. Confirm posts `POST /api/run/cancel`. HTTP 2xx is not the acknowledgement. The stop stays pending until a later `/api/kiosk` snapshot shows nothing on (no running station and no manual run). Every pending state uses the same two-line headline, "Stop not" / "confirmed yet" (a drawn break, with a non-breaking space in "confirmed yet"). While the controller is reachable, including a POST still in flight, a slow acknowledgement, and a 2xx whose snapshot still shows water, the label is "STOP · WAITING FOR CONTROLLER" and the second line is "Retrying until all valves are off." The kiosk keeps re-sending the stop, one POST at a time.

When the controller is unreachable, before or after a 2xx, the label is "STOP · LAST UPDATE h:mm" and the second line is "Can't reach controller. Retrying…". The headline stays "Stop not" / "confirmed yet". "Watering stopped" is never shown on the 2xx alone. STOP stays filled through every pending state. Confirming again while a POST is in flight does not queue a second request. A confirm while nothing is in flight always posts.

A pending stop locks the screen the way offline does. Station tiles and Pause/Resume are not targets, and tiles are drawn unavailable. Pause uses the dim fill (`#F3E1C4`) and muted word (`#6E5A45`). STOP and Schedules stay live, including when no schedule cache is held.

With no touch for 60 seconds, Schedules and every sheet, including Stop all, return to Home. That timeout never confirms Stop all. `ZK_IDLE_HOME_MS` overrides that 60000 ms threshold for tests when the value is an integer from 1 to 600000. The unit predicate `zk_flow_idle_returns_home` stays on the 60000 ms constant.

Schedules lists two programs per page. Next is shown only when more than two are cached. The label is then "SCHEDULES · READ ONLY · 1–2 of N" (an en dash). A page with a single row prints "3 of 3" instead of "3–3 of 3". The offline label keeps "LAST UPDATE" and adds the same range. An empty list says "Nothing scheduled" and "Add a program on your phone". Next stays on the last page so the list can wrap.

The hero headline box stays inset 22 px and 332 px wide. The pending headline's second line is one authored line; this font draws it at 334 px, so that box grows by those pixels and still ends inside the hero. A name of 24 characters or fewer is never ellipsized. A longer name clamps at three lines with an ellipsis. The ends/next line stays. If that block meets the rain meter, the rain line is hidden before the ends/next line is dropped. The rain line is shown only when the 72 h total, rounded half up to 0.01 in, is at least 0.06 in. The words round half up to 0.1 in, so the line never says "Rain 0.0 in". The fill stays the raw inches, capped at 1 in. The rain track is 328 px, and "1 in" is right-aligned at the end of the track.

While paused, the hero is only "PAUSED FOR RAIN", "Until …", and "Tap a station to run it anyway". "Watering stopped" line 2, when the pause is still on, uses `next_effective_run` from `/api/kiosk` ("Next run {day h:mm AM}"). An indefinite pause with no next effective run says "Paused until you resume".

A retry tick repaints only the hero label.

Set `ZK_DIRTY_LOG=1` to log dirty rectangles, one line per event: `dirty event=<name> rects=x,y,wxh;...`. Opening or closing the Schedules screen logs `schedules` and repaints the top area (16,16 768×332) and the Schedules button (16,360 120×104). Pause and Resume repaint the hero and the Pause button (148,360 244×104). Losing the controller also repaints the Schedules button, because that is when it can dim.

## Build

LVGL is not vendored. The pin is `tools/lvgl.pin` (tag `v9.2.2`, commit `7f07a129e8d77f4984fff8e623fd5be18ff42e74`). `make fetch-lvgl` shallow-clones it into `.cache/lvgl` and checks that commit.

From `native-kiosk/`:

| Target | What it does |
|---|---|
| `make fetch-lvgl` | Clone the pinned LVGL tree if `.cache/lvgl` is missing |
| `make host` | `build/zan-kiosk-host` for this machine |
| `make arm` | Static 32-bit ARM hard-float binary `build/zan-kiosk-arm` via Docker (`debian:bookworm-slim`, `gcc-arm-linux-gnueabihf`, `-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard -static`) |
| `make test` | Host unit tests, including `test_layout`, `test_flow`, and `test_ui_text` |
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
| `--allow-writes` | Permit cancel, pause, resume, and station run. Default is read-only |
| `--fb PATH` | Framebuffer to open. There is no default device |
| `--fbshot FILE` | Read that framebuffer and write a PNG. Requires `--fb`. Does not start the UI. Exit 0 if any pixel is non-black, 3 if the frame is all black, 1 on error, 2 without `--fb` or combined with `--script`, `--shot`, or `--shot-all` |
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

`tests/fixtures/<scenario>/` holds `kiosk.json` (same keys as `GET /api/kiosk`). `schedules.json` feeds overlap, usual minutes, and the Schedules screen. Optional `soil.json` and `history.json` feed the soil line and last-run text. Older scenarios (`home-rain`, `home-nosoil`, `running`, `paused-manual`, and the rest) still exercise parsing and copy. Direction C shots use the `dc-*` fixtures (Lawn A, Lawn B, Beds, Trees, Drip, Side yard; program names "Front Summer" and "Drip · All year", plus the long example "Lawn A + B · Weekdays AM"). `dc-schedules-4` and `dc-schedules-6` are example programs only ("Lawn · Morning" at 6:10 AM and "Beds · Every 2 days", the second disabled).

`make shots` writes `build/shots/` with the Direction C names: `C-idle-4`, `C-idle-5`, `C-idle-6`, `C-watering-4`, `C-watering-4-longname`, `C-stop-slow-ack`, `C-stop-no-reply`, `C-stop-still-watering`, `C-stop-not-confirmed-4`, `C-stop-not-confirmed-offline`, `C-watering-6`, `C-paused-4`, `C-paused-6`, `C-nothing-4`, `C-nothing-6`, `C-overlap-6`, `S5-overlap-detail`, `C-offline-6`, `C-offline-stop-1-confirm`, `C-offline-stop-2-pending`, `C-offline-stop-3-stopped`, `S1-confirm-run`, `S1-confirm-run-min`, `S2-confirm-anyway`, `S3-running-tile`, `S3-running-tile-last`, `S3-running-tile-manual`, `S4-confirm-stop-all`, `S4-confirm-stop-all-manual`, `S4-confirm-stop-all-idle`, `C-schedules-stop-all`, `C-soil-mix-4`, `C-soil-mix-6`, `C-soil-watering-6`, `C-manual-run-4`, `C-schedules-1`, `C-schedules-2`, `C-schedules-3-p1`, `C-schedules-3-p2`, `C-schedules-4`, `C-schedules-6`, `C-schedules-empty`, `C-schedules-offline`. `C-paused-4` is the paused spec row only. `C-stop-slow-ack`, `C-stop-no-reply`, `C-stop-still-watering`, and `C-stop-not-confirmed-4` share the reachable pending copy ("Stop not" / "confirmed yet", "STOP · WAITING FOR CONTROLLER"). `C-stop-still-watering` is that copy while a 2xx is waiting on a snapshot that still shows water. `C-stop-not-confirmed-offline` and `C-offline-stop-2-pending` are the same headline with "STOP · LAST UPDATE h:mm" and "Can't reach controller. Retrying…". `C-stop-not-confirmed-offline` is the 4-station fixture after a 2xx. Home shots include the Schedules button. `C-schedules-2` is the two-program list (same cards as `C-schedules-4`). `C-schedules-3-p2` labels the single row "3 of 3". `C-schedules-empty` is "Nothing scheduled". `C-schedules-offline` keeps the list and labels "SCHEDULES · LAST UPDATE 6:42 AM".

`--shot`, `--shot-all`, and `--script` `shot:` use the memory display. They do not open a framebuffer and do not prove what the panel shows. `--fbshot` reads the framebuffer passed to `--fb`.

## Host tests

`make test` builds with `-Wall -Wextra -Werror` and ASan/UBSan when the compiler accepts them.

| Binary | Covers |
|---|---|
| `test_json` | Parse every fixture kiosk.json; nulls and missing optional; garbage, truncated, wrong types; 1e999 / huge arrays; `rain_strip.show` is the only visibility input; console cursor write to `ZK_TTY` |
| `test_logic` | Formatting (relative day, fire when, rain strip text, inches clamp); remaining pause-preview from `/api/schedules`; pause title and until text |
| `test_layout` | Every screen's live targets: short side ≥95, gaps ≥12, 1..6 stations, five stations leave the last cell empty. Home and Schedules targets are also ≥96, and the Schedules button stays 12 px from Pause and STOP |
| `test_flow` | Copy for every hero and sheet, including Schedules paging ("3 of 3" on a one-row page) and the manual and idle Stop all bodies; a tile tap does not POST a run or a stop, and OK/Cancel do not POST; Schedules, Home, and Next do not POST; a pending stop stays on "Stop not confirmed yet" until an all-off snapshot, including after HTTP 2xx with water still on and after the controller drops out; the rain line is hidden below 0.06 in (half up to 0.01 in) and its words round half up to 0.1 in; 60 s of no touch returns Schedules and every sheet, including Stop all, to Home without confirming, and `ZK_IDLE_HOME_MS` is the test override; a paused next run is the first fire at or after the pause ends; soil never shows 0% for a missing input; dirty rects match the repaint table |
| `test_ui_text` | Rendered strings, including the two-line pending headline, a 24-character name left whole, empty and offline Schedules, tile geometry identical with soil on and off, STOP ink gap, unspaced names, hero vs rain rects, soil label width |
| `test_http` | GET and POST to a localhost stub; read-only stop does not connect; cancel, pause, and resume paths; timeout; oversized body; non-`http` URL rejected |
| `test_data` | `/api/kiosk` poll and `/api/schedules` slow poll; 404 => NEEDS_UPDATE and no `/api/status`; stale after the stub stops; fixture load opens no socket; writes POST and repoll kiosk |
| `test_stop_latency` | STOP is posted while a GET is in flight; a repeated STOP or resume is one POST; identical pause bodies collapse; a different pause is still sent; the queue still overflows; read-only sends no POST |
| `test_boot` | Forced redraw after first frame is not due before 2 s, due once at 2 s, due once more at 5 s, never again; unsigned elapsed still works across a clock wrap |

`ZK_POLL_MS_STATUS` overrides the 2 second `/api/kiosk` poll inside those data tests. It is not a user-facing flag. `ZK_IDLE_HOME_MS` overrides the 60 s return-to-Home threshold inside the running UI (integer milliseconds from 1 to 600000). Junk keeps 60000. It is a test hook, not a user-facing flag.

`make e2e` runs `tests/e2e.sh` against `tools/stub_server.py` on an ephemeral localhost port. It checks read-only taps, pause / resume / cancel POSTs, a confirm that ignores taps outside its buttons, an unreachable API (exit 0), and exit 2 when no API is configured. A tile tap sends no POST. A running-tile tap, a tap where Stop station used to be, and OK send no POST. Opening Schedules, paging with Next, confirming STOP and cancelling back to the list, then Home, sends no POST. STOP still confirms, including after a 404 on `/api/kiosk`. A 2xx whose snapshot still shows water stays on "Stop not confirmed yet" ("STOP · WAITING FOR CONTROLLER") and keeps re-POSTing until the snapshot is all-off. A 2xx followed by the stub going unreachable stays on that same headline with "Can't reach controller. Retrying…" until the stub returns all-off. Opening Stop all and waiting past `ZK_IDLE_HOME_MS` returns Home, sends no `POST /api/run/cancel`, and does not show the pending hero. A tile tap during that pending stop sends no station POST. Display power is checked against a temp backlight directory (`ZK_POWER_FORCE=1`: dim, off, swallowed wake, STOP while dimmed, watering and fault stay on, pause dims but does not blank, SIGTERM and SIGINT restore brightness). The stub takes `--get-delay SEC`, `--post-delay SEC`, `--kiosk-404`, `--stay-watering`, and `--control PATH`. `--kiosk-404` makes `GET /api/kiosk` return 404 and leaves POSTs alone. `--control` is reread on each request: `watering=stay|clear` and `reach=up|down`. `reach=down` is `GET /api/kiosk` 404 and POST 503. Without `watering=stay` (and without `--stay-watering`), `POST /api/run/cancel` clears the in-memory kiosk snapshot after the post delay (the fixture file is not rewritten). `watering=clear` serves that all-off snapshot on the next GET. Each connection is its own thread so a slow GET does not hold a POST. Timestamps for logged writes are appended to `<log>.ts` as monotonic seconds; the request log lines themselves stay `METHOD path body`.

## Behaviour

In HTTP mode a poll thread GETs `/api/kiosk` about every 2 seconds. About every 30 seconds it also GETs `/api/soil`, `/api/history?limit=50`, and `/api/schedules`. It does not poll `/api/status` or `/api/stations`. HTTP 404 from `/api/kiosk` is unreachable on the hero (the client does not fall back to `/api/status`). A 404 from `/api/soil` or `/api/history` does not mark the controller unreachable. Transport, 5xx, and malformed kiosk bodies are unreachable. STOP, pause, resume, and run run on a second thread. That thread opens its own sockets and does not wait for a poll to finish, so STOP is not stuck behind a slow GET. After a write succeeds, `/api/kiosk` is polled again.

Pause for rain is one tap. The body is `{"until":"tomorrow_morning","reason":"rain"}`. Resume is one tap. A station tile opens the run sheet; only Run posts `POST /api/stations/{id}/run`. A running tile opens "{Station} is watering" (program label, minutes left, and one body line) and does not POST. There is no Stop station button. OK on that sheet does not POST. While a sheet is open, only its buttons, STOP, and (when paused and no stop is pending) Resume are live. Taps outside the sheet do nothing. Schedules opens a read-only list and does not POST. Next, when more than two programs are cached, only changes which pair is shown. Home closes the list. STOP on that screen still opens "Stop all watering?", and Cancel returns to the list. Sixty seconds with no touch returns Schedules or any open sheet, including Stop all, to Home, and does not confirm the stop. A manual run's Stop all body is "Ends the manual run on {Station} now. Schedules stay on." Nothing running says "Nothing is watering right now. Schedules stay on."

A second STOP is not posted while an identical cancel is in flight. After that request returns 2xx, a snapshot that still shows water stays pending ("Stop not" / "confirmed yet", "Retrying until all valves are off.") and the kiosk posts the stop again, one at a time. If the controller drops out after that 2xx, the headline stays and the second line becomes "Can't reach controller. Retrying…". A confirm while a POST is in flight does not add another request. Resume is treated the same way at the queue. A pause is skipped only when the same body is already queued or in flight. The queue holds 8 actions; one more reports overflow and sends nothing. The paused hero does not add a next-run time under "Until …". A name of 24 characters or fewer is not ellipsized. A longer headline that would wrap past three lines at the fixed 332 px column is ellipsized. If that block would cross the rain rect, the rain meter is hidden and the ends/next line stays. The meter is drawn only from 0.06 in (the 72 h total rounded half up to 0.01 in) and its text is rounded half up to 0.1 in.

Read-only mode does not open a socket for those POSTs. Fixture mode loads the scenario from disk: no threads and no socket.

Writes go out only with `--allow-writes`. Cancel sends nothing. While paused, Run anyway does not clear the pause.

A hostname in `--api` is resolved with `getaddrinfo`, which has no timeout. A slow resolver stalls that request. Use an IPv4 literal on the kiosk (`inet_pton` handles numeric hosts).

## Safety

The default is read-only. Mutating calls are not sent, and the screen says so.

The only writes are `POST /api/run/cancel`, `POST /api/pause`, `POST /api/pause/resume`, and `POST /api/stations/{id}/run`. The URL is never compiled in. This tree does not change the daemon. Writes are tested only against the local stub (`tools/stub_server.py`, `test_http`, `test_data`, `test_flow`, and `test_stop_latency`), not against a real controller.

## Raspberry Pi

Target board is a Pi 3B (armv7). Pass `--fb /dev/fb0`. The client supports 16 bpp RGB565 and 32 bpp XRGB8888, picked from the framebuffer's bits_per_pixel and channel layout, and exits 78 with a clear error on anything else. Other failures still exit 1. `--fbshot` still exits 1 for an unsupported format. With `--fb`, the process best-effort hides the VT cursor on `/dev/tty1` (override `ZK_TTY`) at start and shows it again on normal, `--duration`, SIGINT, and SIGTERM exit. A missing or unwritable tty logs one stderr line and continues. The client does not call `KD_SETMODE` / `KD_GRAPHICS` (a crash could leave the console dead). Host, memory, and fixture modes never open a tty. Touch is evdev. Autodetect prefers a device whose name contains `raspberrypi-ts`, and the client maps that device's reported absolute range onto the framebuffer with no rotation. For that panel the mapping is identity (screen pixels). `--touch-swap` and the flip flags stay off unless you pass them.

On the Raspberry Pi OS desktop image, X owns `/dev/fb0` and the touch device. Do not run this client on top of a live desktop. Boot-persistent install is opt-in; `make` does not enable it. See below.

## Display power

Idle dim and backlight-off run only with `--fb`, or when `ZK_POWER_FORCE=1` (test-only, so the memory display used by `tests/e2e.sh` can exercise the same state machine). Shot and fixture renders do not touch a backlight unless that variable is set. Nothing in this tree is applied to a panel; installing the unit on a board is a separate step.

| Flag | Env (used only when the flag is omitted) | Default |
|---|---|---|
| `--dim-after SEC` | `ZAN_DIM_AFTER_SEC` | 120 |
| `--off-after SEC` | `ZAN_OFF_AFTER_SEC` | 600 |
| `--dim-level N` | `ZAN_DIM_LEVEL` | 51 (about 20% of 255) |
| `--backlight DIR` | `ZAN_BACKLIGHT` | auto-detect: `rpi_backlight`, then `10-0045`, then first writable under `/sys/class/backlight` |

Flags win, including an explicit `0`. Junk and values above the cap (seconds `1000000000`, level `255`) exit 2 with the usage text. A bad numeric env value does the same. A bad `ZAN_BACKLIGHT` (missing, invalid, or too long) logs one line and falls back to auto-detect.

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

`brightness` on the Pi panel is `root:root` mode `0644`. `prepare` in `deploy/zan-kiosk-run.sh` (root, `ExecStartPre=+`) runs `chgrp video` and `chmod g+w` on `brightness` and `bl_power` in the configured directory only (`ZAN_BACKLIGHT`, or auto-detect: `rpi_backlight`, then `10-0045`, then the first writable device) when the file exists. On Bookworm with KMS, the official 7-inch DSI panel backlight appears as `10-0045`; `brightness` may already be group `video`, `bl_power` is root-only, and `prepare` handles it. It writes `0` to `bl_power` (panel on) in that same configured directory when the file exists; absence or a failed write is logged and ignored. If `BACKLIGHT` is unset it also writes `max_brightness` there, so the client restores full brightness. If `BACKLIGHT` is set, that fixed level is written to that same directory only, not to every backlight device, and it is the saved original. The unit's `ExecStopPost` is `reset-backlight` then `restore-cursor`. `reset-backlight` writes the panel max and `bl_power` 0 on that same directory, unless `EXIT_STATUS` is 78 (unsupported framebuffer), when it writes brightness 0 and `bl_power` 4. Rollback calls it best effort and still does not fail the desktop restore. This repository does not run those steps on a machine.

Failure modes, all non-fatal, each logged once on stderr:

- Directory missing: the state machine still runs. No crash. Dim does not write. Off uses `FBIOBLANK` (`FB_BLANK_POWERDOWN` / `FB_BLANK_UNBLANK`) only when the fb path is a real `/dev/fbN` and a blank function was supplied.
- `brightness` present but not writable: same fallback. The process keeps running.
- `bl_power` missing: that write is skipped. Brightness writes still happen.
- Dimming with no backlight and no real framebuffer: no-op. The state still changes.

## Boot-persistent kiosk (full kiosk mode)

`deploy/` can install a systemd unit that starts `/opt/zanjerito/zan-kiosk` on `/dev/fb0` at boot and keeps the desktop off that framebuffer. Copying the files does not enable the unit and does not change the boot target. The procedure, the `--fbshot` panel check, and the rollback commands are in [docs/native-kiosk-boot.md](../docs/native-kiosk-boot.md).

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

`prepare` (root, each start) writes `0` to `fbcon/cursor_blink` when that file exists, sends `ESC[?25l` and `ESC[9;0]` to `/dev/tty1`, and runs `setterm --blank 0 --powerdown 0 --cursor off` when `setterm` exists. It also `chgrp video` and `chmod g+w` on the auto-detected or `ZAN_BACKLIGHT` directory (not every device), writes `0` to `bl_power` there (that path only), and writes either `max_brightness` or the optional `BACKLIGHT` level. Missing files are skipped. Idle dim and off are the client's job; see Display power above. `ZAN_DIM_AFTER_SEC=0` turns both off. After the client exits, `ExecStopPost` runs `reset-backlight` and then `restore-cursor`. Exit 78 turns the backlight off; any other status restores it. Applying this on a board is a separate step from the files in this tree.

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

- A stale soil line uses the soil document's `updated_at` / `et_stale`. There is no separate rain-input timestamp on the existing API.
- `getaddrinfo` has no timeout. A hostname in `--api` can stall one request for as long as the resolver takes. Use an IPv4 literal on the kiosk.
- The client does not set the tty to KD_GRAPHICS. Kernel messages can still draw on tty1.

## Licenses

Fonts are SIL OFL 1.1 (`fonts/`). cJSON is MIT. stb_image_write is public domain (Unlicense) or MIT. LVGL is MIT and is fetched, not vendored. See [THIRD_PARTY.md](THIRD_PARTY.md).
