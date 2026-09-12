# Zanjerito — UI directions (for stakeholder feedback)

**Status:** Draft v0.2 · 2026-09-12  
**Companion:** [prd.md](./prd.md) · [decisions.md](./decisions.md)  
**Selected (proposed):** **Direction D — hybrid strip** (Nick, 2026-09-12)

The `fancy-vibes` React UI was a learning prototype (stations list, schedules, GraphiQL). It is a reference for *capabilities*, not layout or brand.

---

## Shared constraints (all options)

- Works on a phone on the home Wi‑Fi (thumb-friendly primary actions).
- Glanceable: **what is watering now**, **what’s next**, **any fault**.
- **STOP** is always visible on the phone app and on the wall screen (when present). STOP while watering needs **no** extra confirm (safety action).
- Manual run and cancel must be obvious and hard to do by accident for non-STOP actions.
- **Preempt:** if a schedule is watering, starting a manual run shows an **explicit warning dialog**, then aborts the remaining itinerary and starts the manual run.
- Edits to schedules/stations must be understandable by a non-developer stakeholder (clock + weekday chips + minutes — no raw cron / `PT3M` in the household UI).
- No dependency on GraphQL or the old component tree.
- Timezone display and schedule math: `America/Phoenix` (no DST).

### MVP vs v1

| Surface | When | Notes |
|---|---|---|
| Phone on LAN | **MVP** | Full Direction D |
| Wall / `screen-mount-part` | **v1, not MVP** | Same UI, bigger type, STOP dominant, edits tucked (`?mode=kiosk` or equivalent). One UI, two densities — not a second app. |

---

## Direction A — “Status first”

**Idea:** Home screen is a big status card (Idle / Watering station X / Next at …). Secondary tabs for Stations and Schedules.

- **Pros:** Calm, safe, matches “set and forget.”
- **Cons:** More taps to deep-edit schedules.
- **Feels like:** Thermostat / simple home automation.

## Direction B — “Schedule board”

**Idea:** Week-style or list board of programs; status is a persistent strip at the top.

- **Pros:** Power users see the plan at a glance.
- **Cons:** Busier; easier to overwhelm non-technical users.
- **Feels like:** Calendar / sprinkler manufacturer apps.

## Direction C — “Manual operator”

**Idea:** Large station buttons (run 1/5/10 min), schedules tucked under Advanced.

- **Pros:** Great while tuning zones; close to how bash is used today.
- **Cons:** Undervalues automation if Advanced is ignored.
- **Feels like:** Physical front panel.

## Direction D — “Hybrid strip” (A + C) — **selected**

**Idea:** Status-first home with an always-visible compact station strip for quick manual runs; schedules on a second page; **STOP** always on screen.

- **Pros:** Covers daily glance + tuning; matches Nick’s preference.
- **Cons:** Needs careful visual hierarchy so it doesn’t feel cluttered.
- **MVP:** phone. **v1:** wall density of the same layout.

---

## Stakeholder questions

1. Who opens the UI more often — checking status, changing schedules, or manual runs?
2. Phone-only for MVP, wall screen in v1 — still right?
3. Any must-have from the old commercial controller’s app?
4. Preference among A–D (or “none — sketch something else”)?

### Feedback log

| Who | Date | Preference | Notes |
|---|---|---|---|
| Nick | 2026-09-12 | D hybrid | STOP on app and wall. Manual run may preempt a schedule with warnings. Wall screen is v1, not MVP. |
