# Boot-persistent kiosk

Full-kiosk boot for the native client on a Raspberry Pi 3B. The panel is the 800×480 framebuffer on `/dev/fb0`. The client binary on the Pi is `/opt/zanjerito/zan-kiosk`.

Nothing in this page is enabled by installing the files. The Pi keeps booting the desktop until you run the switch step yourself.

The client is not a second controller. Schedules, pause, and valves stay in the daemon. If the API is down, the screen says "Can't reach the controller". The unit does not `Requires=` `zanjerito.service`.

Today the panel is a transient unit, not this one:

```sh
systemd-run --unit=zan-kiosk --property=User=pi --property=Restart=always \
  --property=RestartSec=2 --property=StartLimitIntervalSec=0 \
  /opt/zanjerito/zan-kiosk --fb /dev/fb0 --api http://CONTROLLER_HOST:8080 --allow-writes
```

That goes away on reboot. The unit here is the boot-persistent replacement. It is not enabled until `--switch`.

## Prerequisites

- Raspberry Pi OS on a Pi 3B. User `pi`. Groups `video` and `input` exist (the unit adds them for the process).
- The daemon is already running (`zanjerito.service`). Do not restart it for this install.
- Framebuffer client, built on a machine with Docker:

```sh
make -C native-kiosk arm
```

That writes `native-kiosk/build/zan-kiosk-arm`. Copy it to the Pi, or build there if the toolchain is present. This tree does not deploy it for you.

## 1. Install files only

From a checkout on the Pi (or with `--bin` pointing at the ARM binary you copied):

```sh
sudo native-kiosk/deploy/install-kiosk.sh --bin native-kiosk/build/zan-kiosk-arm
```

This copies files and runs `systemctl daemon-reload`. It does not enable the unit, does not change the default target, and does not stop `lightdm`.

| Path | Mode | Later install |
|---|---|---|
| `/opt/zanjerito/zan-kiosk` | 755 | replaced; previous file kept as `zan-kiosk.bak.YYYYmmddHHMMSS` |
| `/opt/zanjerito/zan-kiosk-run.sh` | 755 | replaced |
| `/opt/zanjerito/rollback-desktop.sh` | 755 | replaced |
| `/etc/systemd/system/zan-kiosk.service` | 644 | replaced |
| `/etc/default/zan-kiosk` | 644 | **kept** if it already exists |

`/opt/zanjerito/zanjerito`, `config.json`, and `zanjerito.env` are not touched.

`--dry-run` prints each action as `DRY-RUN:` and writes nothing.

## 2. Edit `/etc/default/zan-kiosk`

Set `ZAN_API` to the controller base URL. `http://CONTROLLER_HOST:8080` is the placeholder. `--switch` refuses it. Use an IPv4 literal. A hostname in `--api` is resolved with `getaddrinfo`, which has no timeout.

Writes are opt-in. `ZAN_ALLOW_WRITES` is commented out, so the panel is read-only. An existing install may already have writes on. Set `ZAN_ALLOW_WRITES=1` only if this screen should send STOP, pause, and resume.

Optional:

| Variable | Meaning |
|---|---|
| `ZAN_FB` | Framebuffer. Default `/dev/fb0`. |
| `ZAN_TOUCH` | evdev device. Omit to autodetect. |
| `ZAN_EXTRA_ARGS` | Extra client arguments, split on spaces. |
| `ZAN_DIM_AFTER_SEC` | Seconds with no touch before dim. Example ships `120`. `0` disables dim and off. |
| `ZAN_OFF_AFTER_SEC` | Seconds from the last touch before the backlight goes off. Example ships `600`. `0` disables off only. Must be greater than dim, or off stays off. |
| `ZAN_DIM_LEVEL` | Dim brightness 0–255. Optional. The commented example matches the client default, 51. `0` does not disable the feature. |
| `ZAN_BACKLIGHT` | Sysfs backlight device name or directory. Optional. Unset: auto-detect `rpi_backlight`, then `10-0045`, then the first writable device. Invalid values fall back to auto-detect. |
| `BACKLIGHT` | Optional fixed level 0–255 written once at start to that directory only. See below. |

