# LG Protocol Feature Coverage

This table compares [`protocol.md`](protocol.md) with the current ESPHome implementation. "RX" means the controller decodes a valid frame without using that field as a control. Every field with a documented encoding has an optional control. The controls follow the reverse-engineered tables and examples in `protocol.md`. None of the controls for the extended fields has been validated on a physical LG unit, and a unit that does not support a function may ignore it.

Controls are optional YAML entries ending in `_switch`, `_select`, `_number`, or `_button`. Installer settings are hidden on slave controllers, like the existing installer entities. The capabilities the unit reports are listed in the log at boot and are available through the optional `capabilities` text sensor. Changing a setting the unit does not advertise logs a warning, but the setting is still sent.

| Protocol area | Coverage | Current behavior |
| --- | --- | --- |
| Framing and bus scheduling | Implemented | A rolling header/checksum parser discards bad candidates one byte at a time and times out partial frames. UART input is drained continuously. A non-blocking scheduler waits for 500 ms of bus idleness, verifies echoed sends, and retries the same transaction kind after a missing echo. |
| Type 0 status (`0xA8/0xC8/0x28`) | Control + RX | Power, cool/heat/dry/fan/auto, swing, purifier, target/room temperature, Fahrenheit mapping, thermistor selection including 2TH (`thermistor_2th_switch`), and exact-count zone bitfields. Slow to high fan speeds are standard fan modes; low-medium, medium-high, and power are custom fan modes when the unit advertises them. Optional controls: `humidifier_switch`, `auxiliary_heater_switch`, `swirl_switch`, `ventilation_switch`, `fan_auto_switch`, `energy_saving_switch`, `elevation_grill_select`, `ceiling_height_select`. |
| Type 0 one-shot requests | Control | `filter_reset_button`, `test_run_button`, and `release_delay_button` set their bit in one status frame and clear it in the next. |
| Type 0 reservations | Control + RX | Sleep timer as before. `on_timer_number` and `off_timer_number` send a turn-on or turn-off reservation once, and the controller switches the unit itself when the time is up, which is how LG controllers handle them. Reservations and simple timers set by another controller are shown but not acted on. `clear_reservations_button` sends reservation type 4. |
| Type 0 telemetry | Optional RX | Filter warning, energy-saving, external ventilation, humidifier, auxiliary heater, swirl, Fan Auto, robot-clean, cooling-active, heating-active, `zone_type_new`, and `auto_addressing`. Defrost, preheat, outdoor-unit activity, auto-dry activity, and error code remain standard entities. Unit-only telemetry is not cleared by peer controller frames. |
| Type 1 capabilities (`0xC9`) | RX/internal | Capability masks shape operation, fan, custom fan, swing, minimum-temperature, and temperature-step traits, and are persisted. The documented single-bit flags are named in the boot log. Zone capability bits are treated as a maximum only; they do not guess the installed zone count. |
| Type 2 settings (`0xAA/0xCA/0x2A`) | Control + RX | Fan curves, up to four vane positions, auto-dry, and unit-reported min/max target limits. Optional controls: `unit_address_number`, `auto_change_temperature_number`, `auxiliary_heater_installed_switch`, `min_setpoint_number`, `max_setpoint_number`, `dry_contact_auto_switch`, `zone_state_fixed_switch`, `robot_clean_auto_switch`. A Type 2 frame is sent only for an explicit local change and only as a modified copy of the unit's own frame. |
| Type 3 settings (`0xAB/0xCB/0x2B`) | Control + RX | Overheating and unit-only pipe-temperature telemetry; DRED, oil warning, and model/capacity codes are RX only. Optional controls: `overcooling_select`, `half_degree_control_switch`, `emergency_heater_switch`, `emergency_heater_fan_switch`, `low_ambient_heating_number`, `group_control_switch`, `wifi_ap_switch`, `refrigerant_leak_detector_switch`, `thermal_off_fan_select`, `static_pressure_number`, `installed_zones_number`. Byte 10 supplies the exact zone count when `zone_count: 0`; a configured YAML count remains authoritative. |
| Type 3 polling | Implemented, bounded | The normal startup Type 0 request is tried first. Only a master can issue fallback/periodic `0xAB` queries. Startup retries are bounded by `initial_type_b_retries`; periodic polling is controlled by `type_b_refresh_interval` and can be disabled with `0`. Slave controllers remain passive. |
| Type 4 / Type C status (`0xAC/0xCC/0x2C`) | Control + RX + master reply | Optional RX sensors expose filter hours, occupancy, dual-setpoint state, total energy, high/low setpoints, Type 4 room temperature, deadband, Himalaya cooling, mosquito-away, and comfort-cooling states. Optional controls: `dual_setpoint_switch`, `dual_setpoint_high_number`, `dual_setpoint_low_number`, `deadband_number`, `himalaya_cooling_switch`, `mosquito_away_switch`, `comfort_cooling_switch`. A change is sent as the unit's last `0xCC` with the changed field and the settings-changed flag. With `respond_to_type4: true`, a master still mirrors each unit `0xCC` as `0xAC`. The dual setpoints are separate numbers; the climate entity keeps its single target temperature. |
| Type 5 (`0xAD/0xCD/0x2D`) | Control + RX for the documented bits | `auto_start_switch`, `ventilation_fan_interlock_switch`, `auxiliary_heater_duct_type_switch`, `primary_heater_control_switch`, `simple_dry_contact_number`. Sent only as a modified copy of a received Type 5 frame. Bits without an example in `protocol.md` are preserved and not exposed. |
| Type 6 / Type E (`0xAE/0xCE/0x2E`) | Selected control + RX | Unit subtype `0x80` exposes humidity, fan runtime, indoor-unit runtime, and 0.1-degree room temperature. Subtype `0x10` installer settings: `silent_mode_number`, `defrost_mode_number`, `fan_continuous_switch`, `temperature_fan_auto_switch`, `cn_ext_number`, `outdoor_unit_function_switch`, sent only as a modified copy of a received subtype `0x10` frame. |
| Type 7 / Type F (`0xAF/0xCF/0x2F`) | Optional RX | `power` publishes bytes 2-4 in kW using the digit layout the PREMTB100 displays; all three documented examples match. `type7_power_raw` remains. |
| Zones | Selected control + RX | `zone1`-`zone8` are optional schema entries. `zone_count: 1..8` sets an exact authoritative count; `0` waits for a valid Type 3 count bounded by known capability maximum. Type 0 zone bits are decoded only after an exact count is known, avoiding their auto-dry/robot-clean aliases on other units. |

## Not implemented

These have no usable encoding in `protocol.md`:

| Field | Reason |
| --- | --- |
| Operation mode AI | The capability bit is documented but the mode value is not. |
| Central control and lock bits (Type 0 byte 4) | Meaning unknown. |
| Type 3 byte 2 "zone" setting (1-3) | Purpose unknown. |
| DRED mode | Reported by the unit as telemetry; it is not a controller setting. |
| Type 5 bits without an example | Only five settings have examples. |
| Type 6 settings 56, 57, and 60 (outdoor unit cycle priority, heating stage temperature) | The examples do not define a complete encoding. |
| Type 6 subtypes `0x00` and `0x11` (extended capabilities, air quality) | Layout unknown. These frames appear in `last_unsupported_message`. |
| Type 7 fields other than power | Layout unknown. |
| Ventilation and heat-exchanger products (`0xB0`/`0xD0` families) | Out of scope for this controller. |
