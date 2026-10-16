# RAM `/var/log` (SD wear)

Optional log2ram-style setup for a Raspberry Pi: `/var/log` is a **16M tmpfs**, synced back to the SD card **hourly** and on **clean shutdown**. It is not part of `make install`. Watering history, pause state, rain/soil files, `config.json`, and everything else under `/opt/zanjerito` stay on the card.

## What lives where

| Path | Where | Notes |
|---|---|---|
| `/var/log` | 16M tmpfs | syslog, apt, journal, and other loggers |
| `/var/hdd.log` | bind of the SD `/var/log` | persistent copy; do not write here by hand |
| `/opt/zanjerito/*` | SD | daemon, `config.json`, `history.json`, `pause.json`, rain/soil local files, env |
| journald | capped | `SystemMaxUse=10M`, `RuntimeMaxUse=10M` |

Unclean power loss **loses up to about one hour of logs** (whatever was not yet synced). Watering state is unaffected.

## Install

From a checkout, while idle (not during a watering run):

```sh
sudo deploy/logs-ram/install-logs-ram.sh
sudo reboot
```

The script is safe to re-run. It copies units and scripts, writes a journald drop-in, creates `/var/hdd.log`, and enables `zanjerito-logs-ram.service` plus `zanjerito-log-sync.timer`. It does **not** start the ram unit on the live system (journald is already using `/var/log`). A reboot mounts the tmpfs before journald.

If `/var/log` is already larger than 16M, shrink it before reboot:

```sh
sudo journalctl --vacuum-size=8M
sudo du -sk /var/log
```

`--dry-run` prints actions and writes nothing. `DESTDIR=/path` prefixes destinations (for a staged copy; units are not enabled).

Refuse this install if **log2ram** is already present (both use `/var/hdd.log`).

Files:

| Source | Installed |
|---|---|
| `zanjerito-logs-ram.sh` | `/usr/local/sbin/zanjerito-logs-ram.sh` |
| `zanjerito-log-sync.sh` | `/usr/local/sbin/zanjerito-log-sync.sh` |
| `zanjerito-logs-ram.service` | `/etc/systemd/system/zanjerito-logs-ram.service` |
| `zanjerito-log-sync.service` | `/etc/systemd/system/zanjerito-log-sync.service` |
| `zanjerito-log-sync.timer` | `/etc/systemd/system/zanjerito-log-sync.timer` |
| `journald-zanjerito.conf` | `/etc/systemd/journald.conf.d/zanjerito.conf` |

Replaced files (if any existed) are backed up once under `/var/backups/zanjerito-logs-ram/`.

## Boot and sync

At boot, `zanjerito-logs-ram.service` runs before journald and other loggers:

1. Bind-mount `/var/log` to `/var/hdd.log` (SD copy).
2. Mount a 16M tmpfs on `/var/log`.
3. Copy `/var/hdd.log` → `/var/log`.
4. Ensure `/var/log/journal` exists so journald can persist onto the tmpfs.

`zanjerito-log-sync.timer` (`OnCalendar=hourly`, `Persistent=true`) runs `zanjerito-log-sync.sh`: rsync when present, otherwise a staging `cp -a` so a failed copy leaves the SD tree in place. The ram unit's `ExecStop=` runs the same sync, then unmounts, so a clean reboot or halt keeps the last logs.

The daemon and kiosk do not wait on this. Boot still works with no network.

## Check

After reboot:

```sh
findmnt /var/log            # tmpfs, source zanjerito-logs-ram, size 16M
df -h /var/log
journalctl --disk-usage     # under the 10M cap
systemctl list-timers zanjerito-log-sync.timer
systemctl is-active zanjerito-logs-ram.service zanjerito zan-kiosk
```

A logger line should survive a **clean** reboot:

```sh
logger zanjerito-ramlog-test
sudo reboot
# after boot:
journalctl -b -1 | grep zanjerito-ramlog-test
```

`GET /api/status` should still be 200. logrotate keeps running against `/var/log` on the tmpfs; the hourly sync copies the result to the card.

## Power loss and recovery

A hard power cut skips `ExecStop`. The card has the last **hourly** (or last clean shutdown) snapshot. On the next boot that snapshot is copied into the new tmpfs. The missing window is gone; there is nothing to replay.

If the ram unit fails at boot because `/var/hdd.log` is larger than 16M, the bind is undone and `/var/log` stays on the card. Vacuum, then `sudo systemctl start zanjerito-logs-ram.service` or reboot.

If journal files look torn after a crash:

```sh
sudo journalctl --verify
sudo journalctl --vacuum-size=8M
```

## Uninstall

```sh
sudo deploy/logs-ram/uninstall-logs-ram.sh
```

Stops the timer and ram unit (sync + unmount), restores any backed-up files, removes the units/scripts/drop-in, deletes `/var/hdd.log` if it is an empty unmounted directory, and restarts journald so the 10M cap is dropped. Disk `/var/log` is in use again. The irrigation daemon and `/opt/zanjerito` are not changed. Safe to re-run. A reboot is optional.
