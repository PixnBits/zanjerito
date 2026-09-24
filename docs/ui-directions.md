# Zanjerito — UI directions (for stakeholder feedback)

**Status:** Draft v0.6 · 2026-09-24 (Polish v1 Style A + kiosk density)  
**Companion:** [prd.md](./prd.md) · [decisions.md](./decisions.md)  
**Selected (decided):** **Direction D — hybrid strip** (Nick, 2026-09-12)

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
| Nick | 2026-09-23 | Style A | **Polish v1 — Cards + desert light.** Direction D IA unchanged. Brand: sand/cream chrome, adobe terracotta STOP, canal teal active, dusk plum paused. Flat sand UI; dune photos/illustration are mood/line-art only — **not** photo thumbnail cards. Visual language Style A (hero status) on **both** Home and Schedules. **Rejected Style B** illustrated-list with season photo thumbs. |


---

---

## Polish v1 — Cards + desert light (Style A)

**Locked (Nick, 2026-09-23).** Direction D hybrid-strip IA is unchanged. This section is the visual/brand contract for a later CSS/UI polish PR — **docs only here; no CSS/React in this revision.**

**Not Style B.** Do not use an illustrated-list Schedules page with season **photo thumbnail** cards (mesa/dune photos as card art). Dune photography and desert illustration are **mood / line-art accents only**, never photo thumbs on program cards.

### Palette

| Role | Token feel | Use |
|---|---|---|
| Page / chrome | Flat sand | Background; flat sand UI chrome (not photo-backed chrome) |
| Cards / surfaces | Cream | Status hero, station tiles Off, schedule cards |
| STOP / active nav | Adobe terracotta | Primary STOP; terracotta underline on active Home \| Schedules |
| Active / watering | Canal teal | Station On tiles; watering timer |
| Paused | Dusk plum | Pause banner |
| Wordmark | Serif | Left “Zanjerito” wordmark |

### Chrome (Home and Schedules)

- Left wordmark **Zanjerito** (serif).
- Nav: **Home | Schedules** with terracotta underline on the active tab.
- Flat sand chrome throughout.

### Home — Style A (hero status)

- Large **cream** status hero card:
  - Season tag + sun accent
  - Title: **Idle** / **Watering • {station}** / **Paused**
  - Teal timer when watering
  - **Next: …** line
  - Subtle desert **line-art** on the right — **not** a photo
- Stations strip: **West / North / South** only (no fake 4th station); cream **Off** / teal **On** tiles; serif station names
- Actions: **Pause for rain** secondary cream; **STOP** primary terracotta, always visible
- Paused state: dusk plum banner + **Resume** + **STOP**

### Schedules — same language as Home (not Style B)

- Cream **line-art / flat** cards (no mesa/season photo thumbs)
- Soft collide warning banner when enabled programs overlap
- Season badges + enable toggles; **Edit** / **Duplicate**
- Terracotta **+ Add program**

### Editor (household)

- Clock (**HH:MM**), weekday chips, date window (`starts_on` / `ends_on` or all-year), station minutes
- No cron strings, no `PT5M` / ISO durations, no GraphiQL in the household path

### States checklist

| State | Hero / chrome cues |
|---|---|---|
| Idle | Cream hero titled Idle; Next: …; stations Off cream |
| Watering | Title Watering • {station}; teal timer; that station On teal |
| Paused | Dusk plum banner; Resume + STOP; hero may read Paused |

## v1 explore (Direction D — post-cutover)

Still **Direction D** hybrid strip. Production UI is the Go-embedded phone app (`DRIVER=gpiocdev`). Do **not** surface cron strings, `PT5M`, GraphiQL, or raw GPIO as household primary controls.

### Seasonal program cards

Engine already skips programs outside `starts_on` / `ends_on`. Cards should show name, on/off, next fire, and a season badge (date window or year-round). Actions: Enable / Edit / Duplicate. Out of season → label **Out of season** (not a broken card).

### Schedule editor polish

Household editor: clock (**HH:MM**) + weekday chips + station minutes. Season: **All-year** | **Between dates**. Itinerary: station + minutes; fat tap targets. Soft warn when two enabled programs collide (D13). Never lead with cron / ISO durations / GraphiQL.

### Run history

Thin list later (**D10**). **Not** this explore’s implementation scope — no history store/page yet.

### Kiosk density

Wall / `?mode=kiosk` density is **un-parked** (same Direction D + Style A UI — not a second app).

Open `http://<pi-lan>:8080/?mode=kiosk` on the wall / `screen-mount-part` browser:

| Cue | Kiosk behavior |
|---|---|
| Type / layout | Larger root type, wider max-width (~920px), bigger hero phase |
| STOP | Dominant fixed footer (~88px tall) — always visible |
| Edits | Schedules nav + `.edit` actions hidden (tucked; phone path unchanged) |
| Stations | Fat-finger tiles (~168px min-height), larger icons/pills |
| Glance states | Hero titles **Idle** / **Watering • {station}** / **Paused**; plum hero when paused |
| Household leaks | Still no cron / `PT5M` / GraphiQL |

Phone (`/` without `mode=kiosk`) keeps the compact Style A density.

### Suggested build order

1. ~~Seasonal program cards + schedule editor UI~~ (landed — PR #19)
2. ~~CSS/UI polish implementing **Polish v1 Style A**~~ (landed — PR #21)
3. ~~Kiosk / wall density (`?mode=kiosk`)~~ (this PR)
4. Run history thin list later (**D10** — out of scope here)
5. Webpage pause (rain/mowing) when separately green-lit
