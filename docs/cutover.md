# Cutover checklist (D9)

**Status (2026-09):** Flip **completed** ~**2026-09-22**. Production on the Pi is `DRIVER=gpiocdev`; Front bash cron is commented. First live Front morning **wet-check PASS 2026-09-23** (08:23–08:31 PT, west/north/south **2/3/3**). Overlap sequencing unchanged. Bash scripts remain on disk for a season — rollback copy-paste is at the bottom.

The sections below keep the dual-run / pre-flip / flip procedure for history and for anyone replaying cutover on another site. Do **not** re-flip DRIVER or touch Pi cron from a docs PR.

---

## Drivers

| `DRIVER` | Hardware | Use |
|---|---|---|
| `fake` | none (shadow state) | laptop / CI |
| `dualrun` | none; logs `intended Set` | D9 overlap — bash still waters (pre-flip) |
| `lockout` | none; logs `refuse Set` | safe Pi bring-up without bash compare |
| `gpiocdev` | `/dev/gpiochip0` via go-gpiocdev | **production on this Pi (post-flip)** |

Set in `/opt/zanjerito/zanjerito.env` (copy-once). Restart: `sudo systemctl restart zanjerito`.

## Dual-run (historical — pre-flip)

1. Leave `mvp-bash` / `front.sh` (or cron) **enabled**. Do not disable bash yet.
2. Install Go unit with `DRIVER=dualrun` and a LAN `LISTEN`.
3. Confirm `journalctl -u zanjerito` shows `gpio/dualrun: intended Set … (bash still actuates)` and **no** `/dev/gpiochip` claims.
4. Phone UI may show watering; that is engine intent, not Go-driven valves.

**STOP / `systemctl stop` in dual-run only clears the Go engine shadow** (`engine.Stop()` / journal `AllOff`). **Bash keeps watering.** Do not treat the red STOP or a unit stop as a valve-off while `DRIVER=dualrun`.

## Front parity (live vs example)

**Live Front program (ops, 2026-09):** Front West **2** / North **3** / South **3** at **08:23** America/Phoenix.

Repo example `config/front-schedule.example.json` / `schedule.FrontParity()` is still West **4** / North **8** / South **8** — that is **not** the live program. The file on disk (`/opt/zanjerito/config.json`) wins; compare journal `start …` lines to whatever is actually scheduled.

On a scheduled morning (America/Phoenix), pre-flip checklist was:

- [x] Bash ran the **live** west/north/south minutes (wet-check / bash logs) — dual-run era.
- [x] Go journal intended the same station ids in that order, those durations (± overlap sequencing).
- [x] No extra station intended ON.
- [x] Post-flip wet-check **PASS 2026-09-23** (Go + `gpiocdev` watering west/north/south 2/3/3).

## gpiocdev dry smoke (pre-cutover)

`-dry` is **only meaningful with** `-driver=gpiocdev`. Setup still opens `gpiochip0` and requests lines AsOutput(inactive)+AsActiveLow (permissions + pin map). `Set(On)` is a no-op that logs `gpio/gpiocdev-dry: refuse energize …`. AllOff/Close still release cleanly.

**Quiet window required:** character-device line claim conflicts with bash GPIO. Pause the bash Front cron / do not run `channel.sh` while smoking, then restore bash before leaving dual-run.

```sh
# pause bash Front cron first (same line you will later disable for flip)
crontab -l | sed '/front\.sh/s/^/# /' | crontab -

# one-shot smoke (do not change zanjerito.env DRIVER=)
sudo /opt/zanjerito/zanjerito -config /opt/zanjerito/config.json -driver=gpiocdev -dry -listen ''
# Ctrl-C after journal shows setup + any refuse energize lines; Close releases

# restore bash cron
crontab -l | sed '/front\.sh/s/^# //' | crontab -
```

Do **not** set `DRIVER=gpiocdev` in `zanjerito.env` for this smoke.

## Pre-flip gate

All of (completed for this Pi):

- [x] **N GOOD** dual-run mornings (journal match vs bash).
- [x] `gpiocdev` PR **merged** and binary **deployed** to `/opt/zanjerito` while still `DRIVER=dualrun` (or a brief `lockout` smoke — never flip DRIVER early).
- [x] Nick / portfolio green-light to cut bash cron (~2026-09-22).

## Flip (copy-paste) — done ~2026-09-22

```sh
# 1) Disable bash Front cron (example — match the real crontab line)
crontab -l | sed '/front\.sh/s/^/# /' | crontab -
# or: sudo sed -i '/front\.sh/s/^/# /' /etc/cron.d/zanjerito   # if system crontab

# 2) Flip driver + restart
sudo sed -i 's/^DRIVER=.*/DRIVER=gpiocdev/' /opt/zanjerito/zanjerito.env
grep '^DRIVER=' /opt/zanjerito/zanjerito.env   # expect DRIVER=gpiocdev
sudo systemctl restart zanjerito
journalctl -u zanjerito -n 50 --no-pager   # expect gpio/gpiocdev: setup …

# 3) 1-minute manual on a low-risk station (phone UI or API), watch valves
# 4) Next Front morning: wet-check west/north/south vs schedule
```

Keep bash scripts on disk for a season.

## Rollback (copy-paste)

```sh
sudo systemctl disable --now zanjerito
# Re-enable the mvp-bash Front cron line (uncomment the same line you disabled)
crontab -l | sed '/front\.sh/s/^# //' | crontab -
# Leave /opt/zanjerito/config.json in place (copy-once; do not delete)
```

Rollback stays valid for a season after flip.
