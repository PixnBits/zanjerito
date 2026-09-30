# Boot-persistent kiosk

Full-kiosk boot for the native client on a Raspberry Pi 3B. The panel is the Osoyoo 800×480 framebuffer on `/dev/fb0`. The client binary on the Pi is `/opt/zanjerito/zan-kiosk`.

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
| `BACKLIGHT` | Fixed backlight, 0–255. See below. |

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
- On each start, `prepare` hides the console cursor and turns console blank off. It can set a fixed backlight.

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

`ExecStopPost=+` runs `restore-cursor`, which writes `ESC[?25h` to `/dev/tty1`.

Kernel messages can still land on tty1 and draw over the framebuffer. Optional, this boot only, and not done by the unit:

```sh
sudo dmesg -n 1
```

Setting the tty to `KD_GRAPHICS` so printk cannot draw there is a future client change.

## Backlight

The Pi panel exposes `rpi_backlight` with a max of 255. If `BACKLIGHT` is set to 0–255, `prepare` writes that value to `/sys/class/backlight/*/brightness`. The level is fixed for the whole run. The client has no idle dim. A screen timeout needs client support; it is not this unit.

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

`--dry-run` on `rollback-desktop.sh` prints the four commands and runs none.

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
