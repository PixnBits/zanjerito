# Zanjerito — Implementation plan (v1)

**Status:** Active · 2026-09-12  
**Base:** `fancy-vibes` (vision docs merged via PR #3)  
**Working branch family:** `rewrite/go*`  
**Companions:** [prd.md](./prd.md) · [architecture.md](./architecture.md) · [decisions.md](./decisions.md)

## Ground rules

- Scoped PRs; one concern each.
- Reviews from specialist bots (Architect, Coder, Tester, CISO as relevant); merge when the team agrees.
- Work on Nick’s Linux host (not Cloud Agents).
- Bash (`mvp-bash`) stays production until dual-run flip (D9).

## PR sequence

| # | Scope | Branch (suggested) | Reviewers |
|---|---|---|---|
| 1 | Go module, `cmd/zanjerito`, `internal/gpio` iface + fake/lockout, example pinmap, tests | `rewrite/go` | Architect, Tester |
| 2 | Engine state machine + safety invariants (Fault→all-off, ≤2 ON, config-while-watering) | `rewrite/go-engine` | Architect, CISO |
| 3 | Atomic JSON store (load/save pin map, stations, schedules) | `rewrite/go-store` | Coder, Tester |
| 4 | Scheduler + Front West 4 / North 8 / South 8 fixture; skip+log collision | `rewrite/go-schedule` | Architect, Tester |
| 5 | REST + JSON + SSE API | `rewrite/go-api` | Architect, Coder |
| 6 | Direction D phone UI (MVP) embedded | `rewrite/go-ui` | Nick + stakeholders, Tester |
| 7 | systemd unit + deploy docs (`/opt/zanjerito` lean) | `rewrite/go-deploy` | CISO, Coder |
| 8 | Dual-run / parity helpers + cutover checklist | `rewrite/go-cutover` | Architect, Tester |

Wall screen density + seasonal `starts_on`/`ends_on` stay **v1 after MVP** (PRD).

## Done when (MVP)

See PRD success criteria: fake/dry-run, fail-safe off, golden program parity (stations+durations), config persists, LAN phone UI, systemd, dual-run path documented.
