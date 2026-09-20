# Cutover checklist (D9)

Bash on `mvp-bash` stays the actuator until this list is done. The Go daemon **logs intended GPIO** (`DRIVER=dualrun`) so you can compare journal lines to what bash actually did.

Real `gpiocdev` (inactive-on-release on `/dev/gpiochip0`) must be **merged and deployed** before any DRIVER flip — still run the binary as `dualrun` (or lockout smoke) until the pre-flip gate below is green.

## Drivers

| `DRIVER` | Hardware | Use |
|---|---|---|
| `fake` | none (shadow state) | laptop / CI |
| `dualrun` | none; logs `intended Set` | **D9 overlap** — bash still waters (**Pi default until flip**) |
| `lockout` | none; logs `refuse Set` | safe Pi bring-up without bash compare |
| `gpiocdev` | `/dev/gpiochip0` via go-gpiocdev | only after pre-flip gate |

Set in `/opt/zanjerito/zanjerito.env` (copy-once). Restart: `sudo systemctl restart zanjerito`.

## Dual-run (now)

1. Leave `mvp-bash` / `front.sh` (or cron) **enabled**. Do not disable bash yet.
2. Install Go unit with `DRIVER=dualrun` and a LAN `LISTEN`.
3. Confirm `journalctl -u zanjerito` shows `gpio/dualrun: intended Set … (bash still actuates)` and **no** `/dev/gpiochip` claims.
4. Phone UI may show watering; that is engine intent, not Go-driven valves.

**STOP / `systemctl stop` in dual-run only clears the Go engine shadow** (`engine.Stop()` / journal `AllOff`). **Bash keeps watering.** Do not treat the red STOP or a unit stop as a valve-off while `DRIVER=dualrun`.

## Front parity (live bash vs example)

**Live bash `front.sh` (ops, 2026-09):** Front West **2** / North **3** / South **3** at **08:23** America/Phoenix.

Repo example `config/front-schedule.example.json` / `schedule.FrontParity()` is still West **4** / North **8** / South **8** — that is **not** the live program. The file on disk (`/opt/zanjerito/config.json`) wins; compare journal `start …` lines to whatever is actually scheduled.

On a scheduled morning (America/Phoenix):

- [ ] Bash ran the **live** west/north/south minutes (wet-check / bash logs).
- [ ] Go journal intended the same station ids in that order, those durations (± overlap sequencing).
- [ ] No extra station intended ON.
- [ ] `systemctl stop zanjerito` still all-offs in the **engine** (journal `AllOff`); real valves stay under bash.

Repeat until **N good scheduled days** (default **7**, or whatever Nick names). A skip-because-busy (D13) day does not count as good.

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


All of:

- [ ] **N GOOD** dual-run mornings (journal match vs bash).
- [ ] `gpiocdev` PR **merged** and binary **deployed** to `/opt/zanjerito` while still `DRIVER=dualrun` (or a brief `lockout` smoke — never flip DRIVER early).
- [ ] Nick / portfolio green-light to cut bash cron.

## Flip (copy-paste)

Only after the pre-flip gate:

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
