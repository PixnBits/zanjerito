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
| `ZAN_DIM_LEVEL` | Dim brightness 0–255. Optional. Client default is 51. `0` does not disable the feature. |
| `ZAN_BACKLIGHT` | Sysfs backlight directory. Optional. Default `/sys/class/backlight/rpi_backlight`. |
| `BACKLIGHT` | Optional fixed level 0–255 written once at start. See below. |

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

Dump `/dev/fb0` (800×480, 32 bpp, BGRA, stride 3200 = 800×4):

```sh
dd if=/dev/fb0 of=/tmp/fb0.raw bs=3200 count=480 status=none
python3 - <<'PY'
import struct, zlib, pathlib
w, h, stride = 800, 480, 3200
raw = pathlib.Path("/tmp/fb0.raw").read_bytes()
need = h * stride
if len(raw) < need:
    raise SystemExit(f"short read: {len(raw)} bytes, want {need}")

def chunk(tag, data):
    crc = zlib.crc32(tag + data) & 0xFFFFFFFF
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

rows = []
for y in range(h):
    row = raw[y * stride:y * stride + w * 4]
    px = bytearray()
    for i in range(0, w * 4, 4):
        b, g, r, a = row[i:i + 4]
        px += bytes((r, g, b, a))
    rows.append(b"\x00" + bytes(px))
ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
png = b"\x89PNG\r\n\x1a\n"
png += chunk(b"IHDR", ihdr)
png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
png += chunk(b"IEND", b"")
pathlib.Path("/tmp/fb0.png").write_bytes(png)
PY
```

`/tmp/fb0.png` is the panel. The recipe only reads the framebuffer.

## What changes

- Default target becomes `multi-user.target`. The next boot does not start the graphical login.
- `lightdm` is disabled.
- `zan-kiosk.service` is enabled (`WantedBy=multi-user.target`).
- The unit `Conflicts=` with `lightdm.service` and `getty@tty1.service`, so the display manager and a login prompt on tty1 do not fight for `/dev/fb0`.
- On each start, `prepare` hides the console cursor, turns console blank off, and prepares the backlight. The client then dims and blanks from the timers below. Copying these files does not change a running panel. Applying them on the board is a separate step.

The process runs as `pi`, with supplementary groups `video` and `input`, so it can open `/dev/fb0` and the touch device. `Restart=always`, `RestartSec=2`. `StartLimitIntervalSec=0` is in `[Unit]` (systemd ignores that key in `[Service]`), so a crash does not hit the default start burst.

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

`ExecStopPost=+` runs `reset-backlight`, then `restore-cursor`. There is still no `ExecStop=`. `reset-backlight` writes `max_brightness` into `brightness` and `0` into `bl_power` (best effort). `restore-cursor` writes `ESC[?25h` to `/dev/tty1`.

Kernel messages can still land on tty1 and draw over the framebuffer. Optional, this boot only, and not done by the unit:

```sh
sudo dmesg -n 1
```

Setting the tty to `KD_GRAPHICS` so printk cannot draw there is a future client change.

## Backlight and display power

The Pi panel exposes `rpi_backlight`. `brightness` on that board is `root:root` mode `0644`, so the `pi` user cannot dim until `prepare` runs. `prepare` (root, `ExecStartPre=+`) does this best effort, and logs each skip:

- `chgrp video` and `chmod g+w` on `<dir>/brightness` and, if the file exists, `<dir>/bl_power`. `<dir>` is `ZAN_BACKLIGHT`, or `/sys/class/backlight/rpi_backlight` when that is unset. `ZAN_ROOT` prefixes the directory in tests only.
- If `BACKLIGHT` is unset, write `max_brightness` into `brightness` (the raw panel max, not capped at 255). The client saves that value and restores it on exit.
- If `BACKLIGHT` is set to 0–255, write that fixed level to `/sys/class/backlight/*/brightness` instead. That level is what the client later restores. It is not the idle-dim level.

The example file ships `ZAN_DIM_AFTER_SEC=120` and `ZAN_OFF_AFTER_SEC=600`. The client dims after 120 seconds with no touch, and turns the backlight off 600 seconds after the last touch. `ZAN_DIM_LEVEL` and `ZAN_BACKLIGHT` are commented. Flags on the client win over these variables. Invalid numbers are skipped with a stderr line; the unit still starts.

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
