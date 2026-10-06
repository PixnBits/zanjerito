# Bill of materials

Generic part descriptions only. TBD rows are placeholders to fill in.

| Part | Qty | Notes/specs | Status |
|---|---|---|---|
| Raspberry Pi 3 Model B | 1 | armv7l; runs Raspberry Pi OS Bookworm Lite 32-bit | Installed |
| 3.5" 800x480 DSI capacitive touch display (v1.0) | 1 | | Installed |
| 8-channel relay board | 1 | Opto-isolated, active-low inputs, ~15-20 mA per input, JD-VCC jumper | Installed |
| microSD card | 1 | Size TBD | Installed |
| Pi power supply | 1 | Rating TBD | Installed |
| 3D-printed screen mount | 1 | See [`prints/`](prints/) and [README](README.md) | Installed (print-verified) |
| Irrigation valves | TBD | Model TBD | TBD |
| 24VAC transformer | 1 | Rating TBD | TBD |
| Wire | TBD | Gauge/type TBD | TBD |
| Enclosure | 1 | Model TBD | TBD |

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