## 3. Switch on next boot

```sh
sudo native-kiosk/deploy/install-kiosk.sh --switch
sudo reboot
```

`--switch` refuses unless `/etc/default/zan-kiosk` exists and `ZAN_API` is not the placeholder. When it runs, the only `systemctl` commands it adds are:

```sh
systemctl set-default multi-user.target
systemctl disable lightdm
systemctl enable zan-kiosk
```

It does not stop or restart the daemon.

## 4. Switch without rebooting

```sh
sudo native-kiosk/deploy/install-kiosk.sh --switch --now
```

Same as `--switch`, then:

```sh
systemctl stop lightdm
systemctl start zan-kiosk
```

## Check

```sh
systemctl is-active zan-kiosk          # active
systemctl is-active lightdm            # inactive
systemctl is-active zanjerito          # still active
systemctl get-default                  # multi-user.target
sudo journalctl -u zan-kiosk -n 50 --no-pager
```

The daemon's active state should be unchanged from before the switch. This install does not restart it.

The client draws 16 bpp RGB565 or 32 bpp XRGB8888, picked from the framebuffer's `bits_per_pixel` and channel layout. Anything else makes the running client exit 78. A startup line `fb: ... unsupported framebuffer format` in `journalctl -u zan-kiosk` means this build cannot draw on that framebuffer. `--fbshot` still exits 1 for an unsupported format.

`--script` `shot:`, `--shot`, and `--shot-all` render the in-app memory display. They do not prove what the panel shows. Deploy verification must use `--fbshot`, which reads the real framebuffer at whatever depth it is. The `pi` user is in group `video`:

```sh
sudo -u pi /opt/zanjerito/zan-kiosk --fb /dev/fb0 --fbshot /tmp/fb0.png
```

Pass: exit 0, and `nonblack` well above zero (more than half the pixels for the normal dashboard). Then look at `/tmp/fb0.png`. Exit 3 means the frame was read and every pixel is black.

## What changes

