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

Edit `zanjerito.env` — set `LISTEN` to **this Pi’s LAN address** (not `0.0.0.0`), e.g. `192.168.1.8:8080`. `DRIVER=gpiocdev` on the Pi (post-cutover). `fake` for laptop. `dualrun` / rollback: [cutover.md](./cutover.md).

## Enable + LAN UI

```sh
sudo systemctl daemon-reload
sudo systemctl enable --now zanjerito
journalctl -u zanjerito -f
# phone on Wi-Fi: http://192.168.1.8:8080/
```

`ExecStop=` sends `SIGTERM` to the main PID. `cmd/zanjerito` treats SIGTERM/SIGINT as `engine.Stop()` (all stations off, then PSU off), then Close.

## Stop / all-off

```sh
sudo systemctl stop zanjerito   # ExecStop → SIGTERM → engine.Stop()
```

If the process is wedged, systemd SIGKILLs after `TimeoutStopSec=15`. Inactive-on-release: `gpiocdev` requests AsOutput(inactive)+AsActiveLow so Close/release de-energizes. Dual-run / flip / rollback: [cutover.md](./cutover.md).
