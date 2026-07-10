# LG Controller component overview

The `lg_controller` component bridges ESPHome's climate API to the reverse-engineered 13-byte LG wired-controller protocol. Read this with [`protocol.md`](../../../protocol.md). Extended fields vary by model, so code coverage does not imply that every field has been validated on every indoor unit.

## Structure

### `climate.py`

The Python layer defines the YAML schema, creates the climate and auxiliary entities, and registers the native controller as an ESPHome component and UART device.

The core climate entities remain compatible with existing configurations. Additional protocol telemetry is optional. Zone switches `zone1` through `zone8` are also optional, as is the Type 2 `fan_speed_power` curve. `zone_count`, `type_b_refresh_interval`, `initial_type_b_retries`, and `respond_to_type4` configure protocol behavior.

### `lg-protocol.h`

This header contains transport-independent helpers for:

* Frame header/source recognition and checksum calculation.
* Incremental rolling parsing of the byte stream.
* Packed-BCD Type 4 energy encoding/decoding.
* Exact-count zone bitfield encoding/decoding.
* Type 3 zone-count encoding/decoding.

Keeping these helpers independent of ESPHome makes the protocol behavior suitable for host-side tests.

### `lg-controller.h`

`LgController` inherits `climate::Climate`, `uart::UARTDevice`, and `Component`. It owns the live climate state, received shadow frames, optional telemetry pointers, pending transactions, and the non-blocking transmit scheduler.

`LgSwitch` and `LgNumber` distinguish an explicit Home Assistant control call from a state published while decoding the bus. Consequently, a received state update does not queue an unintended write back to the indoor unit.

## Receive path and recovery

`loop()` drains UART input continuously. `protocol::FrameParser` first seeks a valid master, slave, or unit header, then checks the checksum after 13 bytes. If a candidate is invalid, it discards one byte and tries again; a dropped/inserted byte or collision therefore does not leave all later frames permanently misaligned. Partial frames are reset after 2.5 seconds of inactivity. Optional `invalid_frames` and `last_unsupported_message` entities aid diagnosis.

After validation, frames are dispatched by their low three type bits:

* **Type 0 status** (`0xA8/0xC8/0x28`) updates climate state and optional filter, energy-saving, ventilation, humidifier, heater, swirl, Fan Auto, robot-clean, cooling, and heating telemetry. Defrost, preheat, outdoor activity, auto-dry activity, and error are accepted from the unit only. Protocol fan codes 5-7 are reported through `fan_mode_code` and preserved until Home Assistant explicitly selects a standard fan mode.
* **Type 1 capabilities** (`0xC9`) configures supported climate modes, fan modes, swing modes, target minimum, and target step. Changed capabilities are persisted.
* **Type 2 / Type A settings** (`0xCA/0xAA/0x2A`) updates vane positions, auto-dry, five fan curves when configured, and reported min/max setpoints.
* **Type 3 / Type B settings** (`0xCB/0xAB/0x2B`) updates overheating, the exact configured zone count, unit-only pipe temperatures, and optional DRED/overcooling/oil/half-degree/heater/group/AP/model/capacity/leak/static-pressure/fan diagnostics. Peer request frames are never used as pipe telemetry.
* **Type 4 / Type C status** (`0xCC/0xAC/0x2C`) exposes optional filter hours, occupancy, dual-setpoint data, total energy, room temperature, deadband, Himalaya cooling, mosquito-away, and comfort-cooling state.
* **Type 6 / Type E status** (`0xCE` subtype `0x80`) exposes optional humidity, fan runtime, unit runtime, and 0.1-degree room temperature.
* **Type 7 / Type F status** (`0xCF`) exposes bytes 2-4 as a raw diagnostic value. The engineering conversion remains intentionally unclaimed.

Type 5, other Type 6 subtypes, and unknown Type 7 fields are reported only through the optional unsupported-frame diagnostic. They are not decoded into controls.

## Send path

The scheduler runs every 250 ms and never waits in a busy loop. Transmission is disabled for the first ten seconds after setup. Thereafter, a frame can be sent only when UART has no pending data, the parser has no partial frame, the RX line is idle, and no bus activity has been seen for at least 500 ms.

After a send, the controller waits up to three seconds for its exact bus echo. A missing/corrupt echo requeues the same transaction kind. Explicit user changes have priority over replies and telemetry refreshes.

The controller sends only these documented subsets:

* **Type 0 status:** climate mode, standard fan mode, swing, purifier, target and room temperatures, thermistor selection, sleep timer, and exact-count zones. Ceiling-height bits and unrelated reservations from the shadow frame are preserved.
* **Type 2 / Type A:** slow, low, medium, high, and optional power fan curves; vane positions; and auto-dry. A valid received Type 2 shadow is required, and receiving that shadow alone does not queue a write.
* **Type 3 / Type B:** overheating writes after an explicit local change, or a query with the documented request bit. Pipe-temperature bytes are cleared on transmit.
* **Type 4 / Type C reply:** when a unit sends `0xCC`, a master with `respond_to_type4: true` mirrors it as `0xAC`. If a valid configured room sensor is available, byte 9 is refreshed first.

Type 5 and incomplete Type 6/7 installer settings are never synthesized.

## Type B polling

Startup first relies on the settings-batch request in the normal Type 0 status frame. If no `0xCB` arrives, a master may send a bounded number of minimal `0xAB` fallback queries. `initial_type_b_retries` controls that limit (`0` disables fallback queries). Installer changes made before the first `0xCB` share this attempt budget.

After initialization, only a master may poll periodically. `type_b_refresh_interval` is in seconds and `0` disables periodic polling. Slave controllers remain passive and never start a Type B query.

## Zones

Zone bits overlap auto-dry/robot-clean fields on some non-zoned or four-zone units. For that reason the component does not infer an installed count from Type 0 activity or from capability flags.

* `zone_count: 1..8` is authoritative.
* `zone_count: 0` waits for an exact nonzero count in Type 3 byte 10, bounded by the Type 1 capability maximum when known.
* Type 0 zone state is decoded and encoded only after an exact count is available.
* YAML only needs to define the optional zone switches that should be exposed.
* Writes to an unknown or out-of-range zone are rejected and the switch state is restored instead of reporting false success.

## Maintenance and verification

Protocol changes should add or update host-side golden-frame tests before adding a control. In particular, test checksum/resynchronization, source gating, byte-alias behavior, Type B retry bounds, zone counts 1-8, and preservation of unknown/reservation fields.

Receive-only support is preferable for a documented but not hardware-verified field. Promote a field to a control only after its direction, capability gate, valid range, and behavior have been confirmed with real bus captures.
