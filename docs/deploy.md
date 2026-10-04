# Deploy (D8) — systemd + `/opt/zanjerito`

Lean Pi install. Production on this Pi is `DRIVER=gpiocdev` (cutover ~2026-09-22). Flip history and bash rollback: [cutover.md](./cutover.md). This unit runs the Go binary only.

## Build (static)

On the Pi (arch from `uname -m`):

```sh
make build
# aarch64 / arm64 → GOARCH=arm64
# armv7l         → GOARCH=arm GOARM=7
# x86_64         → GOARCH=amd64 (cross from a laptop)
```

Cross-compile from a laptop:

```sh
make build GOARCH=arm64          # 64-bit Pi
make build GOARCH=arm GOARM=7    # 32-bit Pi
```

`CGO_ENABLED=0` — static binary. `go-gpiocdev` is pure Go (ioctl); no CGO. Pi production uses `-driver=gpiocdev` (see [cutover.md](./cutover.md)). Use `fake` on a laptop.

## Install (copy-once config)

```sh
sudo make install
# or: sudo PREFIX=/opt/zanjerito ./deploy/install.sh
```

Writes:

| Path | First install | Later install |
|---|---|---|
| `/opt/zanjerito/zanjerito` | binary | replaced |
| `/opt/zanjerito/config.json` | from `config/pinmap.example.json` | **kept** |
| `/opt/zanjerito/zanjerito.env` | from `deploy/zanjerito.env.example` | **kept** |
| `/etc/systemd/system/zanjerito.service` | unit | replaced |

Edit `zanjerito.env` — set `LISTEN` to **this Pi’s LAN address** (not `0.0.0.0`), e.g. `192.0.2.10:8080`. `DRIVER=gpiocdev` on the Pi (post-cutover). `fake` for laptop. `dualrun` / rollback: [cutover.md](./cutover.md).

If that address is not assigned yet when the unit starts, the process keeps running schedules and valves and retries the API bind in the background (1s, doubling, capped at 30s). Logs show `address not yet available` until the address appears; then the kiosk/phone can connect.

Automatic rain pause reads `rain.local.json` beside `config.json` (or the path in `ZANJERITO_RAIN_CONFIG`). The install script does not write it. Copy `config/rain.local.example.json`, replace `<GAUGE_ID>`, and do not commit the local file. See the README section "Automatic rain pause".

## Enable + LAN UI

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now zanjerito
journalctl -u zanjerito -f
# phone on Wi-Fi: http://192.0.2.10:8080/
```

`ExecStop=` sends `SIGTERM` to the main PID. `cmd/zanjerito` treats SIGTERM/SIGINT as `engine.Stop()` (all stations off, then PSU off), then Close.

## Stop / all-off

```sh
sudo systemctl stop zanjerito   # ExecStop → SIGTERM → engine.Stop()
```

If the process is wedged, systemd SIGKILLs after `TimeoutStopSec=15`. Inactive-on-release: `gpiocdev` requests AsOutput(inactive)+AsActiveLow so Close/release de-energizes. Dual-run / flip / rollback: [cutover.md](./cutover.md).

## Early-boot relay lines (Pi boot config)

At power-on the Pi's GPIO pins float or sit on their SoC default pulls (BCM 9–27 default to pull-down). On active-low relay boards that can briefly energize a relay (an audible tick) until the daemon claims the lines, which happens roughly 13 s after kernel start. The daemon then drives every line inactive.

To drive the relay lines inactive from the firmware stage, add one line to the Pi boot config (`/boot/config.txt`, or `/boot/firmware/config.txt` on newer Raspberry Pi OS), in the global `[all]` section or above any model filter that would not match your Pi:

```
# zanjerito: drive valve/psu relay lines inactive (high) at early boot
gpio=5,6,13,19,21=op,dh
```

Use the BCM pins from your own `config.json` (the valve stations plus the PSU line). `op,dh` (output, drive high) is only correct for active-low wiring (`"active_low": true`), where high means inactive. For active-high wiring use `dl` instead. Confirm every pin before writing. Back up first, for example `sudo cp -p /boot/config.txt /boot/config.txt.pre-early-gpio.bak`. The change takes effect on the next reboot; the running daemon is not affected.

Verify after a reboot: `pinctrl get 5,6,13,19,21` (or `raspi-gpio get 5,6,13,19,21` on older images) should show level high, output, and the journal should show only `Set <id>=false` lines from the daemon. The firmware still needs a second or two before it applies the setting, so a very brief transient at power-on can remain.

Revert: restore the backup (or delete the two lines) and reboot.
