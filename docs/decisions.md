# Zanjerito — Open decisions (prioritized)

**Status:** Draft v0.1 · 2026-09-12  
**Companion:** [prd.md](./prd.md)

Decisions are ordered by **how much they unblock**. Mark each: `open` | `proposed` | `decided`.  
Record the choice inline when decided (date + rationale). Full ADRs can split out later if needed.

---

## P0 — Must decide before serious implementation

### D1. Runtime language — Go vs Rust
- **Status:** open
- **Context:** Optimize for Pi efficiency; LMs maintain either. Node stays reference only.
- **Options:**
  - **Go** — fast compile, easy static binary, simple concurrency, large GPIO/systemd examples.
  - **Rust** — max control / safety; stronger for long-lived embedded-ish services; heavier authoring/CI.
- **Need from:** System Architect + Nick preference; Senior Coder input on Pi GPIO crates/libs.
- **Unblocks:** repo layout, tooling, first binary.

### D2. Valve sequencing policy vs bash
- **Status:** open
- **Context:** Bash drops 24VAC and fully offs between channels. `fancy-vibes` overlaps ~2s (anti water-hammer) while keeping power on.
- **Options:** match bash exactly; keep overlap; make configurable per schedule.
- **Need from:** Nick (field preference / plumbing reality).
- **Unblocks:** schedule engine + parity tests.

### D3. GPIO numbering & pin map ownership
- **Status:** proposed → **config file is source of truth** (wiringPi vs BCM vs physical documented in that file)
- **Still open:** default map for *this* hardware (port from `fancy-vibes` comments + bash), and how we validate on first Pi boot.
- **Unblocks:** driver + dry-run + parity.

### D4. Durable store
- **Status:** open
- **Options:** JSON file(s); SQLite; other.
- **Bias for v1:** smallest thing that survives reboot and is easy to back up (JSON or SQLite).
- **Unblocks:** API mutations that matter.

### D5. Local API shape
- **Status:** open
- **Context:** GraphQL was exploratory, not a requirement.
- **Options:** REST+JSON; JSON-RPC; gRPC (probably overkill on LAN UI); GraphQL; SSE/WebSocket for live status only.
- **Bias for v1:** boring HTTP JSON + optional event stream for “what’s on.”
- **Need from:** System Architect; UI direction may influence.
- **Unblocks:** UI and integration tests.

---

## P1 — Decide before UI build / cutover packaging

### D6. UI direction (stakeholder pick)
- **Status:** open
- **See:** [ui-directions.md](./ui-directions.md)
- **Need from:** Nick + other household/stakeholders.
- **Unblocks:** frontend implementation.

### D7. Auth / exposure model
- **Status:** open (default proposal: **LAN trust only** for v1)
- **Options:** bind LAN only; HTTP basic; mutual TLS; reverse proxy elsewhere.
- **Unblocks:** deploy docs and threat notes (CISO glance).

### D8. Packaging & process supervisor
- **Status:** proposed → **systemd user or system unit + static binary**
- **Still open:** install path (`/opt/zanjerito` vs `$HOME`), how config is shipped, update story (git pull vs release tarball).
- **Unblocks:** Pi install runbook.

### D9. Overlap of bash rollback window
- **Status:** open
- **Question:** keep bash as primary until N successful scheduled days, or dual-run (runtime logs, bash actuates) first?
- **Unblocks:** cutover checklist.

---

## P2 — Can wait until after skeleton works

### D10. History / metrics retention
### D11. Rain skip / weather
### D12. Multi-controller / remote access

---

## Decision log

| ID | Decision | Date | Notes |
|---|---|---|---|
| — | — | — | none yet |
