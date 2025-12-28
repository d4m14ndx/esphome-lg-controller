# LG Controller component overview

This document explains how the custom `lg_controller` component bridges ESPHome’s climate API to an LG indoor unit over UART. Read this alongside ESPHome’s climate docs (<https://esphome.io/components/climate/>) for the Home Assistant-facing behaviour and the LG protocol reference in `protocol.md`.

## High-level structure

### Python glue (`climate.py`)
* Defines the YAML schema for the component, mirroring `climate` plus dozens of auxiliary entities (vanes, installer fan speeds, sleep timer, zone switches).
* Instantiates those entities and passes pointers into the native C++ class via `new_Pvariable`.
* Registers the component as both a `climate` device and a `uart` device so ESPHome generates the bindings and scheduler hooks.

### Native implementation (`lg-controller.h`)
* Implements `LgController`, which inherits `climate::Climate`, `uart::UARTDevice`, and `Component`.
* Provides minimal wrapper classes (`LgSwitch`, `LgSelect`, `LgNumber`) so HA/ESPHome state changes feed into the controller without extra transport logic.
* Encapsulates the LG protocol framing (13-byte messages) and maps ESPHome climate traits to the LG-specific fields.

## Key concepts in `LgController`

### Capability detection
* The unit sends a `0xC9` “capabilities” message; we persist it in NVS so the controller can configure climate traits and enable/disable entities at boot without waiting for a new capabilities frame.
* `configure_capabilities()` reads those flags to:
  * Shape the supported modes, fan speeds, and swing options exposed to HA.
  * Hide entities (vanes, installer fan-speed overrides, overheating select, purifier, auto dry) if unsupported or if the controller is configured as a slave.
  * Infer zone count (4 or 8) when available and mark unused zone switches as `internal`.

### Temperature handling
* LG uses a non-linear Fahrenheit → Celsius mapping that differs from the standard conversion. `TempConversion` contains the lookup tables so HA setpoints and reported values align with what the indoor unit displays.
* Room temperature uses either an external ESPHome sensor or the unit’s thermistor. External sensors are clamped to 11–35°C and rounded to 0.5°C before transmission.

### State inputs and callbacks
* Each select/number/switch entity is given an `add_on_state_callback` that updates internal state (`vane_position_`, `fan_speed_`, `overheating_`, zone flags, etc.) and sets “pending” flags so changes are reflected in the next outbound frame.
* `restore_and_set_mode` on `LgSwitch` mimics ESPHome’s restore flow so switches rehydrate correctly on boot.

### Message types (all 13 bytes)
* **Status** (`0xA8` from master, `0x28` from slave): carries mode, fan, swing, purifier, target temp, room temp, zones, and sleep timer bits. Built from current ESPHome climate state plus the last received status to preserve unknown bits.
* **Type A settings** (`0xAA`/`0x2A`): installer fan curves, vane positions, auto-dry flag. We copy the last received `0xCA/0xAA` frame and patch in local overrides.
* **Type B settings** (`0xAB`/`0x2B`): overheating installer setting; optionally requests a fresh `0xCB` to read pipe temperatures.
* **Capabilities** (`0xC9`): stored to NVS and used to configure traits; may trigger a reboot on first boot to ensure HA sees the correct trait set.

### Receive path
* `update()` drains UART bytes into a fixed-size buffer; when 13 bytes arrive, `process_message()` validates checksum, discerns sender (unit/master/slave), and dispatches by message type.
* Status frames update HA-visible climate state (mode, fan, swing, target temp, zones, purifier) unless a local send is pending to avoid overwriting unsent changes.
* Capabilities frames update the stored capability bits and immediately re-run `configure_capabilities()` so entities appear/disappear without reboot.
* Type A/B frames refresh installer settings, vane positions, overheating select, and pipe temperatures; first-time reception queues a matching outbound frame so the controller pushes its defaults back.

### Send path and scheduling
* Pending flags (`pending_status_change_`, `pending_type_a_settings_change_`, `pending_type_b_settings_change_`) decide which message to send next.
* The update loop:
  * Retries unsatisfied sends (when no echoed frame was seen).
  * Waits for 500 ms of bus idleness (using direct GPIO reads) to reduce collisions on the shared UART line.
  * Prioritizes installer setting frames, then status changes, then periodic maintenance:
    * Type B is sent every 10 minutes to refresh pipe temperatures.
    * Status is sent every 20 seconds (master only) even without changes.
* Sleep timer logic mirrors HA: minutes are encoded into the status frame; when the timer elapses, the controller turns the unit off and clears the timer entity while suppressing callback recursion.

### Zones
* Supports up to 8 zones; zone switches are marked `internal` when unavailable.
* Incoming status frames can infer zone count when capabilities are missing, and keep HA switches in sync without triggering outbound changes (`from_status` guard).
* Outbound status frames re-encode zone bitfields so the indoor unit tracks the desired zoning state.

## Typical data flow
1. YAML is parsed by ESPHome; `climate.py` builds entities and allocates `LgController`.
2. On boot, NVS capabilities are loaded; climate traits and entity visibility are set accordingly.
3. Home Assistant issues a climate change (mode/temp/fan/swing) ➜ callbacks mark `pending_status_change_`.
4. The next `update()` finds the bus idle and sends a status frame; if swing changed, a Type A frame is queued to restore vane positions.
5. The indoor unit responds with `0xC8`/`0xCA`/`0xCB` frames, which refresh local mirrors and complete the pending send.

## Maintenance tips
* When adding new settings that travel over Type A/B frames, patch the corresponding `send_type_*` helpers and ensure callbacks set the right pending flag.
* If you tweak the capability mapping, bump `NVS_STORAGE_VERSION` so stored bits refresh cleanly on next boot.
* For debugging, enable verbose logging; all inbound/outbound frames are logged via `format_hex_pretty`.
