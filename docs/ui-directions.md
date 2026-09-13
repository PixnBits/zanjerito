# Zanjerito — UI directions

**Status:** Draft v0.5 · 2026-09-12  
**Companion:** [prd.md](./prd.md) · [decisions.md](./decisions.md)  
**Selected:** **Direction D — hybrid strip** (Nick, 2026-09-12)  
**Panel:** OSOYOO Raspberry Pi DSI Display 3.5″ capacitive v1.0, 800×480

The `fancy-vibes` React UI is a reference for *capabilities*, not layout.

---

## Shared constraints

- Phone on the home Wi-Fi: thumb-friendly primary actions.
- Glanceable: **now**, **next**, **last run** (v1), **any fault**.
- **STOP** always visible. No extra confirm while watering.
- Manual run while a schedule is watering: explicit warning, then abort remaining itinerary.
- Household edits: clock + weekday chips + minutes. No raw cron / `PT3M`.
- Timezone: `America/Phoenix`.
- Kiosk: physical access = full access (D7). Anonymous LAN status default on, persisted.

### Surfaces

| Surface | When | Notes |
|---|---|---|
| Phone on LAN | **MVP** | Full Direction D |
| OSOYOO 3.5″ DSI 800×480 | **v1** | Same UI, kiosk density. Active area ~76×45 mm. Targets ~15–20 mm (~160–210 px at native). STOP dominant. Not a scaled-down phone page. |
| Orbit 57946 dial/buttons | **stretch** | Unwired today. Future GPIO *inputs* only; same engine; never a second output writer. |

---

## Direction D — selected

Status-first home + compact station strip + always-visible STOP. Schedules on page 2.

Kiosk home is status + STOP + maybe two station chips. Everything else is a second page.

---

## Directions not chosen

- A status-first tabs
- B schedule board
- C manual-operator panel

Kept below only as history.

### A — Status first
Home is a big status card. More taps to edit.

### B — Schedule board
Week board; busy for household use.

### C — Manual operator
Large run buttons; undervalues automation.

---

## Feedback log

| Who | Date | Preference | Notes |
|---|---|---|---|
| Nick | 2026-09-12 | D hybrid | STOP on app and wall. Preempt warns. |
| Nick | 2026-09-12 | kiosk hardware | OSOYOO 3.5″ DSI 800×480 v1.0. Fat finger. |
| Nick | 2026-09-12 | auth | Anon status default on, persisted. Physical access = full access. |
| Nick | 2026-09-12 | Orbit panel | Unwired. Stretch. |
| Nick | 2026-09-12 | seasons | Dates punted; own conversation. |
