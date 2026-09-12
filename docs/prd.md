# Zanjerito — Product Requirements Document

**Status:** Draft v0.4 · 2026-09-12  
**Repo:** [PixnBits/zanjerito](https://github.com/PixnBits/zanjerito)  
**Working branch for this rewrite:** `rewrite/vision` (keeps `fancy-vibes` / `mvp-bash` intact as reference)  
**Companions:** [decisions.md](./decisions.md) · [architecture.md](./architecture.md) · [pin-map.md](./pin-map.md) · [ui-directions.md](./ui-directions.md)  
**Codename etymology:** *Zanjero* is Spanish for “ditch rider.” Since the late 1800s, zanjeros opened head gates so water reached fields and faucets. Zanjerito is a small, reliable local zanjero for a home drip system.

---

## 1. One-liner

Zanjerito is a **Raspberry Pi–hosted irrigation controller**: a small local Go runtime that drives 24VAC valve relays over GPIO, runs schedules reliably, and exposes a simple website so the household can see and change what is watering — without depending on a flaky commercial cloud box.

---

## 2. Why rewrite now

The Node/`fancy-vibes` stack was the right trade for **human** authoring before strong coding LMs: familiar TS, Fastify, GraphQL, React. The target has shifted.

| Then | Now |
|---|---|
| Optimize for Nick writing and maintaining the code | Optimize for **machine efficiency on the Pi** (CPU, RAM, flash, boot, idle) |
| GraphQL because it was interesting | **REST + JSON + SSE** — small, clear, cheap on-device |
| UI as a first sketch | **Direction D hybrid** with stakeholder feedback; current UI is prototype only |

`mvp-bash` remains the **production truth today**. `fancy-vibes` remains the **behavioral and domain reference** (stations, power enable, schedules, overlap). Neither branch is the destination.

---

## 3. Problem

1. Commercial drip controllers fail in ways that are hard to fix (Wi‑Fi brick, silent valve failures).
2. Bash scripts work but are awkward to schedule, observe, and safely change.
3. A Node GraphQL + React stack on a Pi 3 is heavier than this job needs, and the current UI/API are not the long-term product.
4. Confidence to cut over requires: real GPIO (not fakes), durable config, a prod run path (systemd), and parity with today’s watering **stations and durations**.

---

## 4. Hardware (now)

- **Board:** Raspberry Pi 3 (BCM2837), typically `/dev/gpiochip0`, ~1GB RAM.
- **Confirm later:** `uname -m` (`armv7l` vs `aarch64`) and `gpiodetect`.
- **Relays:** active-low; logic HIGH ≈ valve/PSU **off** (match bash).
- **Channels:** Front West / North / South + Drip + 24VAC PSU enable — see [pin-map.md](./pin-map.md).
- **Timezone:** `America/Phoenix` (no DST) for all schedule math.

**Resource budget (v1 target):** idle RSS **< 20MB** for the daemon; static binary; no Node on the Pi for prod.

---

## 5. Goals / non-goals

### Goals — MVP (confident Pi cutover of runtime + phone UI)

- Drive the existing relay board / 24VAC enable and station channels **correctly and safely** (fail-safe off on start, crash, and shutdown; inactive-on-release; `ExecStop` force-off).
- Run **named schedules** (clock + weekday starts + ordered station itinerary with durations in minutes).
- Default sequencing: **overlap ~2s** (anti water-hammer); isolation mode available in config for commissioning / weak transformer.
- Expose a **local phone-friendly website** on the LAN: status, STOP, manual run (with preempt warning), schedule/station edits.
- Persist configuration across restarts (stations, schedules, pin map) via **atomic JSON**.
- Ship as a **single lightweight Go binary** + embedded static UI; REST + JSON + SSE API.
- Document install: cross-compile for arm/arm64 matching `uname -m`, systemd unit, dry-run / lockout mode.
- Prove **parity** with today’s `front.sh` stations + durations (Front West 4 / Front North 8 / Front South 8) before retiring bash as primary. Parity is **not** bash dead-time between stations.
- Dual-run with bash (daemon logs intent; bash actuates) before flip (D9).
- Schedule collision: if a second schedule fires while watering, **skip + log** (D13); manual preempt keeps its warning dialog.

### Goals — v1 (after MVP)

- **Seasonal date windows** (`starts_on` / `ends_on` in `America/Phoenix`) — e.g. winter grass in October. Multi-schedule is first-class; enable/disable + duplicate are enough for MVP.
- **Wall / `screen-mount-part` screen** — same UI as phone, bigger type, STOP dominant, edits tucked (`?mode=kiosk` or equivalent). Not a second app.
- Field-confirm overlap vs isolation one evening (listen at valves, transformer heat).

### Non-goals (MVP / v1)

- Cloud account, phone push vendor lock-in, or mandatory internet.
- Multi-site fleet / multi-tenant SaaS.
- ML weather/ET optimization (interesting later; not cutover).
- Keeping GraphQL, React, or the `fancy-vibes` page structure.
- Pixel-perfect reuse of the current UI.
- Wall screen as an MVP deliverable (it is v1).

### Later (out of v1, keep in mind)

- Weather / soil / rain skip.
- Auth beyond LAN trust (or simple local password) if exposed wider.
- Mobile-polished PWA beyond phone-on-LAN.
- Metrics / history graphs beyond “what ran” (SQLite history = D10).

---

## 6. Users / stakeholders

| Persona | Need |
|---|---|
| Household operator (primary) | Know what’s on, STOP, start/stop a station, change schedules without SSH |
| Nick (builder / ops) | Safe deploy, easy rollback to bash, clear logs, low Pi resource use |
| Wall-screen user (v1) | Glanceable status + dominant STOP; edits optional |

---

## 7. Current system (as-is)

### 7.1 Production: `mvp-bash`

- `startup.sh` — configure pins as outputs, default **off**.
- `channel.sh <wiringPiChannel> <minutes>` — drop 24VAC, all off, enable 24VAC, run one channel, then shut down.
- `front.sh` — **golden program / parity fixture:** Front West 4m, Front North 8m, Front South 8m. Drip channel exists but is not in this program.
- Paths assume `/home/pi/zanjerito/`. WiringPi channel numbers in scripts.

### 7.2 Reference app: `fancy-vibes`

- Fastify + Mercurius (GraphQL) + React/urql; Node 22; `rpi-gpio` intended but **`fake-rpi-gpio` still wired in**.
- Stations + 24VAC power enable; schedules with ~2s overlap; in-memory config.
- Physical pin numbering in code; comments map to wiringPi used by bash.

### 7.3 Hardware assumptions

Exact pin map is a **config file**; human table in [pin-map.md](./pin-map.md).

---

## 8. Target architecture

See [architecture.md](./architecture.md). Summary:

- Go daemon, `go-gpiocdev`, REST + JSON + SSE, atomic JSON store, embedded static UI.
- One run-queue; UI never toggles pins; STOP and fail-safe off are first-class.
- Default overlap sequencing; isolation configurable.

Open / proposed choices live in [decisions.md](./decisions.md).

---

## 9. Product capabilities

| Capability | MVP | v1 |
|---|---|---|
| List stations | ✓ | ✓ |
| Manual run + cancel / STOP | ✓ | ✓ |
| Manual preempt of schedule (warning dialog) | ✓ | ✓ |
| Schedules (clock + weekdays + minutes itinerary) | ✓ | ✓ |
| Multi-schedule + enable/disable + duplicate | ✓ | ✓ |
| Seasonal `starts_on` / `ends_on` | — | ✓ |
| Overlap default / isolate config | ✓ | ✓ |
| Config persists | ✓ | ✓ |
| Dry-run / fake / lockout GPIO | ✓ | ✓ |
| Status + SSE events | ✓ | ✓ |
| Phone-on-LAN UI (Direction D) | ✓ | ✓ |
| Wall / kiosk density | — | ✓ |
| systemd + static binary | ✓ | ✓ |
| Dual-run then flip | ✓ | ✓ |

**Seed / parity fixture:** Front West 4 → Front North 8 → Front South 8 (minutes). Drip exists, not in Front program.

---

## 10. UI direction

**Chosen (decided):** Direction D — hybrid strip. Details in [ui-directions.md](./ui-directions.md).

- Always-visible **STOP** on phone and (later) wall.
- Manual run may preempt a schedule **with an explicit warning**.
- Wall screen = v1, not MVP (same UI, bigger type, edits tucked).

---

## 11. Success criteria (cutover confidence)

- [ ] Runtime builds on amd64 (dev) and on Pi arch matching `uname -m` (`arm` / `arm64`) with one documented path.
- [ ] Idle RSS under ~20MB in dry-run on Pi-class hardware (or measured on the Pi 3).
- [ ] Dry-run mode exercises schedules without GPIO.
- [ ] On Pi with valves locked out / dry: pin toggles match [pin-map.md](./pin-map.md) (scope/log verified); refuses boot without `active_low`.
- [ ] Golden program (Front West 4 / North 8 / South 8) runs with those stations and durations (± documented overlap sequencing, not bash dead-time).
- [ ] Config survives reboot; schedules re-arm after restart (`America/Phoenix`).
- [ ] systemd starts on boot; `ExecStop` / signals leave valves off.
- [ ] LAN phone UI can STOP, start/stop, and edit schedule without SSH.
- [ ] Dual-run period completed; bash kept as documented rollback for one season or until operators are comfortable.

---

## 12. Delivery approach

1. **Record vision** (this PRD + decisions + architecture + pin map + UI directions) on `rewrite/vision`.
2. **Confirm** proposed P0/P1 decisions (or field-test D2) — see [decisions.md](./decisions.md).
3. **Implement** on a follow-on branch (e.g. `rewrite/go`): runtime skeleton → GPIO → schedules → API → UI → systemd → dual-run → parity → cutover.
4. Keep `fancy-vibes` and `mvp-bash` as read-only references until cutover notes say otherwise.

Work runs on Nick’s connected Linux machine (not Cloud Agents). Specialist bots review and implement against these docs.

---

## 13. References in-repo

- `mvp-bash`: `startup.sh`, `channel.sh`, `front.sh`
- `fancy-vibes`: `server/stations.ts`, `server/scheduling.ts`, `server/server.ts`, `client/`
- [decisions.md](./decisions.md) — prioritized decisions
- [architecture.md](./architecture.md) — runtime layers, state machine, API sketch
- [pin-map.md](./pin-map.md) — this hardware’s pin table + example config
- [ui-directions.md](./ui-directions.md) — UI options and stakeholder log
