# TWC Controller

An [ESPHome](https://esphome.io) external component that turns a Tesla Wall Connector Gen 2 into a smart, network-controllable charger — **without Home Assistant, without Tesla's cloud, and without the Tesla vehicle API.**

The Gen 2 has no IP interface of its own: no WiFi, no Modbus, no local API. The only thing it speaks is Tesla's proprietary RS-485 load-sharing protocol, designed to let up to four TWCs share power between themselves. This project puts an ESP32 on that bus, has it impersonate a "primary" (master) TWC, and exposes everything it learns — and everything it can command — as plain ESPHome entities: readable and writable over REST, MQTT, or the Home Assistant API, whichever you already have.

That makes it usable from **any** home automation system that can make an HTTP request, including systems like [Loxone](https://www.loxone.com/) that have no Tesla or Home Assistant integration at all. That is the specific goal this fork is built and tested around: get vehicle-connected/charging status, live power draw, and control over the charge current and on/off state into Loxone with no intermediate service required — just this one ESP32.

This started as a standalone Arduino application and was rewritten as an ESPHome external component so that WiFi, OTA updates, and network transport are no longer something you have to build yourself — only the load-sharing protocol itself is project-specific code.

## How it works

In its factory configuration, a Gen 2 TWC's rotary switch is set to a numbered position (its own address) and it acts as its own master. Turning the switch to position **F** puts it into slave mode: it stops deciding its own charge current and instead waits for a master TWC to tell it, once a second, how many amps it's allowed to draw. This project's ESP32 *becomes* that master — a "fake master" in the same sense TWCManager uses the term — talking to the real TWC over two wires (D+/D−) on a shared RS-485 bus.

**This protocol is reverse-engineered, not documented by Tesla.** Every value and command byte in this project was worked out by the community (see [Credits](#credits--related-projects)) by observing real traffic, not from a spec. A malformed frame can, in the worst case, put the TWC into a state that isn't recoverable without a factory reset or a call to Tesla support. Firmware differences between hardware revisions and protocol versions are common — don't assume behavior confirmed on one TWC applies unchanged to another. Set `max_current` in your configuration to what your circuit and breaker can actually deliver, not just what the TWC reports — the wallbox no longer has its own upper limit in slave mode; whatever the master sends is the only ceiling left.

## Features

- **Set Current** — a controllable `number` entity for the current (in amps) offered to the connected vehicle. Reasserted in *every* heartbeat sent to the TWC (roughly once a second), not just when it changes, so the limit actually holds — see [Known limitations](#known-limitations--safety-notes) for why that matters. Set to `0` to offer no current at all.
- **Allow Charging** — an experimental `switch` entity that sends the protocol's dedicated start/stop commands rather than relying on a `0` current limit. See the caveat below before depending on it.
- **Vehicle Connected / Charging Active** — two `binary_sensor` entities derived from the TWC's own status reporting, so you don't have to interpret raw protocol state codes yourself. Each also has a plain numeric (`0`/`1`) `sensor` twin, since ESPHome's MQTT integration always publishes a `binary_sensor` as the text `"ON"`/`"OFF"` with no way to configure that.
- **Live measurements** — per-phase voltage and current, total actual current, instantaneous charging power (computed), lifetime energy delivered.
- **Identification** — TWC serial number, firmware version, and (where the firmware supports it) the VIN of the connected vehicle.
- **REST, MQTT, or Home Assistant API** — every entity above is a normal ESPHome entity. Expose it however your automation platform can consume it; this fork was built and tested against ESPHome's `web_server` REST interface specifically because it needs no broker and no HA install.

## Hardware

You need an ESP32 (any variant with a free UART) and an RS-485 transceiver wired to the TWC's D+/D− terminals, plus a GPIO controlling the transceiver's driver-enable (DE/RE) pin for half-duplex switching. A few things that matter in practice, learned the hard way:

- **Level shifting.** The common red "MAX485 TTL to RS485" breakout is a 5 V part. Its receive output (RO) will drive ~5 V into an ESP32 GPIO, which is not 5 V-tolerant. Use a 3.3 V-native transceiver (MAX3485 / SP3485 / SN65HVD75) instead, or a module built for 3.3 V.
- **Galvanic isolation.** The TWC sits next to mains/EV-charging voltages. Prefer an isolated ESP32 board (e.g. Olimex ESP32-POE-**ISO**) powered independently (PoE is ideal), or an isolated transceiver, so your control electronics don't share a ground reference with the wallbox.
- **Termination.** 120 Ω across A/B is usually unnecessary for a short, direct run to a single TWC, but add it if you see corrupted frames.
- **Bench-test first.** An M5 Atom Lite + RS-485 base (or any small dev board) is a fine way to validate wiring and see real frames in the logs before committing to a permanent, isolated install.

Boards known to work: Olimex ESP32-POE / ESP32-POE-ISO with a MOD-RS485, and M5Stack Atom Lite with the Atomic RS485 Base. Any ESP32 + 3.3 V RS-485 transceiver combination should work equally well.

## Installation

```yaml
esphome:
  name: twc
  friendly_name: Tesla Wall Connector

esp32:
  board: esp32dev # match your actual board - e.g. m5stack-atom for an
                   # M5Stack Atom Lite

# On a PoE board, prefer `ethernet:` over `wifi:` here — one less thing
# that can drop out on a device that's about to control your car charger.
wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

logger:
  level: DEBUG

api:
  reboot_timeout: 0s # required: without a Home Assistant client, ESPHome
                      # would otherwise reboot this device every 15 minutes

ota:
  - platform: esphome

# REST interface for Loxone (or anything else that can make an HTTP request).
# version: 1 is deliberately minimal - v2/v3 pull in a full web UI you don't need here.
web_server:
  port: 80
  version: 1

uart:
  id: twc_uart
  # GPIO19 (TX) / GPIO22 (RX) are confirmed working for an M5Stack Atom Lite
  # with the Atomic RS485 Base - M5's own pinout docs are ambiguous here, so
  # this was verified empirically (pulse-counting each candidate GPIO while
  # the TWC's linkready frames were on the bus). Different board, different
  # pins - check yours before assuming these apply.
  tx_pin: GPIO19
  rx_pin: GPIO22
  baud_rate: 9600
  data_bits: 8
  parity: NONE
  stop_bits: 1

external_components:
  - source: github://lucienkerl/esphome-twc-controller
    components: [twc-controller]
    refresh: 0s # avoid ESPHome's day-long cache while you're iterating;
                # widen this once your setup is stable

twc-controller:
  id: twc
  uart_id: twc_uart
  twc_id: 0xAB32          # this device's fake TWC ID - must not collide
                           # with a real TWC's ID on the bus
  flow_control_pin: GPIO33 # DE/RE pin of your RS-485 transceiver. A board
                            # whose transceiver switches direction itself
                            # (e.g. the Atomic RS485 Base) doesn't use this
                            # pin electrically, but the option is required -
                            # point it at any otherwise-unused GPIO. On a
                            # board like the Olimex MOD-RS485 this pin is
                            # real and must go to the transceiver's DE/RE.
  min_current: 0           # 0 is a valid "offer nothing" value; 1-5 A are not
                            # valid in the protocol and get clamped up to 6
  max_current: 16           # hard ceiling - set this to what your circuit
                             # can actually deliver, see the warning above
  set_current: 6             # initial value after boot, before anything
                              # else has written to the number entity

  current:
    name: "Charging Current"       # sum of actual current across all
                                    # connected TWCs (A)
  actual_current:
    name: "Actual Current"         # actual current of this connector (A)
  max_allowable_current:
    name: "Maximum Allowable Current" # the TWC's own reported hardware max
  state:
    name: "State"                  # raw protocol status code, see below
  total_kwh_delivered:
    name: "Total kWh Delivered"
  serial:
    name: "Serial"
  firmware_version:
    name: "Firmware Version"
  connected_vin:
    name: "Connected VIN"
  phase_1_voltage:
    name: "Phase 1 Voltage"
  phase_2_voltage:
    name: "Phase 2 Voltage"
  phase_3_voltage:
    name: "Phase 3 Voltage"
  phase_1_current:
    name: "Phase 1 Current"
  phase_2_current:
    name: "Phase 2 Current"
  phase_3_current:
    name: "Phase 3 Current"

  allow_charging:
    name: "Allow Charging"     # experimental, see below

  vehicle_connected:
    name: "Vehicle Connected"
  charging_active:
    name: "Charging Active"
  vehicle_connected_numeric:
    name: "Vehicle Connected Numeric"   # same value as vehicle_connected,
  charging_active_numeric:              # as a plain 0/1 sensor for MQTT
    name: "Charging Active Numeric"
```

### Computing charging power in kW

There's no `power` sensor built in, since it's just voltage × current per phase, computed the same way regardless of what this component exposes. Give the three voltage and three current sensors above an `id:` each, then add a `template` sensor:

```yaml
twc-controller:
  # ...
  phase_1_voltage:
    name: "Phase 1 Voltage"
    id: twc_v1
  phase_2_voltage:
    name: "Phase 2 Voltage"
    id: twc_v2
  phase_3_voltage:
    name: "Phase 3 Voltage"
    id: twc_v3
  phase_1_current:
    name: "Phase 1 Current"
    id: twc_i1
  phase_2_current:
    name: "Phase 2 Current"
    id: twc_i2
  phase_3_current:
    name: "Phase 3 Current"
    id: twc_i3

sensor:
  - platform: template
    name: "Charging Power"
    unit_of_measurement: "kW"
    device_class: power
    state_class: measurement
    accuracy_decimals: 2
    update_interval: 5s
    lambda: |-
      auto v = [](esphome::sensor::Sensor *s) -> float {
        return std::isnan(s->state) ? 0.0f : s->state;
      };
      float p = v(id(twc_v1)) * v(id(twc_i1))
              + v(id(twc_v2)) * v(id(twc_i2))
              + v(id(twc_v3)) * v(id(twc_i3));
      return p / 1000.0f;
```

The `std::isnan` guard matters: every sensor reads NAN until its first real value arrives after boot, and one NAN in the sum turns the whole result into NAN.

## Configuration reference

### `twc-controller:` options

| Option | Default | Description |
|---|---|---|
| `id` | — | Entity ID for this instance (also the `number` entity itself). |
| `uart_id` | — | The `uart:` bus wired to the RS-485 transceiver. |
| `flow_control_pin` | *required* | GPIO driving the transceiver's DE/RE (direction control) pin. |
| `twc_id` | `0xABCD` | The fake TWC ID this device presents. Must be unique on the bus. |
| `min_current` | `6` | Lower bound offered to the vehicle. `0` is valid (offers nothing); `1`-`5` are not valid in the protocol and are clamped up to `min_current`. |
| `max_current` | `32` | Upper bound offered to the vehicle. This is the *only* limit left once the TWC's rotary switch is in slave mode — set it to what your wiring can actually carry. |
| `set_current` | — | Value the `number` entity starts at before anything else writes to it. |
| `passive_mode` | `0` | If `1`, this device never announces itself as a master and never sends control commands — it only listens and reports what it observes. Useful for monitoring a TWC that already has a real master, without risking interference. |

### Entities

| Config key | Entity type | Notes |
|---|---|---|
| *(the `twc-controller:` block itself)* | `number` | The controllable current offered to the vehicle, in amps. |
| `allow_charging` | `switch` | Experimental — see [Known limitations](#known-limitations--safety-notes). |
| `current` | `sensor` | Sum of actual current across every connected TWC (A). |
| `actual_current` | `sensor` | Actual current of this connector specifically (A). Identical to `current` when only one TWC is linked. |
| `max_allowable_current` | `sensor` | The TWC's own reported hardware maximum (A), from its presence announcement — not your configured `max_current`. |
| `state` | `sensor` | Raw protocol status byte. See [Understanding `state`](#understanding-state) before using it directly. |
| `total_kwh_delivered` | `sensor` | Lifetime energy delivered by the TWC (kWh). |
| `phase_1/2/3_voltage` | `sensor` | Per-phase voltage (V). |
| `phase_1/2/3_current` | `sensor` | Per-phase current (A). |
| `serial` | `text_sensor` | TWC serial number. |
| `firmware_version` | `text_sensor` | TWC firmware version. |
| `connected_vin` | `text_sensor` | VIN of the connected vehicle, where the firmware reports it (Tesla only, firmware-dependent). |
| `vehicle_connected` | `binary_sensor` | Derived — see below. Published over MQTT as `"ON"`/`"OFF"`. |
| `charging_active` | `binary_sensor` | Derived — see below. Published over MQTT as `"ON"`/`"OFF"`. |
| `vehicle_connected_numeric` | `sensor` | Same value as `vehicle_connected`, as a plain `0`/`1` for MQTT consumers that want a number instead of `"ON"`/`"OFF"`. |
| `charging_active_numeric` | `sensor` | Same value as `charging_active`, as a plain `0`/`1`. |

## Controlling and reading over REST

With `web_server: version: 1` configured, every entity above is reachable at `http://<ip>/<domain>/<Entity Name>`, where `<Entity Name>` is exactly the `name:` you gave it in YAML, URL-encoded (spaces become `%20`). This is the interface Loxone's Virtual HTTP Input/Output blocks talk to directly — no MQTT broker, no Home Assistant, no extra service in between.

| Purpose | Request |
|---|---|
| Read current setpoint | `GET /number/Set%20Current` |
| Set current setpoint | `POST /number/Set%20Current/set?value=10` |
| Read actual current | `GET /sensor/Actual%20Current` |
| Read charging state | `GET /switch/Allow%20Charging` |
| Stop charging | `GET` or `POST /switch/Allow%20Charging/turn_off` |
| Resume charging | `GET` or `POST /switch/Allow%20Charging/turn_on` |
| Vehicle connected? | `GET /binary_sensor/Vehicle%20Connected` |
| Currently charging? | `GET /binary_sensor/Charging%20Active` |
| Vehicle connected? (as 0/1) | `GET /sensor/Vehicle%20Connected%20Numeric` |
| Currently charging? (as 0/1) | `GET /sensor/Charging%20Active%20Numeric` |

Responses are JSON, e.g. `{"id":"number-set_current","value":10.0,"state":"10.0 A"}` or `{"id":"switch-allow_charging","value":true,"state":"ON"}`. In Loxone, a Virtual HTTP Input polling one of the `GET` URLs with command recognition on `"value":` or `"state":"` gets you the number; a Virtual Output's *Command on ON* field takes the `set`/`turn_on`/`turn_off` URL directly.

One important difference between the two entity types: `POST` is **required** for `/number/.../set` because the value travels in the query string on a URL with an action segment. The `switch` actions (`turn_on`/`turn_off`/`toggle`) take no value and accept plain `GET`, so no extra configuration is needed on the Loxone side for those.

**Re-send your setpoint periodically.** Every heartbeat this device sends to the TWC already carries the current `number` value, roughly once a second — so once Loxone has set a value, it stays in effect without being re-sent. There is, however, no watchdog: if Loxone (or your automation logic) stops updating the setpoint entirely, the TWC keeps charging at whatever was last configured, indefinitely. If your control logic can fail silently, have it re-publish the setpoint periodically anyway, and treat the absence of updates as something to alarm on.

## Understanding `state`

The `state` sensor is the raw status byte the TWC reports in its heartbeat. Per the community protocol notes (TWCManager's `TWCSlave.py`):

| Code | Meaning |
|---|---|
| 0 | Ready. Vehicle may or may not be plugged in — this code alone doesn't tell you. |
| 1 | Plugged in, charging. |
| 2 | Error (e.g. heartbeat timeout from the master). |
| 3 | Plugged in, not charging. Doesn't reliably stay set — not safe to use alone as a "charge complete" signal. |
| 4 | Plugged in, ready to charge or charge scheduled. |
| 5 | Busy — typically appears for about a second and can interrupt any other state. |
| 8 | Starting to charge (ramping up). |
| 9 | **Not an independent status.** Protocol v2 echoes the command type the master just sent (this device sends command `0x09`, current limit, in every heartbeat) as an acknowledgement. Expect to see `9` almost continuously — it does not mean anything changed with the vehicle. |
| 0x0A | Amp adjustment period complete (follow-up to states 6/7). |

Because raw `state` alone can't reliably answer "is a car connected?" or "is it charging?" — code `0` is explicitly ambiguous, code `9` is now near-constant, and a fully charged car settles back into `0` while still plugged in — the `vehicle_connected` and `charging_active` binary sensors are computed instead of exposing this byte directly:

```
charging_active = state in {1, 8}  OR  actual_current > 0
```

For `vehicle_connected`, this component also queries the protocol's separate `GET_PLUG_STATE` command (not exposed as its own sensor, since it's specifically about disambiguating this one question) periodically in the background. Unlike the heartbeat's `state`, its answer is unambiguous — `0` genuinely means unplugged, not "unplugged or just idle". Once the first response has come back:

```
vehicle_connected = plug_state != 0
```

Before that first response (briefly, after boot or a fresh link), it falls back to:

```
vehicle_connected = charging_active  OR  state in {3, 4}
```

which is the same heuristic used before `GET_PLUG_STATE` was wired up, and is why the fallback can still misread a fully-charged-but-still-plugged-in car as disconnected if you catch it in that narrow window.

### Finding state changes in the logs

With `uart: debug:` enabled, every single heartbeat frame (roughly once a second per connected TWC) gets logged, which buries the state transitions you actually care about. Every real vehicle state change and every `GET_PLUG_STATE` change is also logged separately at `INFO` with a fixed, greppable prefix — `Vehicle state changed for ...` / `Plug state changed for ...` — regardless of the raw byte dump. To see only those and cut the per-heartbeat noise, drop the `uart_debug` tag down in your `logger:` config:

```yaml
logger:
  level: DEBUG
  logs:
    uart_debug: WARN   # suppresses the raw >>>/<<< byte dump
    twc.protocol: INFO # keeps the state-change lines
```

Turn `uart_debug` back up to its previous level only when you need to see the actual bytes on the wire, e.g. while debugging wiring or a new protocol command.

## Known limitations & safety notes

- **The protocol is reverse-engineered.** Treat every command as capable of putting the TWC into an unrecoverable state until proven otherwise on your specific hardware/firmware. Don't invent your own frames.
- **`min_current: 0` stops the *offer*, not necessarily the vehicle.** Setting the current to `0` tells the TWC no current is available; whether the connected car reliably stops drawing power (and, more importantly, reliably *resumes* afterward without being re-plugged) depends on your vehicle and its sleep behavior. Test the stop *and* the resume before relying on this for anything time-critical.
- **`Allow Charging` is experimental.** It uses the protocol's dedicated start/stop commands, which are less exercised in the community than the current-limit heartbeat. TWCManager — the most actively maintained fake-master implementation — has an open, years-old TODO for exactly this ("start and stop charging using protocol 2 commands... if I ever figure out how") and falls back to controlling the vehicle directly via Tesla's own API instead. Verify it does what you expect on your hardware before automating around it. Its last commanded state is restored and re-asserted on every boot (including after a crash or OTA update), not just within a running session.
- **No watchdog.** Unlike TWCManager's MQTT `chargeNow` command (which takes a duration and expires), this component holds the last value it was given indefinitely. Build any "fail safe" behavior you need into your automation logic.
- **1-5 A are not valid values.** The protocol only accepts `0` or `≥ 6` A. Values in between get clamped up to `min_current` — don't let a rounding error in your control logic land in that gap and assume it did what you asked.
- **`max_current` is your only ceiling.** In slave mode, the TWC's rotary switch no longer limits anything — whatever you configure here (and only here) is what protects your wiring.

## Credits & related projects

Credit to [WinterDragoness](https://teslamotorsclub.com/tmc/members/winterdragoness.40930/) and others on the [Tesla Motors Club forum thread](https://teslamotorsclub.com/tmc/threads/new-wall-connector-load-sharing-protocol.72830/) for reverse-engineering the protocol, and to [Craig Peacock](https://teslamotorsclub.com/tmc/members/craig-128.113283/) for the reference C implementation this project builds on. None of this would exist without that work.

- [craigpeacock/TWC](https://github.com/craigpeacock/TWC) — the C reference implementation
- [TWCManager](https://github.com/ngardiner/TWCManager) — the most actively maintained Python fake-master, MQTT/HTTP control, Tesla API integration
- [TWCManager (original)](https://github.com/dracoventions/TWCManager) — the original project, useful wiring/installation documentation
- [erwin314/twc_gen2](https://github.com/erwin314/twc_gen2) — an independent ESPHome implementation; the source for this fork's `Allow Charging` switch
- [yoziru/esphome-tesla-ble](https://github.com/yoziru/esphome-tesla-ble) — controls the vehicle directly over BLE instead of the wallbox; the more reliable option for start/stop if this component's RS-485 approach proves insufficient
