# Zanjerito — Product Requirements Document

**Status:** Draft v0.5 · 2026-10-14
**Repo:** [PixnBits/zanjerito](https://github.com/PixnBits/zanjerito)
**Trunk:** `main`. The bash MVP at tag `bash-mvp` is the rollback reference.
**Companions:** [decisions.md](./decisions.md) · [architecture.md](./architecture.md) · [pin-map.md](./pin-map.md) · [ui-directions.md](./ui-directions.md) · [home-assistant.md](./home-assistant.md) · [native-kiosk-boot.md](./native-kiosk-boot.md)
**Codename etymology:** *Zanjero* is Spanish for “ditch rider.” Since the late 1800s, zanjeros opened head gates so water reached fields and faucets. Zanjerito is a small, reliable local zanjero for a home drip system.

---

## 1. One-liner

Zanjerito is a **Raspberry Pi–hosted irrigation controller**: a small local Go runtime that drives 24VAC valve relays over GPIO, runs schedules reliably, and exposes a simple website so the household can see and change what is watering — without depending on a flaky commercial cloud box.

---

## 2. Why this exists

The commercial controller failed as a closed update channel. The Wi-Fi daughter board could not be upgraded, so software updates died with the manufacturer's hardware. The valves still worked.

Swapability is the point:

- Swap the Pi.
- Swap the relay board.
- Update software on a channel that is not "buy their next board."

This house comes first. The guts stay copyable: the pin map is config, so someone else can point the same software at their board. A fleet product waits until this household can change a schedule without SSH.

---

## 3. Product ethic

1. Fail off, never fail on.
2. The household operates the controller without SSH, cron, or the owner at the keyboard.
3. Internet is optional for watering. Forecast and rain feeds are add-ons, and they are not a boot requirement.
4. One GPIO writer.
5. Honesty over catch-up. A missed or collided start is skipped and logged. The controller does not water later to make up for it.
6. Rollback is a feature. Tag `bash-mvp` stays the rollback reference.
7. Parts are replaceable. Software must not couple itself to one Pi, one relay board, or one vendor radio.

**Boot policy.** After power loss, or a reboot in the middle of a run, every output is off. The half-finished itinerary stays unfinished. The scheduler re-arms the next wall-clock start.

---

## 4. Users

| Who | Need |
|---|---|
| Household operator | See what is watering and what is next. Change schedules without the owner. The box is not a black box. |
| Owner/admin | Pin map, deploy, rollback to the bash MVP, safety caps, and the auth policy. |
| Wall/kiosk user | The same controller on the installed touchscreen. Large tap targets. STOP is dominant. |
| Future copier | Pin map as config, plus these docs. Not a first-class customer until this house works. |

---

## 5. Status (as of 2026-10-14)

Shipped work below is on `main`. Pull-request numbers are the merge record. Cutover is done: the Go daemon actuates, and `bash-mvp` is the rollback reference.

### Shipped

- Go daemon replaced the bash scripts: engine, store, scheduler, API, phone UI, and systemd. Dual-run came first, then cutover (#4–#11, #14).
- Fail-off GPIO: the real gpiocdev driver, with every relay inactive on release (#15, #17).
- Seasonal date-window programs and a schedule editor in the web UI (#19).
- Style A UI (#20, #21).
- Pause and delay for rain, with durations (#22, #25). STOP asks "Stop all watering?" first, then immediate all-off.
- Run history, `GET /api/history`, and a Recent runs card (#28). A record is written after outputs are off and the run is no longer busy, so the all-off after confirm is immediate.
- Automatic rain pause (#30), the first slice of #26. The poller retries after a failed fetch, capped at the normal interval (#41). Schedules still run when the feed is absent.
- Display-only soil estimate and a rain strip on Home (#31, #33, #34). The estimate does not decide to water.
- Native C/LVGL framebuffer kiosk at boot, with one-line rollback, a `GET /api/kiosk` snapshot, display dim and off with a wake-tap guard, backlight auto-detect, RGB565 support, and framebuffer hardening with exit-78 recovery (#35–#39, #42–#46, #48, #50). See [native-kiosk-boot.md](./native-kiosk-boot.md).
- The API retries its listen bind when the address is late. Schedules keep running through that wait (#40).
- Hardware: a printable screen mount (#1) and a bill of materials (#47).
- Home Assistant Phase 1: read-only `GET /api/ha` and RESTful sensor docs (#51–#55). See [home-assistant.md](./home-assistant.md). The daemon never dials Home Assistant.

### In progress / experimental

Home Assistant MQTT discovery is under evaluation (#60). Pull request #61 is an experimental test. It is not merged, and it is not part of `main`. The shipped integration remains the read-only HTTP snapshot.

### Remaining for v1

- **Auth and write gating** (#62), which reopens D7. The LAN bind stays. Writes from a LAN browser (schedules, stations, the pin map, pause, a manual run longer than a short preset, and driver mode) need a local household account with a hashed password. First boot creates the admin. The wall panel is a trusted console: physical access to the box is full access, including STOP, and there is no logged-out watch-only kiosk mode. Anonymous LAN status defaults on and the setting persists across power cycles in the atomic JSON store. The `max_on_sec` cap (default 900) stays in force regardless of auth. There is no cloud identity provider.
- **Time and discovery:** deploy docs call out NTP time sync and the configured timezone, and the kiosk footer shows how to reach the controller (its `.local` name or LAN address). Not started.
- **Soil water balance and evapotranspiration** adjustment, including catch-can calibration (#26). Rain pause has shipped. The balance has not.
- **Home Assistant Phase 2 controls** (stop, pause, manual run) (#49).
- **Logs and history:** RAM-disk logs (#56), history retention of about 2.5 years (#57), CSV and JSON export (#58), and docs for using Home Assistant as a history archive (#59). Today's log is the short window in D10: the newest 200 runs from the last 60 days.
- **Installable PWA** (#27) is parked.
- The pre-native browser kiosk drafts (#23, #24) were closed in favour of the native kiosk.

### Later

- Sliding seasonal ramp. Optional `ramp_start_on` / `ramp_end_on` moves the start time in a straight line between two clocks. No spline.
- Flow or current sensing.
- A municipal restriction feed. Honor restrictions in software when that feed exists.
- Multi-site and remote access beyond the LAN.
- Physical front-panel inputs (the original controller's dial and buttons), a stretch goal. Nothing is wired to the Pi today and no GPIO inputs are reserved. When it happens it is a second pointer into the same engine, never a second GPIO writer.

Still out of scope: a cloud account, a multi-site product, and a GraphQL server in the Go binary. Watering runs with no internet.

---

## 6. Seasonal programs

A program is a named itinerary (stations and durations), a clock start, weekdays, and optional `starts_on` / `ends_on`. Those dates use the controller's configured local timezone (IANA zone; no-DST zones avoid DST edge cases). Enable and disable sit beside the window.

Seasons are several programs with date windows. The household does not keep a lettered slot in their head and flip it by hand.

Example, with no clock times and no calendar dates: a summer program that starts earlier (evaporation) and a winter program that starts later (freeze risk).

If a second program fires while one is already watering, the new start is skipped and logged (D13). Itineraries are not interleaved, and the skipped start is not watered later in the day. A manual run may still preempt a schedule after an explicit warning.

Date windows that overlap and share a start time are a config error. The schedule editor should refuse to save that pair.

A start time that creeps across a shoulder season is the later ramp in section 5. It is not part of the window schema that has shipped.

---

## 7. Observability

Home shows now, next, and last: program name, station, and when.

There is no flow meter and no current sensor. A GPIO line driven active means the relay was commanded on. It is not proof that water left the emitter.

Rain totals and the soil bars are display. Automatic rain pause can hold watering when a feed is configured and usable. A missed fetch does not invent a skip, and it does not block the scheduler.

---

## 8. Hardware

- **Board:** Raspberry Pi 3 (armv7), typically `/dev/gpiochip0`, about 1GB RAM.
- **Relays:** active-low. Logic high means the valve or the PSU enable is off.
- **Power:** a 24VAC supply-enable output, separate from the station valves.
- **Channels:** the pin map in config. The human table is [pin-map.md](./pin-map.md).
- **Clock:** the controller's configured local timezone (IANA zone; no-DST zones avoid DST edge cases). All schedule math uses that zone.
- **Budget:** one static binary (`CGO_ENABLED=0`). Idle RSS target is under 20MB. Production on the Pi does not run Node.

`go-gpiocdev` is the GPIO driver (Linux character device). Install, the systemd unit, and the early-boot relay notes are in [deploy.md](./deploy.md).

Default sequencing is a 2-second overlap so the next station opens before the previous one closes, with at most two stations on at once. Isolation stays available for commissioning or a weak transformer. Parity with the bash golden program is that program's stations and durations, not the dead time between stations in the bash scripts.

---

## 9. Capabilities

| Capability | MVP | v1 | Status |
|---|---|---|---|
| List stations | ✓ | ✓ | Shipped |
| Manual run, cancel, and STOP | ✓ | ✓ | Shipped. Confirm, then immediate all-off |
| Manual preempt of a schedule, with a warning | ✓ | ✓ | Shipped |
| Schedules (clock, weekdays, minute itinerary) | ✓ | ✓ | Shipped |
| Several programs, enable and disable | ✓ | ✓ | Shipped |
| Seasonal `starts_on` / `ends_on` | — | ✓ | Shipped (#19) |
| Overlap default, isolate in config | ✓ | ✓ | Shipped |
| Config persists (atomic JSON) | ✓ | ✓ | Shipped |
| Dry-run, fake, and lockout GPIO | ✓ | ✓ | Shipped |
| Status and SSE | ✓ | ✓ | Shipped |
| Phone UI on the LAN | ✓ | ✓ | Shipped. Style A (#20, #21) |
| Wall / kiosk | — | ✓ | Shipped. Native C/LVGL client |
| systemd and a static binary | ✓ | ✓ | Shipped |
| Dual-run, then cutover | ✓ | ✓ | Shipped. Daemon actuates on `main` |
| Pause and delay for rain | — | ✓ | Shipped (#22, #25) |
| Run history, `/api/history` | — | ✓ | Shipped (#28) |
| Automatic rain pause | — | ✓ | Shipped (#30, #41) |
| Soil estimate and rain strip | — | ✓ | Shipped, display only (#31, #33, #34) |
| Home Assistant read-only `GET /api/ha` | — | ✓ | Shipped (#51–#55) |
| Local accounts and write gating | — | ✓ | Open (#62). Reopens D7 |
| Soil water balance / ET | — | ✓ | Open (#26) |
| Home Assistant controls | — | ✓ | Open (#49) |
| Log retention, export, archive notes | — | ✓ | Open (#56–#59) |
| Installable PWA | — | — | Parked (#27) |
| Sliding seasonal ramp | — | — | Later |
| Flow or current sensing | — | — | Later |
| Municipal restriction feed | — | — | Later |
| Multi-site or remote access | — | — | Later |
| Physical front-panel inputs | — | — | Later (stretch) |

---

## 10. UI

The phone UI on `main` is Style A: status first, station chips, and a STOP control that stays on screen. A manual run may preempt a schedule after an explicit warning. STOP asks "Stop all watering?" first, then turns every valve off immediately.

The wall panel is the native framebuffer client in [native-kiosk-boot.md](./native-kiosk-boot.md). It is a client of the daemon. Schedules, pause, and valves stay in the Go process. If the API is down, the panel says it cannot reach the controller. Rollback to the desktop is one command. Writes from the panel are opt-in.

Direction D, the earlier phone layout, is recorded in [ui-directions.md](./ui-directions.md).

---

## 11. Success criteria

Cutover has happened. The checks below are the MVP bar, marked where `main` already meets them.

- [x] The runtime builds as a static binary for the dev machine and for the Pi (armv7, and arm64 where that is the board).
- [x] Dry-run and lockout exercise schedules without energizing relays.
- [x] On the Pi, gpiocdev is the driver. Release and process death leave relays inactive (#15, #17).
- [x] The bash golden program's stations and durations ran on the Go daemon before bash stopped actuating.
- [x] Config survives reboot. After power loss or a mid-run reboot, outputs are off, the half-finished itinerary does not resume, and the next start is armed. The clock is the controller's configured local timezone (IANA zone; no-DST zones avoid DST edge cases).
- [x] systemd starts the daemon on boot. Stop and signals leave the valves off.
- [x] From a phone on the LAN, someone can see status, hit STOP, start a manual run, and edit a schedule without SSH.
- [x] Dual-run finished and the flip is done. Tag `bash-mvp` remains the documented rollback.
- [ ] Idle RSS stays under 20MB. That is still a target. This document does not record a measurement.

---

## 12. Runtime, in short

See [architecture.md](./architecture.md).

- One Go daemon. REST, JSON, and SSE. Atomic JSON for the pin map, stations, and programs. `GET /api/events` is the live stream.
- The UI never toggles pins. The engine is the only GPIO writer.
- Start, stop, signals, and faults turn all stations off, then the 24VAC enable off.
- `max_on_sec` defaults to 900. A session, compromised or not, cannot hold a station on all day. Auth does not replace the cap.
- The kiosk and Home Assistant are clients. Stopping the screen does not stop the valves. Stopping the daemon does.

---

## 13. References

- Tag `bash-mvp`: the bash MVP, kept as the rollback reference.
- [decisions.md](./decisions.md) — decision log. v1 reopens D7.
- [architecture.md](./architecture.md) — layers, state machine, API sketch.
- [pin-map.md](./pin-map.md) — this board's pin table and example config.
- [deploy.md](./deploy.md) — systemd install and cutover notes.
- [ui-directions.md](./ui-directions.md) — earlier UI options.
- [native-kiosk-boot.md](./native-kiosk-boot.md) — framebuffer kiosk at boot.
- [home-assistant.md](./home-assistant.md) — Phase 1 RESTful sensors.
