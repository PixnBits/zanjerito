# Zanjerito — Open decisions (prioritized)

**Status:** Draft v0.3 · 2026-09-12  
**Companion:** [prd.md](./prd.md) · [architecture.md](./architecture.md) · [pin-map.md](./pin-map.md)

Decisions are ordered by **how much they unblock**. Mark each: `open` | `proposed` | `decided`.  
Record the choice inline when decided (date + rationale). Full ADRs can split out later if needed.

---

## P0 — Must decide before serious implementation

### D1. Runtime language — Go vs Rust
- **Status:** decided → **Go** (2026-09-12)
- **Context:** Optimize for Pi efficiency; LMs maintain either. Node stays reference only. Hardware is a **Raspberry Pi 3** (BCM2837, typically `gpiochip0`, ~1GB RAM).
- **Choice:** Go. Static binary, stdlib HTTP + `embed`, `GOOS=linux GOARCH=arm64` and `GOARCH=arm GOARM=7` (confirm `uname -m` on the box). GPIO via Linux character device: `github.com/warthog618/go-gpiocdev`. Not wiringPi, not sysfs, not `go-rpio`.
- **Why not Rust for v1:** Operational fail-safe (signals, systemd `ExecStop`, single writer, inactive-on-release) matters more than typestate. `rppal` was archived 2025-07-01; a Rust path would use `gpiocdev` anyway. Revisit if this moves to a Pi Zero or typestate becomes a product goal.
- **Unblocks:** repo layout, tooling, first binary.

### D2. Valve sequencing policy vs bash
- **Status:** decided → **overlap default; isolation configurable** (2026-09-12)
- **Context:** Bash drops 24VAC and fully offs between channels (flow slams shut — hammer risk). `fancy-vibes` overlaps ~2s while keeping power on (two solenoids briefly, anti-hammer). Nick: water hammer is the bigger danger.
- **Choice:**
  - Default: `sequencing: overlap`, `overlap_ms: 2000`. 24VAC stays up for the itinerary. Next station ON, then previous OFF. If duration < overlap, shrink overlap.
  - Config enum per schedule (or global fallback): `overlap | isolate`.
  - Isolation mode remains for commissioning / a weak transformer.
- **Still do:** one-evening field test — same Front program, isolation vs 2s overlap, listen at valves, feel transformer heat. Software default is overlap until that test says otherwise.
- **Parity note:** `front.sh` parity is stations + durations, **not** bash dead-time between stations.
- **Unblocks:** schedule engine + parity tests.

### D3. GPIO numbering & pin map ownership
- **Status:** decided → **config file is source of truth** (2026-09-12)
- **Choice:** Runtime speaks BCM / `gpiochip` + offset. wiringPi and physical pin numbers live in [pin-map.md](./pin-map.md) and in config comments only. Validate polarity on first boot; refuse to run if the map is missing `active_low`.
- **Default map for this hardware:** see [pin-map.md](./pin-map.md) (Front West/North/South + Drip + PSU).
- **Unblocks:** driver + dry-run + parity.

### D4. Durable store
- **Status:** decided → **atomic JSON** (2026-09-12)
- **Choice:** stations, schedules, pin map as JSON; write temp + rename. SQLite later for run history (D10).
- **Unblocks:** API mutations that matter.

### D5. Local API shape
- **Status:** decided → **REST + JSON + SSE** (2026-09-12)
- **Choice:** boring HTTP JSON for CRUD; SSE (`GET /api/events`) for live status. No GraphQL engine in the binary. No gRPC.
- **Sketch:** [architecture.md](./architecture.md)
- **Unblocks:** UI and integration tests.


### D13. Schedule collision while a run is active
- **Status:** decided → **skip + log** (2026-09-12)
- **Choice:** If a second schedule fires while one is already watering (or queued manual/schedule run is active), **skip** the new schedule start and log it. Do not interleave itineraries. Do not silently queue delayed watering into a worse time of day.
- **Manual preempt** remains separate: warning dialog, then abort remaining itinerary and start the manual run (D6).
- **Later:** optional single-slot queue if we miss programs in practice.
- **Unblocks:** engine run-queue behavior.

---

## P1 — Decide before UI build / cutover packaging

### D6. UI direction (stakeholder pick)
- **Status:** decided → **Direction D hybrid strip** (Nick, 2026-09-12)
- **Choice:** Status-first home + compact station chips + always-visible STOP. Schedules on page 2 with clock + weekday chips + minutes (no raw cron / `PT3M` in the household UI).
- **MVP vs v1:** phone-on-LAN is MVP. Wall / `screen-mount-part` screen is **v1, not MVP** — same layout, bigger type, STOP dominant, edits tucked. One UI, two densities (`?mode=kiosk` or equivalent). Not a second app.
- **Preempt:** manual run may interrupt a schedule **with an explicit warning dialog**. STOP itself needs no extra confirm when already watering (safety action). See [ui-directions.md](./ui-directions.md).
- **Unblocks:** frontend implementation.

### D7. Auth / exposure model
- **Status:** decided → **LAN trust only** for MVP/v1
- **Options later:** HTTP basic / shared password if exposure widens. Do not ship GraphiQL-on-0.0.0.0.
- **Unblocks:** deploy docs and threat notes.

### D8. Packaging & process supervisor
- **Status:** decided → **systemd system unit + static binary** (2026-09-12); install path / update story still open
- **Choice:** system unit + static Go binary. `ExecStop=` force all-off. Inactive-on-release on GPIO lines so crash de-energizes.
- **Still open:** install path (`/opt/zanjerito` vs `$HOME`), how config is shipped, update story (git pull vs release tarball). Default lean: `/opt/zanjerito` + example config copied once; release tarball later if git-on-Pi gets annoying.
- **Unblocks:** Pi install runbook (path choice can land with first deploy docs).

### D9. Overlap of bash rollback window
- **Status:** decided → **dual-run first**
- **Choice:** daemon logs intended actions while bash still actuates; flip after N good scheduled days. Bash stays documented rollback for a season.
- **Unblocks:** cutover checklist.

---

## P2 — Can wait until after skeleton works

### D10. History / metrics retention
### D11. Rain skip / weather
### D12. Multi-controller / remote access

Seasonal date windows (winter grass in October) are **not** P2 — they are v1 schedule schema. Enable/disable + duplicate is enough for MVP; `starts_on` / `ends_on` (`America/Phoenix`) is the v1 refinement.

---

## Decision log

| ID | Decision | Date | Notes |
|---|---|---|---|
| D1 | Go + `go-gpiocdev` | 2026-09-12 | decided; Pi 3; confirm 32 vs 64-bit OS (`uname -m`) |
| D2 | overlap default, isolate configurable | 2026-09-12 | decided; Nick: hammer is the bigger danger; field-test still recommended |
| D3 | config + pin-map.md | 2026-09-12 | decided |
| D4 | atomic JSON | 2026-09-12 | decided |
| D5 | REST+JSON+SSE | 2026-09-12 | decided |
| D6 | Direction D; wall = v1 not MVP | 2026-09-12 | decided; Nick |
| D7 | LAN trust only | 2026-09-12 | decided for MVP/v1 |
| D8 | systemd + static binary + ExecStop force-off | 2026-09-12 | decided; install path / update story still open |
| D9 | dual-run then flip | 2026-09-12 | decided |
| D13 | schedule collision → skip + log | 2026-09-12 | decided; manual preempt unchanged |
