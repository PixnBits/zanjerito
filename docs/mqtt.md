# MQTT discovery (read-only sidecar)

A separate `zanjerito-mqtt` process publishes Home Assistant MQTT device discovery and state from `GET /api/ha`. It sits alongside the REST integration in [home-assistant.md](./home-assistant.md). The irrigation daemon never imports a broker client and does not depend on this process.

The sidecar is read-only. There are no command topics. Stop, pause, and manual run stay out of scope (Phase 2, issue #49).

## Setup

The install script does not install this sidecar. Cross-compile on a workstation with Go 1.24 or newer, copy the binary to the controller, then install the unit. Do not build on the Pi. Debian Bookworm's apt Go is 1.19, which is too old for this module.

```sh
# Workstation (Go >= 1.24):
make build-mqtt-arm       # 32-bit Pi OS -> dist/zanjerito-mqtt-linux-armv7
# make build-mqtt-arm64   # 64-bit Pi OS -> dist/zanjerito-mqtt-linux-arm64
# Copy that binary to the controller as /opt/zanjerito/zanjerito-mqtt (mode 0755).

# On the controller:
sudo install -o root -g root -m 0600 deploy/zanjerito-mqtt.env.example /opt/zanjerito/zanjerito-mqtt.env
# set MQTT_URL, MQTT_USER, MQTT_PASS, ZANJERITO_HA_URL

sudo cp deploy/zanjerito-mqtt.service /etc/systemd/system/zanjerito-mqtt.service
sudo systemctl daemon-reload
sudo systemctl enable --now zanjerito-mqtt
journalctl -u zanjerito-mqtt -f
```

`ZANJERITO_HA_URL` is required. It must be an `http` or `https` URL of the daemon `LISTEN` address, for example `http://<controller-host>:8080/api/ha`. Use a loopback URL only when the daemon binds loopback.

The unit sets `DynamicUser=yes`. systemd reads `EnvironmentFile` as root before it starts the process as the dynamic user, so the env file stays `root:root` mode `0600`. The dynamic user never opens that file. The unit does not start after, require, or bind to the irrigation service or the kiosk. If `/api/ha` is down, the sidecar publishes availability `offline` and watering is unchanged.

## Broker

Preferred: the Home Assistant Mosquitto add-on with a dedicated login for this sidecar.

Alternative: a LAN-only Mosquitto 2.x broker with password auth, anonymous disabled, not exposed to the internet. Do not run the broker on the irrigation Pi.

## Environment

Real file: `/opt/zanjerito/zanjerito-mqtt.env`, owner `root:root`, mode `0600`, never committed. See `deploy/zanjerito-mqtt.env.example`. `root:root` mode `0600` works with `DynamicUser=yes` because systemd reads `EnvironmentFile` as root before dropping to the dynamic user.

| Variable | Default | Meaning |
|---|---|---|
| `MQTT_URL` | (required) | Broker URL, for example `tcp://<broker-host>:1883` |
| `MQTT_USER` | empty | Broker username. If `MQTT_URL` itself has userinfo, that login is used and the startup log says `user=(from url)` |
| `MQTT_PASS` | empty | Broker password (never logged) |
| `MQTT_CLIENT_ID` | `zanjerito-mqtt` | MQTT client id |
| `ZANJERITO_HA_URL` | (required) | `http` or `https` URL of `GET /api/ha`; must match daemon `LISTEN` |
| `MQTT_POLL_INTERVAL` | `2s` | Poll period (Go duration). Rejected below `500ms` or above `10s` |
| `MQTT_REFRESH_INTERVAL` | `5m` | Re-publish state even if unchanged |

## Topics

| Purpose | Topic | Retained | QoS |
|---|---|---|---|
| Device discovery | `homeassistant/device/zanjerito/config` | yes | 1 |
| State | `zanjerito/state` | yes | 1 |
| Availability | `zanjerito/availability` | yes | 1 |
| HA birth/status (subscribe) | `homeassistant/status` | n/a | 1 |

Availability payloads are `online` and `offline` (Home Assistant defaults). The broker last-will is retained `offline` on `zanjerito/availability`.

## Entities

One Home Assistant device (`Irrigation`, manufacturer PixnBits, model Zanjerito). Component `uniq_id` is `zanjerito_mqtt_<key>`.

| key | platform | field | device class | unique_id |
|---|---|---|---|---|
| `phase` | sensor | `phase` | | `zanjerito_mqtt_phase` |
| `pause_source` | sensor | `pause_source` | | `zanjerito_mqtt_pause_source` |
| `paused_until` | sensor | `paused_until` | timestamp | `zanjerito_mqtt_paused_until` |
| `active_zone` | sensor | `active_zone` | | `zanjerito_mqtt_active_zone` |
| `zones_on` | sensor | `zones_on` | | `zanjerito_mqtt_zones_on` |
| `step_remaining` | sensor | `step_remaining_sec` | duration (s) | `zanjerito_mqtt_step_remaining` |
| `run_remaining` | sensor | `run_remaining_sec` | duration (s) | `zanjerito_mqtt_run_remaining` |
| `next_run` | sensor | `next_run_at` | timestamp | `zanjerito_mqtt_next_run` |
| `next_run_ends` | sensor | `next_run_ends_at` | timestamp | `zanjerito_mqtt_next_run_ends` |
| `next_run_length` | sensor | `next_run_total_min` | duration (min) | `zanjerito_mqtt_next_run_length` |
| `rain_last_update` | sensor | `rain_last_ok_at` | timestamp | `zanjerito_mqtt_rain_last_update` |
| `rain_24h` | sensor | `rain_24h_in` | precipitation (in) | `zanjerito_mqtt_rain_24h` |
| `rain_72h` | sensor | `rain_72h_in` | precipitation (in) | `zanjerito_mqtt_rain_72h` |
| `last_run_kind` | sensor | `last_run_kind` | | `zanjerito_mqtt_last_run_kind` |
| `last_run_outcome` | sensor | `last_run_outcome` | | `zanjerito_mqtt_last_run_outcome` |
| `last_run_started` | sensor | `last_run_started_at` | timestamp | `zanjerito_mqtt_last_run_started` |
| `last_run_ended` | sensor | `last_run_ended_at` | timestamp | `zanjerito_mqtt_last_run_ended` |
| `paused` | binary_sensor | `paused` | | `zanjerito_mqtt_paused` |
| `lockout` | binary_sensor | `lockout` | problem | `zanjerito_mqtt_lockout` |
| `error` | binary_sensor | `has_error` | problem | `zanjerito_mqtt_error` |
| `next_run_skipped_by_pause` | binary_sensor | `next_run_skipped_by_pause` | | `zanjerito_mqtt_next_run_skipped_by_pause` |
| `rain_feed_enabled` | binary_sensor | `rain_enabled` | | `zanjerito_mqtt_rain_feed_enabled` |
| `rain_unavailable` | binary_sensor | `rain_unavailable` | problem | `zanjerito_mqtt_rain_unavailable` |
| `zone_N` | binary_sensor | `zones[].on` | running | `zanjerito_mqtt_zone_N` |

Empty `pause_source` renders `none`. JSON `null` on text fields (`active_zone`, `last_run_kind`, `last_run_outcome`) renders `none`. JSON `null` on timestamps renders the literal `None` so Home Assistant treats the sensor as unknown.

If a REST `rest:` package already created the same names, remove it first. MQTT discovery would otherwise allocate `_2` entity ids.

## Behaviour

The sidecar polls `ZANJERITO_HA_URL` every `MQTT_POLL_INTERVAL` (first poll immediately). While the broker connection is open, it publishes discovery when the zone set changes (and tombstones removed `zone_N` components), publishes the snapshot body verbatim on `zanjerito/state` when the bytes change or `MQTT_REFRESH_INTERVAL` has elapsed, then publishes `online`.

Polls while the broker is disconnected do not publish and do not queue messages for later. They still remember the latest good snapshot and whether the latest poll was valid. A failed publish does not advance the last-published caches, so the next connected poll publishes again. Each publish waits at most a couple of seconds.

When Home Assistant comes back (`homeassistant/status` payload `online`), the sidecar waits a random 0–2s and republishes discovery, state, and availability.

On broker connect the sidecar clears last-published caches and publishes once, in order: discovery, the latest good snapshot, then availability (`online` if the latest poll succeeded, otherwise `offline`). If there is no good snapshot yet, connect publishes nothing unless a poll has already failed, in which case it publishes `offline`. A fresh start leaves availability to the retained last-will `offline`, so Home Assistant does not see an extra offline message before the first good poll.

## When the publisher stops

| Event | Availability |
|---|---|
| Clean stop (`systemctl stop`, SIGINT/SIGTERM) | retained `offline` when the broker connection is open; otherwise the session last-will |
| Crash / kill | broker LWT retained `offline` within about 1.5× keepalive (keepalive is 15s) |
| `/api/ha` unreachable or body not a snapshot object with `phase` | retained `offline` (once until the snapshot recovers) |

Home Assistant then shows every MQTT entity as unavailable. Valves and schedules are unaffected.

## Rollback

The REST integration is unaffected.

```sh
sudo systemctl disable --now zanjerito-mqtt
sudo rm -f /etc/systemd/system/zanjerito-mqtt.service
sudo systemctl daemon-reload
sudo rm -f /opt/zanjerito/zanjerito-mqtt /opt/zanjerito/zanjerito-mqtt.env
```

Remove the Home Assistant device and retained messages (against your broker):

```sh
mosquitto_pub -h <broker-host> -t homeassistant/device/zanjerito/config -r -n
mosquitto_pub -h <broker-host> -t zanjerito/state -r -n
mosquitto_pub -h <broker-host> -t zanjerito/availability -r -n
```

An empty retained payload on the discovery topic deletes the device.
