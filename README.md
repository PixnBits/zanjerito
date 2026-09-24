# zanjerito
A little irrigation controller.

> Zanjero is Spanish for "ditch rider." Since the late 1800s, zanjeros have played a vital role in the control and flow of water in the Valley. They traveled hundreds of miles along canals (first by horse, then by truck) and opened head gates to release water from the major canals into smaller canals and pipes that deliver the water that eventually comes out of our faucets and grows our food.

https://www.srpnet.com/water/canals/azfallstour/Zanjero.aspx

I had a name-brand commercial drip irrigation system controller, but first the WiFi system stopped working and then it stopped turning on valves. Raspberry Pis are easy to switch out, and Open-Source Software is great for fixing usability issues. Here's an attempt to do it "right".

## Production (v2 Go on the Pi)

**Live actuator** is the Go binary on the Pi with `DRIVER=gpiocdev` (cutover ~2026-09-22). Front bash cron is commented; bash scripts stay on disk for rollback.

- [docs/deploy.md](docs/deploy.md) — build, install, systemd, env
- [docs/cutover.md](docs/cutover.md) — completed flip notes + bash rollback copy-paste
- [docs/prd.md](docs/prd.md) — product vision / PRD
- [docs/decisions.md](docs/decisions.md) — prioritized decisions
- [docs/architecture.md](docs/architecture.md) — runtime layers, state machine, API sketch
- [docs/pin-map.md](docs/pin-map.md) — this hardware’s pin table + example config
- [docs/ui-directions.md](docs/ui-directions.md) — UI directions (Direction D selected) + v1 explore
- [docs/impl-plan.md](docs/impl-plan.md) — scoped PR implementation plan

### Go binary + systemd

See [docs/deploy.md](docs/deploy.md). Lean path is `/opt/zanjerito`.

```sh
make build          # static; arch from uname -m (or GOARCH=arm64)
sudo make install   # binary + copy-once config/env + unit
sudo systemctl enable --now zanjerito
# phone: http://<pi-lan>:8080/   (set LISTEN in /opt/zanjerito/zanjerito.env)
# wall:  http://<pi-lan>:8080/?mode=kiosk
```

`systemctl stop` sends SIGTERM; the binary `engine.Stop()`s (all-off) before exit. With `DRIVER=gpiocdev`, that de-energizes valves (inactive-on-release).

## Developing

Primary path is the Go binary (same as production):

```sh
git clone https://github.com/PixnBits/zanjerito.git
cd zanjerito
git checkout fancy-vibes
make build
./zanjerito -config config/config.example.json -driver=fake -listen 127.0.0.1:8080
# phone UI: http://127.0.0.1:8080/
# wall / kiosk density: http://127.0.0.1:8080/?mode=kiosk
```

The older Node 14 / GraphiQL app under this tree is a **behavioral reference** only (not the production UI or Developing default). Prefer the embedded Direction D UI served by the Go binary.
