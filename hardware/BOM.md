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
| Inline fuse holder for 5x20 mm fuses, plus a 1 A 250 V slow-blow (time-delay) fuse | 1 each | On the transformer's 120 VAC primary hot leg, after the power relay. The 40 VA primary draws about 0.33 A; slow-blow rides through transformer inrush | Required |
| Wire: valve field | 1 run | 18 AWG multi-conductor direct-burial sprinkler (irrigation) wire; one conductor per valve plus a shared common. Use 16 AWG for the common on very long runs | Required |
| Wire: 120 VAC side | As needed | Plug to power relay to fuse to transformer primary: 18 AWG stranded, insulation rated 300 V or higher | Required |
| Wire: 5 V supply to Pi and relay board | As needed | 18-20 AWG, kept short to avoid Pi undervoltage | Required |
| Wire: Pi GPIO to relay inputs | As needed | 22-26 AWG (Dupont jumpers are fine) | Required |
| Enclosure: salvaged Orbit B-hyve 6-zone indoor/outdoor smart sprinkler timer housing | 1 | Original electronics removed; the printed screen mount ([`prints/`](prints/)) fits its frame | Installed |

## Wiring

GPIO assignments come from [docs/pin-map.md](../docs/pin-map.md), the source of truth. Relays are active-low (logic HIGH = off).

| BCM | Physical pin | Use | Relay channel |
|---|---|---|---|
| GPIO5 | 29 | Station relay | verify on board |
| GPIO6 | 31 | Station relay | verify on board |
| GPIO13 | 33 | Station relay | verify on board |
| GPIO19 | 35 | Station relay | verify on board |
| GPIO26 | 37 | Unused | verify on board |
| GPIO16 | 36 | Unused | verify on board |
| GPIO20 | 38 | Unused | verify on board |
| GPIO21 | 40 | 24VAC power enable relay | CH8 (top), verify on board |

Channel numbering is believed to be CH1 at the bottom of the board and CH8 at the top; verify on the board's silkscreen. The repo doesn't document which channel each station pin drives.

## Safety

The 24 VAC secondary is PTC-fused inside the transformer, but the 120 VAC primary needs its own fuse (see the inline fuse row in the parts table). Mains wiring should be done by someone qualified, kept inside the enclosure and strain-relieved.
