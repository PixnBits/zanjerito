# Bill of materials

TBD entries are placeholders to fill in. No shop or affiliate links.

| Part | Qty | Notes/specs | Status |
|---|---|---|---|
| Raspberry Pi 3 Model B | 1 | armv7l; runs Raspberry Pi OS Bookworm Lite 32-bit | Installed |
| Osoyoo 3.5" 800x480 DSI capacitive touch display (v1.0) | 1 | | Installed |
| JBtek 8-channel relay board | 1 | Opto-isolated, active-low inputs, ~15-20 mA per input, JD-VCC jumper | Installed |
| microSD card | 1 | 16 GB installed. Measured use is about 2.2 GB (root plus boot). Recommended minimum is 8 GB (about 2x used plus 2 GB headroom for upgrades, logs and a rollback margin); 16 GB is the practical choice for endurance and availability | Installed |
| 5 V DC supply: Mean Well RS-15-5 | 1 | 5 V 3 A 15 W, 88-264 VAC in, screw terminals. Powers the Pi and directly feeds the relay board | Installed |
| 3D-printed screen mount | 1 | See [`prints/`](prints/) and [README](README.md) | Installed (print-verified) |
| Standard 24 VAC sprinkler valve solenoids | 4 | One per active station relay (see Wiring) | Installed |
| 24 VAC valve transformer: Hotop PS-D40 plug-in transformer internals | 1 | 120 VAC in, 24 VAC 40 VA out, PTC-fused secondary. Model believed; confirm on unit | Installed |
| Wire | TBD | Gauge/type TBD | TBD |
| Enclosure: salvaged Orbit B-hyve 6-zone indoor/outdoor smart sprinkler timer housing | 1 | Original electronics removed; the printed screen mount ([`prints/`](prints/)) fits its frame | Installed |

## Wiring

GPIO assignments come from [docs/pin-map.md](../docs/pin-map.md), the source of truth. Relays are active-low (logic HIGH = off).

| BCM | Physical pin | Use |
|---|---|---|
| GPIO5 | 29 | Station relay |
| GPIO6 | 31 | Station relay |
| GPIO13 | 33 | Station relay |
| GPIO19 | 35 | Station relay |
| GPIO26 | 37 | Unused |
| GPIO16 | 36 | Unused |
| GPIO20 | 38 | Unused |
| GPIO21 | 40 | 24VAC power enable relay |

Relay board channel numbers for each pin: TBD (not documented in the repo).
