# Deploy (D8) — systemd + `/opt/zanjerito`

Lean Pi install. Production on this Pi is `DRIVER=gpiocdev` (cutover ~2026-09-22). Flip history and bash rollback: [cutover.md](./cutover.md). The default unit runs the Go binary only (headless API-only Pis stay valid).

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

Edit `zanjerito.env` — set `LISTEN` to **this Pi’s LAN address** (not `0.0.0.0`), e.g. `192.168.5.51:8080`. `DRIVER=gpiocdev` on the Pi (post-cutover). `fake` for laptop. `dualrun` / rollback: [cutover.md](./cutover.md).

## Enable + boot-start + LAN UI

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now zanjerito   # boot-start
journalctl -u zanjerito -f
# phone on Wi-Fi: http://192.168.5.51:8080/
# wall density:   http://192.168.5.51:8080/?mode=kiosk
```

`enable --now` both starts immediately and enables boot-start via `WantedBy=multi-user.target`.

`ExecStop=` sends `SIGTERM` to the main PID. `cmd/zanjerito` treats SIGTERM/SIGINT as `engine.Stop()` (all stations off, then PSU off), then Close.

## Update / redeploy (Idle-gate)

Before replacing the binary on a live Pi:

```sh
curl -s http://192.168.5.51:8080/api/status   # must show phase Idle (not watering)
# then build + install, e.g. from a laptop worktree:
make build GOARCH=arm GOARM=7
# copy binary / sudo make install on the Pi — do not touch cron or front.sh
sudo systemctl restart zanjerito
```

Do **not** restart while `phase` is watering. Idle-gate still applies for deploys.

## Stop / all-off

```sh
sudo systemctl stop zanjerito   # ExecStop → SIGTERM → engine.Stop()
```

If the process is wedged, systemd SIGKILLs after `TimeoutStopSec=15`. Inactive-on-release: `gpiocdev` requests AsOutput(inactive)+AsActiveLow so Close/release de-energizes. Dual-run / flip / rollback: [cutover.md](./cutover.md).

## Optional: Chromium kiosk unit (wall screen)

Separate unit: `deploy/zanjerito-kiosk.service`. Fullscreen Chromium to the LAN UI `?mode=kiosk`.

**Headless-safe by design:**

- `WantedBy=graphical.target` (not `multi-user.target`) — no display session ⇒ unit never pulled in
- Default `make install` / `./deploy/install.sh` does **not** install or enable it
- Does not `Require=` / `BindsTo=` `zanjerito.service` — API-only Pis unchanged

Opt-in install (copies unit only; you still enable):

```sh
sudo INSTALL_KIOSK=1 ./deploy/install.sh
# edit KIOSK_URL in the unit (or a drop-in) to match LISTEN
sudo systemctl daemon-reload
sudo systemctl enable --now zanjerito-kiosk
```

Or copy by hand:

```sh
sudo install -m 644 deploy/zanjerito-kiosk.service /etc/systemd/system/
# adjust Environment=KIOSK_URL=http://<pi-lan>:8080/?mode=kiosk
sudo systemctl daemon-reload
sudo systemctl enable --now zanjerito-kiosk
```

Prereqs: Raspberry Pi OS with a desktop/session (`DISPLAY=:0`), `chromium-browser` (or adjust `ExecStart` to `/usr/bin/chromium`), and `zanjerito` already listening on LAN.

Disable without touching irrigation:

```sh
sudo systemctl disable --now zanjerito-kiosk
```

PWA / offline install is **out of scope** here — browser kiosk only.
