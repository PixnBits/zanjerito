# zanjerito
A little irrigation controller.

> Zanjero is Spanish for "ditch rider." Since the late 1800s, zanjeros have played a vital role in the control and flow of water in the Valley. They traveled hundreds of miles along canals (first by horse, then by truck) and opened head gates to release water from the major canals into smaller canals and pipes that deliver the water that eventually comes out of our faucets and grows our food.

https://www.srpnet.com/water/canals/azfallstour/Zanjero.aspx

The commercial box failed as an update channel: the Wi-Fi daughter board could not be replaced, so software died with the manufacturer's hardware. Raspberry Pis and relay boards are easy to switch out. Open-source software is updated on a channel that is not "buy their next board."

Household-first, copyable guts. Prove it on this Phoenix drip system; pin map is config so someone else can point it at their board.

## Rewrite (v2)

Vision lives on `rewrite/vision` (see also [#12](https://github.com/PixnBits/zanjerito/issues/12)):

- [docs/prd.md](docs/prd.md) — product vision / PRD
- [docs/decisions.md](docs/decisions.md) — prioritized decisions
- [docs/architecture.md](docs/architecture.md) — runtime layers, state machine, API sketch
- [docs/pin-map.md](docs/pin-map.md) — this hardware’s pin table + example config
- [docs/ui-directions.md](docs/ui-directions.md) — UI directions (Direction D selected)

Production today remains the bash scripts on `mvp-bash`. The Node app on `fancy-vibes` is a behavioral reference, not the destination. The Go runtime is on `rewrite/go*`.

Do not run Node on the Pi for production.

## Developing the reference Node app (`fancy-vibes` only)

```shell
$ git clone https://github.com/PixnBits/zanjerito.git
$ git checkout fancy-vibes
$ cd zanjerito
$ nvm use 14
$ npm ci
$ npm start
# http://localhost:3000/ — GraphiQL is reference-only, not a product surface
```
