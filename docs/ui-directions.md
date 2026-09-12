# Zanjerito — UI directions (for stakeholder feedback)

**Status:** Draft v0.1 · 2026-09-12  
**Companion:** [prd.md](./prd.md)  
**Rule:** Do **not** implement a full UI until stakeholders pick a direction (or a hybrid).

The `fancy-vibes` React UI was a learning prototype (stations list, schedules, GraphiQL). It is a reference for *capabilities*, not layout or brand.

---

## Shared constraints (all options)

- Works on a phone on the home Wi‑Fi (thumb-friendly primary actions).
- Glanceable: **what is watering now**, **what’s next**, **any fault**.
- Manual run and cancel must be obvious and hard to do by accident (confirm or hold).
- Edits to schedules/stations must be understandable by a non-developer stakeholder.
- No dependency on GraphQL or the old component tree.

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

## Direction D — “Hybrid strip” (A + C)

**Idea:** Status-first home with a always-visible compact station strip for quick manual runs; schedules on a second page.

- **Pros:** Covers daily glance + tuning.
- **Cons:** Needs careful visual hierarchy so it doesn’t feel cluttered.

---

## Stakeholder questions

1. Who opens the UI more often — checking status, changing schedules, or manual runs?
2. Phone-only, or also a wall tablet / Pi-attached screen (`screen-mount-part`)?
3. Any must-have from the old commercial controller’s app?
4. Preference among A–D (or “none — sketch something else”)?

Record feedback below.

### Feedback log

| Who | Date | Preference | Notes |
|---|---|---|---|
| | | | |
