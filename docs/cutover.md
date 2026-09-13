# Cutover checklist (D9)

Bash on `mvp-bash` stays the actuator until this list is done. The Go daemon **logs intended GPIO** (`DRIVER=dualrun`) so you can compare journal lines to what bash actually did.

`gpiocdev` (inactive-on-release, real `/dev/gpiochip*`) is an **explicit leftover** — do not flip to it in this PR.

## Drivers

| `DRIVER` | Hardware | Use |
|---|---|---|
| `fake` | none (shadow state) | laptop / CI |
| `dualrun` | none; logs `intended Set` | **D9 overlap** — bash still waters |
| `lockout` | none; logs `refuse Set` | safe Pi bring-up without bash compare |
| `gpiocdev` | fails closed today | leftover: wire + inactive-on-release, then flip |

Set in `/opt/zanjerito/zanjerito.env` (copy-once). Restart: `sudo systemctl restart zanjerito`.

## Dual-run (now)

1. Leave `mvp-bash` / `front.sh` (or cron) **enabled**. Do not disable bash yet.
2. Install Go unit with `DRIVER=dualrun` and a LAN `LISTEN`.
3. Confirm `journalctl -u zanjerito` shows `gpio/dualrun: intended Set … (bash still actuates)` and **no** `/dev/gpiochip` claims.
4. Phone UI may show watering; that is engine intent, not Go-driven valves.

**STOP / `systemctl stop` in dual-run only clears the Go engine shadow** (`engine.Stop()` / journal `AllOff`). **Bash keeps watering.** Do not treat the red STOP or a unit stop as a valve-off while `DRIVER=dualrun`.

## Front 4/8/8 parity

Golden program: Front West **4** / North **8** / South **8** (`config/front-schedule.example.json`, `schedule.FrontParity()`). Minutes are itinerary time, not bash dead-time.

On a scheduled morning (America/Phoenix):

- [ ] Bash ran west 4, north 8, south 8 (existing logs / wet-check).
- [ ] Go journal intended the same station ids in that order, those durations (± overlap sequencing).
- [ ] No extra station intended ON.
- [ ] `systemctl stop zanjerito` still all-offs in the **engine** (journal `AllOff`); real valves stay under bash.

Repeat until **N good scheduled days** (default **7**, or whatever Nick names). A skip-because-busy (D13) day does not count as good.

## Flip (after N good days, leftover driver)

Only after `gpiocdev` is wired and sat:

1. Disable bash cron / `front.sh` for that program.
2. `DRIVER=gpiocdev` in `zanjerito.env`, restart unit.
3. Watch one manual 1-minute run on a spare/low-risk station, then one Front morning.
4. Keep bash scripts on disk for a season.

## Rollback (bash)

1. `sudo systemctl disable --now zanjerito`
2. Re-enable the `mvp-bash` cron / `front.sh` path.
3. Leave `/opt/zanjerito/config.json` in place (copy-once; do not delete).

Rollback stays valid for a season after flip.
