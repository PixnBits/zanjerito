# Zanjerito — Product Requirements Document

**Status:** Draft v0.1  
**Date:** 2026-09-12  
**Repo:** [PixnBits/zanjerito](https://github.com/PixnBits/zanjerito)  
**Working branch for this rewrite:** `rewrite/vision` (keeps `fancy-vibes` / `mvp-bash` intact as reference)  
**Codename etymology:** *Zanjero* is Spanish for “ditch rider.” Since the late 1800s, zanjeros opened head gates so water reached fields and faucets. Zanjerito is a small, reliable local zanjero for a home drip system.

---

## 1. One-liner

Zanjerito is a **Raspberry Pi–hosted irrigation controller**: a small local runtime that drives 24VAC valve relays over GPIO, runs schedules reliably, and exposes a simple website so the household can see and change what is watering — without depending on a flaky commercial cloud box.

---

## 2. Why rewrite now

The Node/`fancy-vibes` stack was the right trade for **human** authoring before strong coding LMs: familiar TS, Fastify, GraphQL, React. The target has shifted.

| Then | Now |
|---|---|
| Optimize for Nick writing and maintaining the code | Optimize for **machine efficiency on the Pi** (CPU, RAM, flash, boot, idle) |
| GraphQL because it was interesting | Pick the interface that is **small, clear, and cheap on-device** |
| UI as a first sketch | **Redo the UI** with stakeholder feedback; treat current UI as a prototype only |

`mvp-bash` remains the **production truth today**. `fancy-vibes` remains the **behavioral and domain reference** (stations, power enable, schedules, overlap). Neither branch is the destination.

---

## 3. Problem

1. Commercial drip controllers fail in ways that are hard to fix (Wi‑Fi brick, silent valve failures).
2. Bash scripts work but are awkward to schedule, observe, and safely change.
3. A Node GraphQL + React stack on a Pi is heavier than this job needs, and the current UI/API are not the long-term product.
4. Confidence to cut over requires: real GPIO (not fakes), durable config, a prod run path (e.g. systemd), and parity with today’s watering behavior.

---

## 4. Goals / non-goals

### Goals (v1 — confident Pi cutover)

- Drive the existing relay board / 24VAC enable and station channels **correctly and safely** (fail-safe off on start, crash, and shutdown).
- Run **named schedules** (cron-like starts + ordered station itinerary with durations).
- Expose a **local website** on the LAN for status, manual run, and schedule/station edits.
- Persist configuration across restarts (stations, schedules, pin map).
- Ship as a **single lightweight runtime** (Go *or* Rust — decision open) + static or similarly cheap UI assets.
- Document install: Node-free prod path, systemd (or equivalent), and a dry-run / lockout mode for safe testing.
- Prove **parity** with current `front.sh` / bash behavior before retiring bash as primary.

### Non-goals (v1)

- Cloud account, phone push vendor lock-in, or mandatory internet.
- Multi-site fleet / multi-tenant SaaS.
- ML weather/ET optimization (interesting later; not cutover).
- Keeping GraphQL, React, or the `fancy-vibes` page structure.
- Pixel-perfect reuse of the current UI.

### Later (out of v1, keep in mind)

- Weather / soil / rain skip.
- Auth beyond LAN trust (or simple local password) if exposed wider.
- Mobile-polished PWA.
- Metrics / history graphs beyond “what ran.”

---

## 5. Users / stakeholders

| Persona | Need |
|---|---|
| Household operator (primary) | Know what’s on, start/stop a station, change schedules without SSH |
| Nick (builder / ops) | Safe deploy, easy rollback to bash, clear logs, low Pi resource use |
| Other stakeholders (UI/product feedback) | Review UI directions; veto clutter; prefer glanceable status |

Stakeholder UI review is **required** before locking a visual design. Capture options in `docs/ui-directions.md`.

---

## 6. Current system (as-is)

### 6.1 Production: `mvp-bash`

- `startup.sh` — configure pins as outputs, default **off** (active-low / inverted relay sense).
- `channel.sh <wiringPiChannel> <minutes>` — drop 24VAC, all off, enable 24VAC, run one channel, then shut down.
- `front.sh` — Front West 4m, Front North 8m, Front South 8m (drip channel defined but not in this program).
- Paths assume `/home/pi/zanjerito/`. WiringPi channel numbers in scripts.

### 6.2 Reference app: `fancy-vibes`

- Fastify + Mercurius (GraphQL) + React/urql; Node 22; `rpi-gpio` intended but **`fake-rpi-gpio` still wired in**.
- Stations + 24VAC power enable; schedules with overlap; in-memory config (edits do not persist; schedule saves do not re-arm jobs).
- Physical pin numbering in code; comments map to wiringPi used by bash.

### 6.3 Hardware assumptions (verify on Pi)

- Raspberry Pi with GPIO to a relay board controlling 24VAC valve solenoids.
- One **power enable** (24VAC) plus multiple **station** channels.
- Relay polarity: logic high ≈ valve/PSU **off** (inverted board) — match bash.

Exact pin map must be a **config file**, not only hard-coded constants.

---

## 7. Target architecture (draft — decisions open)

```
[ Browser on LAN ]
        |
        v
[ Local HTTP API + static UI ]   <-- protocol TBD (REST / RPC / SSE / GraphQL / …)
        |
        v
[ Zanjerito runtime (Go or Rust) ]
   - schedule engine
   - station / power state machine
   - GPIO driver (real + fake/dry-run)
   - durable store (JSON/SQLite/…)
        |
        v
[ Linux GPIO → relay board → 24VAC valves ]
```

### Principles

1. **Safety first:** default off; never leave 24VAC on after errors; single-writer to GPIO.
2. **Pi-thin:** small binary, low idle RAM/CPU, fast start under systemd.
3. **Durable truth:** config on disk; runtime state recoverable after reboot.
4. **Observable:** structured logs; clear “what is on / next run.”
5. **Replaceable UI:** API is the product boundary; UI can be rebuilt without rewriting valve logic.
6. **Parity path:** bash remains fallback until parity checklist is green.

Open choices (language, API shape, store, UI kit) live in `docs/decisions.md`.

---

## 8. Product capabilities (v1)

| Capability | Notes |
|---|---|
| List stations | Name, notes, pin, on/off |
| Manual run | Start station for duration; cancel; respect power-enable sequencing |
| Schedules | Cron-like starts; ordered itinerary; enable/disable |
| Overlap / sequencing | Explicit policy (overlap vs full 24VAC drop between stations) — **decide vs bash** |
| Config edit | Stations + schedules persist |
| Dry-run / fake GPIO | Dev laptop + safe Pi bring-up |
| Status | Now watering? Next invocation? Last error? |
| Deploy | Build artifact + systemd unit + README |

---

## 9. UI direction (intentionally open)

The `fancy-vibes` UI proved GraphQL subscriptions and basic CRUD; it is **not** the target UX.

We will:

1. Sketch 2–4 UI directions (glanceable status, mobile-first controls, “set and forget” schedule board, etc.) in `docs/ui-directions.md`.
2. Get stakeholder feedback before implementation.
3. Prefer boring, readable controls over novelty.

---

## 10. Success criteria (cutover confidence)

- [ ] Runtime builds on amd64 (dev) and aarch64 (Pi) with one documented path.
- [ ] Dry-run mode exercises schedules without GPIO.
- [ ] On Pi with valves locked out / dry: pin toggles match expected map (scope/log verified).
- [ ] A named program matching today’s `front.sh` runs with the same stations and durations (± documented sequencing difference).
- [ ] Config survives reboot; schedules re-arm after restart.
- [ ] systemd (or equivalent) starts on boot; failure leaves valves off.
- [ ] LAN UI can start/stop and edit schedule without SSH.
- [ ] Bash kept as documented rollback for one season or until operators are comfortable.

---

## 11. Delivery approach

1. **Record vision** (this PRD + decisions + UI directions) on `rewrite/vision`.
2. **Decide** language, API, store, sequencing policy, UI direction (with Architect / stakeholders).
3. **Implement** runtime skeleton → GPIO → schedules → API → UI → systemd → parity → cutover.
4. Keep `fancy-vibes` and `mvp-bash` as read-only references until cutover notes say otherwise.

Work runs on Nick’s connected Linux machine (not Cloud Agents). Specialist bots (System Architect, Senior Coder, Tester, CISO, …) review and implement against this doc.

---

## 12. References in-repo

- `mvp-bash`: `startup.sh`, `channel.sh`, `front.sh`
- `fancy-vibes`: `server/stations.ts`, `server/scheduling.ts`, `server/server.ts`, `client/`
- `docs/decisions.md` — prioritized open decisions
- `docs/ui-directions.md` — UI options for stakeholder review
