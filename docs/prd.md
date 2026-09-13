# Zanjerito — Product Requirements Document

**Status:** Draft v0.5 · 2026-09-12  
**Repo:** [PixnBits/zanjerito](https://github.com/PixnBits/zanjerito)  
**Working branch for this rewrite:** `rewrite/vision`  
**Source of tonight’s refinements:** [#12](https://github.com/PixnBits/zanjerito/issues/12)  
**Companions:** [decisions.md](./decisions.md) · [architecture.md](./architecture.md) · [pin-map.md](./pin-map.md) · [ui-directions.md](./ui-directions.md)  
**Codename etymology:** *Zanjero* is Spanish for “ditch rider.” Zanjerito is a small, reliable local zanjero for a home drip system.

---

## 1. One-liner

Zanjerito is the ditch rider for **one home drip system**: a Pi you can swap opens the right 24VAC valve at the right time, fails off when anything is wrong, and lets the household see and change the program from a phone or the box — no cloud, no cron, no manufacturer radio.

Go, GPIO, REST, and systemd are how that promise stays cheap on a Pi 3.

---

## 1.1 Product ethic

1. Fail off, never fail on.
2. The household operates without SSH / cron / Nick.
3. Internet is optional for watering. Forecast and rain feeds are later add-ons, not a boot requirement.
4. One writer to GPIO outputs.
5. Honesty over catch-up: no silent delayed watering after a missed or collided start.
6. Rollback is a feature (`mvp-bash` stays a season).
7. Parts are replaceable. Software must not couple itself to one Pi, one relay board, or one vendor radio.

**Boot / power-loss:** all-off → do **not** resume a half-finished itinerary → re-arm the next wall-clock start.

---

## 2. Why rewrite now

The commercial controller’s Wi-Fi daughter board could not be upgraded. Updates died with the manufacturer’s hardware. Everything else still worked.

The Node/`fancy-vibes` stack was the right trade for **human** authoring before strong coding LMs. The target has shifted to a small Go appliance the household can operate.

`mvp-bash` remains the **production truth today**. `fancy-vibes` remains the **behavioral and domain reference**. Neither branch is the destination.

---

## 3. Problem

1. Commercial controllers fail as closed update channels (Wi-Fi brick) and as silent valve boxes.
2. Bash works but is a black box to the household.
3. Node GraphQL + React on a Pi 3 is heavier than this job, and that UI is not the product.
4. Cutover needs real GPIO, durable config, systemd, and parity with today’s Front stations + durations.

---

## 4. Hardware (now)

- **Board:** Raspberry Pi 3 (BCM2837), typically `/dev/gpiochip0`, ~1GB RAM.
- **Confirm later:** `uname -m` and `gpiodetect`.
- **Relays:** active-low; logic HIGH ≈ valve/PSU **off** (match bash).
- **Channels:** Front West / North / South + Drip + 24VAC PSU enable — see [pin-map.md](./pin-map.md).
- **Kiosk panel:** OSOYOO Raspberry Pi DSI Display 3.5″ capacitive v1.0, 800×480. DSI leaves the 40-pin header free. Active area ~76×45 mm — fat-finger targets (~15–20 mm), not a shrunk phone layout.
- **Orbit B-Hyve 57946 dial/buttons:** original circuitry exists; **no wires to the Pi today.** Stretch goal. Do not reserve pins for it now.
- **Timezone:** `America/Phoenix` (no DST).

**Resource budget (v1 target):** idle RSS **< 20MB**; static binary; no Node on the Pi for prod.

---

## 5. Goals / non-goals

### Audience

**This house first, copyable guts.** Station titles stay human (“Front West”). Pin map is config so another board can be described without a fork of the engine.

### Goals — MVP (confident Pi cutover of runtime + phone UI)

- Drive the existing relay board correctly and safely (fail-safe off on start, crash, shutdown; inactive-on-release; `ExecStop` force-off).
- Run **named schedules** (clock + weekdays + itinerary in minutes).
- Default sequencing: **overlap ~2s**; isolation configurable.
- Phone-on-LAN UI: status, STOP, manual run with preempt warning, schedule/station edits **without SSH**.
- Persist config via **atomic JSON** (including the anonymous-status toggle).
- Single Go binary + embedded static UI; REST + JSON + SSE.
- Cross-compile + systemd + dry-run / lockout documented.
- Parity with `front.sh`: Front West 4 / Front North 8 / Front South 8. Parity is stations + durations, not bash dead-time.
- Dual-run with bash before flip (D9).
- Collision: skip + log (D13).

### Goals — v1 (after MVP)

- Seasonal **date windows** (`starts_on` / `ends_on` in `America/Phoenix`). Specific Front summer/winter clocks and Drip’s window are **not seeded yet** — separate conversation.
- Kiosk density on the installed 3.5″ DSI panel. Same UI, two densities.
- Last-run / next-run on the home strip.
- Local accounts for writes from a random LAN client. Physical access to the box is full access (D7).
- NTP called out in deploy docs. Discovery: `zanjerito.local` or printed IP on the kiosk footer.

### Non-goals (MVP / v1)

- Cloud account, vendor push, mandatory internet.
- Multi-site fleet / SaaS.
- ML weather/ET.
- Keeping GraphQL, React, or the `fancy-vibes` page tree.
- Wiring the Orbit front-panel controls.
- Inventing seasonal dates before that conversation happens.

### Later

- Weather / rain / municipal-restriction feed (design the skip hook; do not wire a vendor).
- Sliding start-time ramp across a shoulder season.
- Flow or current sensing.
- Orbit 57946 dial + buttons as GPIO inputs (second pointer into the same engine).
- Multi-controller / remote access beyond LAN.

---

## 6. Users / stakeholders

| Persona | Need |
|---|---|
| Household operator (primary) | See the program, change it, STOP it, without SSH |
| Nick (builder / ops) | Safe deploy, bash rollback, pin map, logs, low RSS |
| Kiosk user (v1) | Fat-finger status + STOP on the 3.5″ panel; physical access = full access |
| Future copier | Pin-map-as-config + docs. Not a first-class customer until this house works |

---

## 7. Current system (as-is)

### 7.1 Production: `mvp-bash`

- `startup.sh` — pins as outputs, default **off**.
- `channel.sh <wiringPiChannel> <minutes>` — isolate-style single channel.
- `front.sh` — golden program: Front West 4m, Front North 8m, Front South 8m. Drip exists, not in this program.

### 7.2 Reference app: `fancy-vibes`

Behavioral reference only. Fake GPIO still wired. Not the destination.

### 7.3 Hardware

Pin map is a **config file**; human table in [pin-map.md](./pin-map.md).

---

## 8. Target architecture

See [architecture.md](./architecture.md). UI never toggles pins. One run-queue. Fail-safe off. Overlap default.

---

## 9. Product capabilities

| Capability | MVP | v1 |
|---|---|---|
| List stations | ✓ | ✓ |
| Manual run + STOP | ✓ | ✓ |
| Manual preempt with warning | ✓ | ✓ |
| Schedules (clock + weekdays + minutes) | ✓ | ✓ |
| Multi-schedule + enable/disable + duplicate | ✓ | ✓ |
| Seasonal `starts_on` / `ends_on` | — | ✓ (dates later) |
| Overlap default / isolate config | ✓ | ✓ |
| Config persists (incl. anonymous-status toggle) | ✓ | ✓ |
| Dry-run / fake / lockout GPIO | ✓ | ✓ |
| Status + SSE | ✓ | ✓ |
| Phone-on-LAN UI (Direction D) | ✓ | ✓ |
| 3.5″ DSI kiosk density | — | ✓ |
| Anonymous LAN status (default on) | — | ✓ |
| Physical kiosk = full access | — | ✓ |
| systemd + static binary | ✓ | ✓ |
| Dual-run then flip | ✓ | ✓ |
| Last run / next run on home strip | cheap if easy | ✓ |
| Orbit dial/buttons | — | stretch after v1 |

**Seed / parity fixture:** Front West 4 → Front North 8 → Front South 8. Drip is its own program when seasons are specified.

---

## 10. UI direction

**Chosen:** Direction D — hybrid strip. See [ui-directions.md](./ui-directions.md).

- STOP always visible. No confirm while watering.
- Manual preempt warns, then aborts the remaining itinerary.
- Phone MVP. Kiosk v1 on the OSOYOO 3.5″ 800×480 DSI panel — large targets, not a scaled-down phone page.

---

## 11. Success criteria (cutover confidence)

- [ ] Builds on amd64 and on the Pi arch matching `uname -m`.
- [ ] Idle RSS under ~20MB in dry-run.
- [ ] Dry-run exercises schedules without GPIO.
- [ ] Pin toggles match [pin-map.md](./pin-map.md); refuse boot without `active_low`.
- [ ] Golden Front program stations + durations (± overlap, not bash dead-time).
- [ ] Config survives reboot; schedules re-arm (`America/Phoenix`); power-loss does not resume a partial itinerary.
- [ ] systemd + `ExecStop` leave valves off.
- [ ] LAN phone UI: STOP, start/stop, edit schedule without SSH.
- [ ] Dual-run completed; bash documented rollback for a season.

---

## 12. Delivery approach

1. Record vision on `rewrite/vision` (this PRD + companions). Contract also in [#12](https://github.com/PixnBits/zanjerito/issues/12).
2. Implement on `rewrite/go*`.
3. Keep `fancy-vibes` and `mvp-bash` as references until cutover notes say otherwise.

Work runs on Nick’s Linux host. Specialist bots review and implement against these docs. Seasonal clocks are a later conversation — do not invent them in code or docs.

---

## 13. References in-repo

- `mvp-bash`: `startup.sh`, `channel.sh`, `front.sh`
- `fancy-vibes`: `server/stations.ts`, `server/scheduling.ts`, `client/`
- [#12](https://github.com/PixnBits/zanjerito/issues/12) — household vision capture
- Companions in `docs/`
