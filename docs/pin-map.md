# Zanjerito — Pin map (this hardware)

**Status:** Draft v0.2 · 2026-09-12  
**Hardware now:** Raspberry Pi 3 (BCM2837). Typically `/dev/gpiochip0`.  
**Polarity:** active-low relays — logic HIGH ≈ valve/PSU **off**. Match `mvp-bash`.  
**Source of truth at runtime:** the config file. This table is the human SoT.

| Name | Colour | wiringPi | Physical | BCM | Role |
|---|---|---|---|---|---|
| Front West | red | 21 | 29 | GPIO5 | station |
| Front North | yellow | 22 | 31 | GPIO6 | station |
| Drip Line | blue | 23 | 33 | GPIO13 | station |
| Front South | green | 24 | 35 | GPIO19 | station |
| (unused) | — | 25 | 37 | GPIO26 | unused — omit from default config or mark disabled |
| (unused) | — | 27 | 36 | GPIO16 | unused |
| (unused) | — | 28 | 38 | GPIO20 | unused |
| Station PSU | — | 29 | 40 | GPIO21 | 24VAC power enable |

Example config:

```json
{
  "chip": "gpiochip0",
  "active_low": true,
  "timezone": "America/Phoenix",
  "max_on_sec": 900,
  "sequencing": { "mode": "overlap", "overlap_ms": 2000 },
  "power": { "id": "psu", "title": "Station PSU", "bcm": 21, "physical": 40, "wiringPi": 29 },
  "stations": [
    { "id": "front-west",  "title": "Front West",  "color": "red",    "bcm": 5,  "physical": 29, "wiringPi": 21 },
    { "id": "front-north", "title": "Front North", "color": "yellow", "bcm": 6,  "physical": 31, "wiringPi": 22 },
    { "id": "drip",        "title": "Drip Line",   "color": "blue",   "bcm": 13, "physical": 33, "wiringPi": 23 },
    { "id": "front-south", "title": "Front South", "color": "green",  "bcm": 19, "physical": 35, "wiringPi": 24 }
  ]
}
```

Seed schedule (parity fixture, not the only program): Front West 4 min → Front North 8 min → Front South 8 min. Drip is not in that itinerary.

Validate on first boot: refuse to run if `active_low` is missing. Confirm chip with `gpiodetect` / `gpioinfo`.
