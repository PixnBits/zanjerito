# Zanjerito — Open decisions (prioritized)

**Status:** Draft v0.5 · 2026-09-12  
**Companion:** [prd.md](./prd.md) · [architecture.md](./architecture.md) · [pin-map.md](./pin-map.md)  
**Vision capture:** [#12](https://github.com/PixnBits/zanjerito/issues/12)

Decisions are ordered by **how much they unblock**. Mark each: `open` | `proposed` | `decided`.

---

## P0 — Must decide before serious implementation

### D1. Runtime language — Go vs Rust
- **Status:** decided → **Go** (2026-09-12)
- **Choice:** Go. Static binary, stdlib HTTP + `embed`, `GOOS=linux GOARCH=arm64` and `GOARCH=arm GOARM=7` (confirm `uname -m`). GPIO: `github.com/warthog618/go-gpiocdev`. Not wiringPi, not sysfs, not `go-rpio`.

### D2. Valve sequencing policy vs bash
- **Status:** decided → **overlap default; isolation configurable** (2026-09-12)
- **Choice:** Default `sequencing: overlap`, `overlap_ms: 2000`. Isolation remains for commissioning / weak transformer. `front.sh` parity is stations + durations, not bash dead-time.

### D3. GPIO numbering & pin map ownership
- **Status:** decided → **config file is source of truth** (2026-09-12)
- **Choice:** Runtime speaks BCM / `gpiochip` + offset. Refuse boot if `active_low` is missing.

### D4. Durable store
- **Status:** decided → **atomic JSON** (2026-09-12)
- **Choice:** stations, schedules, pin map, and the anonymous-status toggle as JSON; write temp + rename. SQLite later for run history (D10).

### D5. Local API shape
- **Status:** decided → **REST + JSON + SSE** (2026-09-12)

### D13. Schedule collision while a run is active
- **Status:** decided → **skip + log** (2026-09-12)
- **Choice:** Do not interleave itineraries. Do not silently catch up. Manual preempt stays a separate warning path (D6).
- **Power-loss (2026-09-12):** boot → all-off → do not resume a partial itinerary → re-arm next wall-clock start. Same honesty principle.

### D14. Architect Approve follow-ups
- **Status:** decided → fold into architecture (2026-09-12)
- Fault → all-off; reject/defer config writes while watering; ≤2 stations ON; Direction D is decided.

### D15. Audience
- **Status:** decided → **this house first, copyable guts** (2026-09-12)
- Pin map stays config. Do not design a fleet product before the household can change a schedule without SSH.

---

## P1 — Decide before UI build / cutover packaging

### D6. UI direction
- **Status:** decided → **Direction D hybrid strip** (Nick, 2026-09-12)
- Phone-on-LAN is MVP.
- Wall is **v1**, on the already-installed **OSOYOO 3.5″ DSI 800×480 capacitive v1.0**. Fat-finger targets (~15–20 mm). One UI, two densities (`?mode=kiosk`).
- **Orbit 57946 dial/buttons:** unwired today. Stretch after v1. Do not reserve GPIO inputs now.

### D7. Auth / exposure model
- **Status:** decided → **physical access = full access; anonymous LAN status default on** (2026-09-12, revises “LAN trust only”)
- Bind LAN only. No cloud IdP. No GraphiQL on 0.0.0.0.
- **Kiosk / standing at the box:** unauthenticated STOP and operation. You can touch the hardware.
- **Anonymous status on LAN:** default **on** at first boot; toggle persists across power cycles (JSON store).
- **Writes from a random laptop on the LAN** (schedules, pin map, long manual runs) may be gated by local accounts in v1. That surface is not the kiosk.
- `max_on_sec` (default 900) still caps runaway watering even when authenticated.
- Threat model: unattended LAN client while the house is away, not someone in the garage.

### D8. Packaging & process supervisor
- **Status:** decided → **systemd system unit + static binary**; lean default **`/opt/zanjerito`** (2026-09-12)
- `ExecStop=` force all-off. Inactive-on-release.
- Still open: update story (git pull vs release tarball). Example config copied once.

### D9. Overlap of bash rollback window
- **Status:** decided → **dual-run first**

---

## P2 — After skeleton

### D10. History / metrics retention
### D11. Rain skip / weather / municipal restriction feed
### D12. Multi-controller / remote access
### D16. Seasonal clocks and Drip window
- **Status:** open — punted 2026-09-12
- Schema (`starts_on` / `ends_on`) is v1. Specific Front summer/winter times and Drip’s program are a later conversation. Do not invent dates.

---

## Decision log

| ID | Decision | Date | Notes |
|---|---|---|---|
| D1 | Go + `go-gpiocdev` | 2026-09-12 | Pi 3; confirm `uname -m` |
| D2 | overlap default, isolate configurable | 2026-09-12 | hammer is the bigger danger |
| D3 | config + pin-map.md | 2026-09-12 | |
| D4 | atomic JSON | 2026-09-12 | includes anonymous-status toggle |
| D5 | REST+JSON+SSE | 2026-09-12 | |
| D6 | Direction D; 3.5″ DSI kiosk = v1 | 2026-09-12 | Orbit panel = stretch, unwired |
| D7 | physical access = full access; anon status default on | 2026-09-12 | revises LAN-trust-only |
| D8 | systemd + static binary + `/opt/zanjerito` | 2026-09-12 | update story still open |
| D9 | dual-run then flip | 2026-09-12 | |
| D13 | skip + log; no resume after power-loss | 2026-09-12 | |
| D14 | Architect follow-ups | 2026-09-12 | |
| D15 | this house first, copyable guts | 2026-09-12 | |
| D16 | seasonal dates | 2026-09-12 | open / punted |