- Default target becomes `multi-user.target`. The next boot does not start the graphical login.
- `lightdm` is disabled.
- `zan-kiosk.service` is enabled (`WantedBy=multi-user.target`).
- The unit `Conflicts=` with `lightdm.service` and `getty@tty1.service`, so the display manager and a login prompt on tty1 do not fight for `/dev/fb0`.
- The unit is `After=plymouth-quit.service` (and still `After=` `network-online.target` and `zanjerito.service`). It does not `Wants=` or `Requires=` Plymouth. See [Plymouth splash](#plymouth-splash).
- On each start, `prepare` hides the console cursor, turns console blank off, and prepares the backlight. The client then dims and blanks from the timers below. Copying these files does not change a running panel. Applying them on the board is a separate step.

## Plymouth splash

Without ordering, the kiosk can paint Home and then lose it. On a Pi boot journal (kernel-relative seconds), `zan-kiosk` started at 12.38 s while "Terminate Plymouth Boot Screen" (`plymouth-quit.service` / `plymouth-quit-wait.service`) ran 12.72–12.83 s, and `plymouthd` received quit signals at about 12.77 / 12.81 s. Splash teardown can clear `/dev/fb0` after the kiosk's first frame. Home is static, so nothing repaints. Observed sequence: rainbow, Pi OS boot screen, then a blank panel.

`After=plymouth-quit.service` waits for the unit that sends `plymouth quit`. `After=` on a missing unit is ignored, so a board without Plymouth still starts. There is no `Wants=` or `Requires=` on Plymouth.

Do not `After=plymouth-quit-wait.service`. Its `multi-user.target` ordering differs between images, and this kiosk is `WantedBy=multi-user.target`, so leaving it out rules out any ordering loop. On Raspberry Pi OS Buster (systemd 241), `plymouth-quit.service` is `After=basic.target` and `Before=multi-user.target`, so the chosen `After=` cannot form a cycle. `plymouth-quit-wait` only waits for the quit to finish; ordering after `plymouth-quit.service` is enough.

The client also forces a full-screen redraw once at about 2 s after the first rendered frame, and once more at about 5 s (monotonic clock, two one-shots, never periodic). That covers a quit that still clears `fb0` after the kiosk has started. The paint is skipped while display power is dimmed or off; it does not wake or brighten the panel and does not touch the API.

After a reboot, the panel should show Home. `zanjerito.service` must stay active; this unit does not restart the daemon. Check with `systemctl is-active zan-kiosk`, `systemctl is-active zanjerito`, and `journalctl -u zan-kiosk -b`.

The process runs as `pi`, with supplementary groups `video` and `input`, so it can open `/dev/fb0` and the touch device. `Restart=always`, `RestartSec=2`. `RestartPreventExitStatus=78` stops that loop only when the framebuffer format is unsupported. Exit 1 still restarts, including a framebuffer that is not there yet at boot. `StartLimitIntervalSec=0` is in `[Unit]` (systemd ignores that key in `[Service]`), so a crash does not hit the default start burst.

## What does not change

- `zanjerito.service` is not modified, stopped, or restarted.
- `config.json`, `zanjerito.env`, cron, and the old bash scripts are not touched.
- Boot files are not touched: `config.txt` and `cmdline.txt` (under `/boot` or `/boot/firmware`). Early-boot blanking stays as the image set it.

## No STOP when the kiosk stops

The unit has no `ExecStop=` that sends STOP or all-off.

The kiosk is only a client. Valve safety lives in `zanjerito.service`: SIGTERM runs engine all-off, and the `max_on` limit is enforced there. A STOP on every kiosk stop, restart, or crash-restart would cancel a scheduled or manual run for no reason. It would also depend on the kiosk being able to POST. An earlier Chromium-unit draft all-off when the UI stopped. That belongs on the daemon unit, which already has it.

Stopping `zan-kiosk` kills the client. Water that is already on keeps running until the daemon stops it.

## Cursor and console blank

`prepare` runs as root before the client (`ExecStartPre=+`). Every step is best-effort and is logged to the journal. A missing sysfs file does not fail the unit.

- If `/sys/class/graphics/fbcon/cursor_blink` exists, write `0`. That stops the fbcon cursor blinking on the framebuffer.
- Write `ESC[?25l` to `/dev/tty1` to hide the VT cursor.
- Write `ESC[9;0]` to `/dev/tty1` to set the console blank timeout to 0 minutes.
- If `setterm` exists, run `setterm --blank 0 --powerdown 0 --cursor off` with stdin and stdout on `/dev/tty1`. A failure is ignored.

`ExecStopPost=+` runs `reset-backlight`, then `restore-cursor`. There is still no `ExecStop=`. `reset-backlight` writes `max_brightness` into `brightness` and `0` into `bl_power` (best effort). After exit 78 it writes brightness `0` and `bl_power` `4` instead, so a panel that cannot be drawn does not stay lit. `restore-cursor` writes `ESC[?25h` to `/dev/tty1`.

Kernel messages can still land on tty1 and draw over the framebuffer. Optional, this boot only, and not done by the unit:

```sh
sudo dmesg -n 1
```

Setting the tty to `KD_GRAPHICS` so printk cannot draw there is a future client change.

## Backlight and display power

The Pi panel exposes `rpi_backlight`. `brightness` on that board is `root:root` mode `0644`, so the `pi` user cannot dim until `prepare` runs. On Bookworm with KMS, the official 7-inch DSI panel backlight appears as `10-0045` instead; `brightness` may already be group `video`, `bl_power` is root-only, and `prepare` handles it. `prepare` (root, `ExecStartPre=+`) does this best effort, and logs each skip:

- `chgrp video` and `chmod g+w` on `<dir>/brightness` and, if the file exists, `<dir>/bl_power`. `<dir>` is `ZAN_BACKLIGHT` when that names an existing device, else auto-detect (`rpi_backlight`, then `10-0045`, then the first writable device). `ZAN_ROOT` prefixes the directory in tests only.
- Write `0` to `<dir>/bl_power` when that file exists (panel on). Configured path only, not every backlight device. Absence or a failed write is logged and ignored.
- If `BACKLIGHT` is unset, write `max_brightness` into `brightness` (the raw panel max, not capped at 255). The client reads that value at startup and restores it on exit.
- If `BACKLIGHT` is set to 0–255, write that fixed level only to `<dir>/brightness` (the same directory as above, not every backlight device). That level is what the client later restores. It is not the idle-dim level.

The example file ships `ZAN_DIM_AFTER_SEC=120` and `ZAN_OFF_AFTER_SEC=600`. The client dims after 120 seconds with no touch, and turns the backlight off 600 seconds after the last touch. `ZAN_DIM_LEVEL` is commented at 51, matching the client default. `ZAN_BACKLIGHT` is commented. Flags on the client win over these variables. Invalid numbers are skipped with a stderr line; the unit still starts.

The client reads `brightness` and `max_brightness` at startup, not on the first dim. A dim writes the minimum of the dim level (never below 1 when the saved brightness was at least 1), that saved brightness, and the panel max, so a dim never raises the panel. If the saved brightness is 0, dimming does not write; wake and exit restore `max_brightness`. If the original could not be read, the dim write is min(dim level, max) and restore writes max.

`ZAN_DIM_AFTER_SEC=0` disables dimming and off. The client does not open the backlight directory. `ZAN_OFF_AFTER_SEC=0` disables off only. If off is not strictly greater than dim, off is disabled rather than raised.

While a run is watering, the panel is locked out, a fault or `last_error` is set, the controller is unreachable (including needs-update), or a confirm modal is open, the screen stays on and the idle clock does not advance. Pause may dim and never turns the backlight off. A touch while the backlight is off is swallowed (the screen wakes, and that tap does not press a control), including STOP. A touch while dimmed is swallowed except STOP, which passes on that first tap and wakes. The swallow lasts until the finger lifts plus 300 ms.

A missing directory, or a brightness node that cannot be written, is not fatal. The client logs it once and keeps running. With no writable backlight, dim is a no-op. Off then uses `FBIOBLANK` only when the framebuffer path is a real `/dev/fbN`.

To turn the feature off without removing the unit, set `ZAN_DIM_AFTER_SEC=0` in `/etc/default/zan-kiosk` and restart the kiosk unit (do not restart the irrigation daemon for that).

If the panel is left black, restore the backlight as root. This does not stop the daemon:

```sh
sudo /opt/zanjerito/zan-kiosk-run.sh reset-backlight
```

Rollback runs that same subcommand, best effort, after it stops the kiosk. A failure there does not fail the desktop restore. Nothing in this tree applies these steps on a board; that install is a separate step.

## If the panel is blank

SSH in. The daemon is still running; the kiosk is only the screen. Roll the desktop back (next section). You do not need the panel for that.

## Rollback

```sh
sudo /opt/zanjerito/rollback-desktop.sh
```

If that file is gone, the same steps by hand:

```sh
sudo systemctl disable --now zan-kiosk && sudo systemctl enable lightdm && sudo systemctl set-default graphical.target && sudo systemctl start lightdm
```

`disable --now` stops the kiosk first. Then lightdm is enabled, the default target is `graphical.target` again, and lightdm is started. Each step runs even if an earlier one failed. The script exits non-zero if any step failed. It does not stop or restart the daemon.

The transient unit (the `systemd-run` command above, not an enabled boot unit) is cleared with:

```sh
sudo systemctl stop zan-kiosk && sudo systemctl start lightdm
```

That does not change the default target. After `--switch`, use the longer command.

`--dry-run` on `rollback-desktop.sh` prints the four systemctl commands and `reset-backlight`, and runs none.

## Reversibility

Rollback restores lightdm and `graphical.target`. The installed files can stay. To remove them, without removing the daemon:

```sh
sudo rm -f /opt/zanjerito/zan-kiosk \
  /opt/zanjerito/zan-kiosk-run.sh \
  /opt/zanjerito/rollback-desktop.sh \
  /etc/systemd/system/zan-kiosk.service \
  /etc/default/zan-kiosk
sudo rm -f /opt/zanjerito/zan-kiosk.bak.*
sudo systemctl daemon-reload
```

Do not remove `/opt/zanjerito/zanjerito`, `config.json`, or `zanjerito.env` as part of this.
