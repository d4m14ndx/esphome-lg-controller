import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import (
    binary_sensor,
    button,
    climate,
    number,
    select,
    sensor,
    switch,
    text_sensor,
    uart,
)
from esphome.const import CONF_ID, CONF_RX_PIN, CONF_INTERNAL

CODEOWNERS = ["JanM321"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "button", "number", "switch", "sensor", "select", "text_sensor"]

lg_controller_ns = cg.esphome_ns.namespace("lg_controller")
LgController = lg_controller_ns.class_(
    "LgController", cg.Component, uart.UARTDevice, climate.Climate
)
LgNumber = lg_controller_ns.class_("LgNumber", number.Number, cg.Component)
LgSelect = lg_controller_ns.class_("LgSelect", select.Select, cg.Component)
LgSwitch = lg_controller_ns.class_("LgSwitch", switch.Switch, cg.Component)
LgButton = lg_controller_ns.class_("LgButton", button.Button)
Field = lg_controller_ns.namespace("protocol").enum("Field", is_class=True)

CONF_FAHRENHEIT = "fahrenheit"
CONF_IS_SLAVE_CONTROLLER = "is_slave_controller"

CONF_TEMPERATURE_SENSOR = "temperature_sensor"

CONF_VANE1 = "vane1"
CONF_VANE2 = "vane2"
CONF_VANE3 = "vane3"
CONF_VANE4 = "vane4"
CONF_OVERHEATING = "overheating"

CONF_FAN_SPEED_SLOW = "fan_speed_slow"
CONF_FAN_SPEED_LOW = "fan_speed_low"
CONF_FAN_SPEED_MEDIUM = "fan_speed_medium"
CONF_FAN_SPEED_HIGH = "fan_speed_high"
CONF_FAN_SPEED_POWER = "fan_speed_power"
CONF_SLEEP_TIMER = "sleep_timer"

CONF_ERROR_CODE = "error_code"
CONF_PIPE_TEMP_IN = "pipe_temp_in"
CONF_PIPE_TEMP_MID = "pipe_temp_mid"
CONF_PIPE_TEMP_OUT = "pipe_temp_out"

CONF_DEFROST = "defrost"
CONF_PREHEAT = "preheat"
CONF_OUTDOOR = "outdoor"
CONF_AUTO_DRY_ACTIVE = "auto_dry_active"

CONF_PURIFIER = "purifier"
CONF_INTERNAL_THERMISTOR = "internal_thermistor"
CONF_AUTO_DRY = "auto_dry"
CONF_TYPE_B_REFRESH_INTERVAL = "type_b_refresh_interval"
CONF_INITIAL_TYPE_B_RETRIES = "initial_type_b_retries"
CONF_RESPOND_TO_TYPE4 = "respond_to_type4"
CONF_ZONE_COUNT = "zone_count"
CONF_ZONE1 = "zone1"
CONF_ZONE2 = "zone2"
CONF_ZONE3 = "zone3"
CONF_ZONE4 = "zone4"
CONF_ZONE5 = "zone5"
CONF_ZONE6 = "zone6"
CONF_ZONE7 = "zone7"
CONF_ZONE8 = "zone8"

OPTIONAL_BINARY_SENSORS = {
    "filter_warning": "set_filter_warning_binary_sensor",
    "energy_saving": "set_energy_saving_binary_sensor",
    "external_ventilation": "set_external_ventilation_binary_sensor",
    "humidifier_active": "set_humidifier_active_binary_sensor",
    "auxiliary_heater_active": "set_auxiliary_heater_active_binary_sensor",
    "swirl_active": "set_swirl_active_binary_sensor",
    "fan_auto_active": "set_fan_auto_active_binary_sensor",
    "robot_clean_active": "set_robot_clean_active_binary_sensor",
    "cooling_active": "set_cooling_active_binary_sensor",
    "heating_active": "set_heating_active_binary_sensor",
    "oil_warning": "set_oil_warning_binary_sensor",
    "half_degree_control": "set_half_degree_control_binary_sensor",
    "emergency_heater": "set_emergency_heater_binary_sensor",
    "emergency_heater_fan": "set_emergency_heater_fan_binary_sensor",
    "group_control": "set_group_control_binary_sensor",
    "wifi_ap": "set_wifi_ap_binary_sensor",
    "refrigerant_leak_detector": "set_refrigerant_leak_detector_binary_sensor",
    "occupancy": "set_occupancy_binary_sensor",
    "dual_setpoint": "set_dual_setpoint_binary_sensor",
    "himalaya_cooling": "set_himalaya_cooling_binary_sensor",
    "mosquito_away": "set_mosquito_away_binary_sensor",
    "comfort_cooling": "set_comfort_cooling_binary_sensor",
    "zone_type_new": "set_zone_type_new_binary_sensor",
    "auto_addressing": "set_auto_addressing_binary_sensor",
}

OPTIONAL_SENSORS = {
    "fan_mode_code": "set_fan_mode_code_sensor",
    "dred_mode": "set_dred_mode_sensor",
    "overcooling_mode": "set_overcooling_mode_sensor",
    "outdoor_model": "set_outdoor_model_sensor",
    "indoor_model": "set_indoor_model_sensor",
    "model_capacity": "set_model_capacity_sensor",
    "static_pressure": "set_static_pressure_sensor",
    "thermal_off_fan_mode": "set_thermal_off_fan_mode_sensor",
    "low_ambient_heating": "set_low_ambient_heating_sensor",
    "configured_zone_count": "set_configured_zone_count_sensor",
    "filter_time_remaining": "set_filter_time_remaining_sensor",
    "energy_total": "set_energy_total_sensor",
    "high_setpoint": "set_high_setpoint_sensor",
    "low_setpoint": "set_low_setpoint_sensor",
    "type4_room_temperature": "set_type4_room_temperature_sensor",
    "deadband": "set_deadband_sensor",
    "humidity": "set_humidity_sensor",
    "fan_runtime": "set_fan_runtime_sensor",
    "unit_runtime": "set_unit_runtime_sensor",
    "precise_room_temperature": "set_precise_room_temperature_sensor",
    "type7_power_raw": "set_type7_power_raw_sensor",
    "power": "set_power_sensor",
    "invalid_frames": "set_invalid_frames_sensor",
}

CONF_LAST_UNSUPPORTED_MESSAGE = "last_unsupported_message"
CONF_CAPABILITIES = "capabilities"

# Optional controls for the bit fields in lg-protocol.h (protocol::Field). The second element
# marks installer settings, which are hidden on slave controllers like the existing ones.
FIELD_SWITCHES = {
    "humidifier_switch": ("Humidifier", False),
    "auxiliary_heater_switch": ("AuxiliaryHeater", False),
    "swirl_switch": ("Swirl", False),
    "ventilation_switch": ("ExternalVentilation", False),
    "fan_auto_switch": ("FanAuto", False),
    "energy_saving_switch": ("EnergySaving", False),
    "robot_clean_auto_switch": ("RobotCleanAuto", False),
    "wifi_ap_switch": ("WifiAp", False),
    "dual_setpoint_switch": ("DualSetpoint", False),
    "himalaya_cooling_switch": ("HimalayaCooling", False),
    "mosquito_away_switch": ("MosquitoAway", False),
    "comfort_cooling_switch": ("ComfortCooling", False),
    "auxiliary_heater_installed_switch": ("AuxiliaryHeaterInstalled", True),
    "dry_contact_auto_switch": ("DryContactAuto", True),
    "zone_state_fixed_switch": ("ZoneStateFixed", True),
    "half_degree_control_switch": ("HalfDegreeControl", True),
    "emergency_heater_switch": ("EmergencyHeater", True),
    "emergency_heater_fan_switch": ("EmergencyHeaterFan", True),
    "group_control_switch": ("GroupControl", True),
    "refrigerant_leak_detector_switch": ("RefrigerantLeakDetector", True),
    "auto_start_switch": ("AutoStart", True),
    "ventilation_fan_interlock_switch": ("VentilationFanInterlock", True),
    "auxiliary_heater_duct_type_switch": ("AuxiliaryHeaterDuctType", True),
    "primary_heater_control_switch": ("PrimaryHeaterControl", True),
    "fan_continuous_switch": ("FanContinuous", True),
    "temperature_fan_auto_switch": ("TemperatureFanAuto", True),
    "outdoor_unit_function_switch": ("OutdoorUnitFunction", True),
}

# key: (field, installer setting, options in protocol value order)
FIELD_SELECTS = {
    "elevation_grill_select": ("ElevationGrill", False, ["Default", "Stop", "Up", "Down"]),
    "ceiling_height_select": ("CeilingHeight", True, ["Medium", "Low", "High", "Very high"]),
    "overcooling_select": (
        "Overcooling",
        True,
        ["0 (+0.5C/-0.5C)", "1 (+6C/+4C)", "2 (+4C/+2C)", "3 (+1C/-1C)"],
    ),
    "thermal_off_fan_select": ("ThermalOffFan", True, ["Fan low", "Fan off", "Fan setting"]),
}

# key: (field, installer setting, minimum, maximum)
FIELD_NUMBERS = {
    "auto_change_temperature_number": ("AutoChangeTemperature", False, 1, 7),
    "unit_address_number": ("UnitAddress", True, 0, 255),
    "min_setpoint_number": ("MinSetpoint", True, 16, 30),
    "max_setpoint_number": ("MaxSetpoint", True, 16, 30),
    "static_pressure_number": ("StaticPressure", True, 0, 11),
    "installed_zones_number": ("ZoneNumber", True, 0, 8),
    "low_ambient_heating_number": ("LowAmbientHeating", True, 0, 15),
    "simple_dry_contact_number": ("SimpleDryContact", True, 0, 3),
    "silent_mode_number": ("SilentMode", True, 0, 3),
    "defrost_mode_number": ("DefrostMode", True, 0, 3),
    "cn_ext_number": ("CnExt", True, 0, 7),
}

# key: (setter, installer setting, minimum, maximum, step)
OTHER_NUMBERS = {
    "on_timer_number": ("set_on_timer_number", False, 0, 1440, 1),
    "off_timer_number": ("set_off_timer_number", False, 0, 1440, 1),
    "dual_setpoint_high_number": ("set_dual_setpoint_high_number", False, 16, 30, 0.5),
    "dual_setpoint_low_number": ("set_dual_setpoint_low_number", False, 16, 30, 0.5),
    "deadband_number": ("set_deadband_number", False, 0, 9.9, 0.1),
}

# key: (setter, installer setting)
BUTTONS = {
    "filter_reset_button": ("set_filter_reset_button", False),
    "clear_reservations_button": ("set_clear_reservations_button", False),
    "test_run_button": ("set_test_run_button", True),
    "release_delay_button": ("set_release_delay_button", True),
}

CONF_THERMISTOR_2TH_SWITCH = "thermistor_2th_switch"

VANE_OPTIONS = ["0 (Default)", "1 (Up)", "2", "3", "4", "5", "6 (Down)"]
OVERHEATING_OPTIONS = ["0 (Default)", "1 (+4C/+6C)", "2 (+2C/+4C)", "3 (-1C/+1C)", "4 (-0.5C/+0.5C)"]

# This schema mirrors ESPHome's climate component (https://esphome.io/components/climate/)
# and wires up the custom LG controller entities. Each required entry creates an entity that
# ultimately feeds into the UART messages in lg-controller.h.
CONFIG_SCHEMA = climate.climate_schema(LgController).extend(
    {
        cv.Required(CONF_RX_PIN): pins.gpio_input_pin_schema,

        cv.Required(CONF_FAHRENHEIT): cv.boolean,
        cv.Required(CONF_IS_SLAVE_CONTROLLER): cv.boolean,

        cv.Optional(CONF_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),

        cv.Required(CONF_VANE1): select.select_schema(LgSelect),
        cv.Required(CONF_VANE2): select.select_schema(LgSelect),
        cv.Required(CONF_VANE3): select.select_schema(LgSelect),
        cv.Required(CONF_VANE4): select.select_schema(LgSelect),
        cv.Required(CONF_OVERHEATING): select.select_schema(LgSelect),

        cv.Required(CONF_FAN_SPEED_SLOW): number.number_schema(LgNumber),
        cv.Required(CONF_FAN_SPEED_LOW): number.number_schema(LgNumber),
        cv.Required(CONF_FAN_SPEED_MEDIUM): number.number_schema(LgNumber),
        cv.Required(CONF_FAN_SPEED_HIGH): number.number_schema(LgNumber),
        cv.Optional(CONF_FAN_SPEED_POWER): number.number_schema(LgNumber),
        cv.Required(CONF_SLEEP_TIMER): number.number_schema(LgNumber),

        cv.Required(CONF_ERROR_CODE): sensor.sensor_schema(),
        cv.Required(CONF_PIPE_TEMP_IN): sensor.sensor_schema(),
        cv.Required(CONF_PIPE_TEMP_MID): sensor.sensor_schema(),
        cv.Required(CONF_PIPE_TEMP_OUT): sensor.sensor_schema(),

        cv.Required(CONF_DEFROST): binary_sensor.binary_sensor_schema(),
        cv.Required(CONF_PREHEAT): binary_sensor.binary_sensor_schema(),
        cv.Required(CONF_OUTDOOR): binary_sensor.binary_sensor_schema(),
        cv.Required(CONF_AUTO_DRY_ACTIVE): binary_sensor.binary_sensor_schema(),

        cv.Required(CONF_PURIFIER): switch.switch_schema(LgSwitch),
        cv.Required(CONF_INTERNAL_THERMISTOR): switch.switch_schema(LgSwitch),
        cv.Required(CONF_AUTO_DRY): switch.switch_schema(LgSwitch),
        cv.Optional(CONF_ZONE1): switch.switch_schema(LgSwitch),
        cv.Optional(CONF_ZONE2): switch.switch_schema(LgSwitch),
        cv.Optional(CONF_ZONE3): switch.switch_schema(LgSwitch),
        cv.Optional(CONF_ZONE4): switch.switch_schema(LgSwitch),
        cv.Optional(CONF_ZONE5): switch.switch_schema(LgSwitch),
        cv.Optional(CONF_ZONE6): switch.switch_schema(LgSwitch),
        cv.Optional(CONF_ZONE7): switch.switch_schema(LgSwitch),
        cv.Optional(CONF_ZONE8): switch.switch_schema(LgSwitch),

        cv.Optional(CONF_TYPE_B_REFRESH_INTERVAL, default=600): cv.int_range(min=0, max=3600),
        cv.Optional(CONF_INITIAL_TYPE_B_RETRIES, default=2): cv.int_range(min=0, max=5),
        cv.Optional(CONF_RESPOND_TO_TYPE4, default=True): cv.boolean,
        cv.Optional(CONF_ZONE_COUNT, default=0): cv.int_range(min=0, max=8),
        **{
            cv.Optional(key): binary_sensor.binary_sensor_schema()
            for key in OPTIONAL_BINARY_SENSORS
        },
        **{cv.Optional(key): sensor.sensor_schema() for key in OPTIONAL_SENSORS},
        cv.Optional(CONF_LAST_UNSUPPORTED_MESSAGE): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_CAPABILITIES): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_THERMISTOR_2TH_SWITCH): switch.switch_schema(LgSwitch),
        **{cv.Optional(key): switch.switch_schema(LgSwitch) for key in FIELD_SWITCHES},
        **{cv.Optional(key): select.select_schema(LgSelect) for key in FIELD_SELECTS},
        **{cv.Optional(key): number.number_schema(LgNumber) for key in FIELD_NUMBERS},
        **{cv.Optional(key): number.number_schema(LgNumber) for key in OTHER_NUMBERS},
        **{cv.Optional(key): button.button_schema(LgButton) for key in BUTTONS},
    }
).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)

async def to_code(config):
    # Build all entities declared in the YAML and pass them into the native controller.
    # ESPHome will generate the necessary bindings so the C++ implementation can publish
    # state updates and consume user actions.
    rx_pin = await cg.gpio_pin_expression(config[CONF_RX_PIN])

    if CONF_TEMPERATURE_SENSOR in config:
        temperature_sensor = await cg.get_variable(config[CONF_TEMPERATURE_SENSOR])
    else:
        temperature_sensor = cg.nullptr

    # Hide entities not supported for 'slave' controllers.
    if config[CONF_IS_SLAVE_CONTROLLER]:
        config[CONF_FAN_SPEED_SLOW][CONF_INTERNAL] = True
        config[CONF_FAN_SPEED_LOW][CONF_INTERNAL] = True
        config[CONF_FAN_SPEED_MEDIUM][CONF_INTERNAL] = True
        config[CONF_FAN_SPEED_HIGH][CONF_INTERNAL] = True
        if CONF_FAN_SPEED_POWER in config:
            config[CONF_FAN_SPEED_POWER][CONF_INTERNAL] = True
        config[CONF_OVERHEATING][CONF_INTERNAL] = True
        config[CONF_INTERNAL_THERMISTOR][CONF_INTERNAL] = True
        installer_keys = [CONF_THERMISTOR_2TH_SWITCH]
        for table in (FIELD_SWITCHES, FIELD_SELECTS, FIELD_NUMBERS, OTHER_NUMBERS, BUTTONS):
            installer_keys += [key for key, entry in table.items() if entry[1]]
        for key in installer_keys:
            if key in config:
                config[key][CONF_INTERNAL] = True

    vane1 = await select.new_select(config[CONF_VANE1], options=VANE_OPTIONS)
    vane2 = await select.new_select(config[CONF_VANE2], options=VANE_OPTIONS)
    vane3 = await select.new_select(config[CONF_VANE3], options=VANE_OPTIONS)
    vane4 = await select.new_select(config[CONF_VANE4], options=VANE_OPTIONS)
    overheating = await select.new_select(config[CONF_OVERHEATING], options=OVERHEATING_OPTIONS)

    fan_speed_slow = await number.new_number(config[CONF_FAN_SPEED_SLOW], min_value=0, max_value=255, step=1)
    fan_speed_low = await number.new_number(config[CONF_FAN_SPEED_LOW], min_value=0, max_value=255, step=1)
    fan_speed_medium = await number.new_number(config[CONF_FAN_SPEED_MEDIUM], min_value=0, max_value=255, step=1)
    fan_speed_high = await number.new_number(config[CONF_FAN_SPEED_HIGH], min_value=0, max_value=255, step=1)
    if CONF_FAN_SPEED_POWER in config:
        fan_speed_power = await number.new_number(
            config[CONF_FAN_SPEED_POWER], min_value=0, max_value=255, step=1
        )
    else:
        fan_speed_power = cg.nullptr
    sleep_timer = await number.new_number(config[CONF_SLEEP_TIMER], min_value=0, max_value=420, step=1)

    error_code = await sensor.new_sensor(config[CONF_ERROR_CODE])
    pipe_temp_in = await sensor.new_sensor(config[CONF_PIPE_TEMP_IN])
    pipe_temp_mid = await sensor.new_sensor(config[CONF_PIPE_TEMP_MID])
    pipe_temp_out = await sensor.new_sensor(config[CONF_PIPE_TEMP_OUT])

    defrost = await binary_sensor.new_binary_sensor(config[CONF_DEFROST])
    preheat = await binary_sensor.new_binary_sensor(config[CONF_PREHEAT])
    outdoor = await binary_sensor.new_binary_sensor(config[CONF_OUTDOOR])
    auto_dry_active = await binary_sensor.new_binary_sensor(config[CONF_AUTO_DRY_ACTIVE])

    purifier = await switch.new_switch(config[CONF_PURIFIER])
    internal_thermistor = await switch.new_switch(config[CONF_INTERNAL_THERMISTOR])
    auto_dry = await switch.new_switch(config[CONF_AUTO_DRY])

    async def optional_zone(key):
        if key in config:
            return await switch.new_switch(config[key])
        return cg.nullptr

    zone1 = await optional_zone(CONF_ZONE1)
    zone2 = await optional_zone(CONF_ZONE2)
    zone3 = await optional_zone(CONF_ZONE3)
    zone4 = await optional_zone(CONF_ZONE4)
    zone5 = await optional_zone(CONF_ZONE5)
    zone6 = await optional_zone(CONF_ZONE6)
    zone7 = await optional_zone(CONF_ZONE7)
    zone8 = await optional_zone(CONF_ZONE8)

    var = cg.new_Pvariable(config[CONF_ID], rx_pin, temperature_sensor,
                           vane1, vane2, vane3, vane4, overheating,
                           fan_speed_slow, fan_speed_low, fan_speed_medium, fan_speed_high,
                           fan_speed_power,
                           sleep_timer,
                           error_code, pipe_temp_in, pipe_temp_mid, pipe_temp_out,
                           defrost, preheat, outdoor, auto_dry_active,
                           purifier, internal_thermistor, auto_dry,
                           zone1, zone2, zone3, zone4, zone5, zone6, zone7, zone8,
                           config[CONF_FAHRENHEIT], config[CONF_IS_SLAVE_CONTROLLER],
                           config[CONF_TYPE_B_REFRESH_INTERVAL], config[CONF_ZONE_COUNT],
                           config[CONF_INITIAL_TYPE_B_RETRIES], config[CONF_RESPOND_TO_TYPE4])

    for key, setter in OPTIONAL_BINARY_SENSORS.items():
        if key in config:
            entity = await binary_sensor.new_binary_sensor(config[key])
            cg.add(getattr(var, setter)(entity))

    for key, setter in OPTIONAL_SENSORS.items():
        if key in config:
            entity = await sensor.new_sensor(config[key])
            cg.add(getattr(var, setter)(entity))

    if CONF_LAST_UNSUPPORTED_MESSAGE in config:
        entity = await text_sensor.new_text_sensor(config[CONF_LAST_UNSUPPORTED_MESSAGE])
        cg.add(var.set_last_unsupported_message_text_sensor(entity))

    if CONF_CAPABILITIES in config:
        entity = await text_sensor.new_text_sensor(config[CONF_CAPABILITIES])
        cg.add(var.set_capabilities_text_sensor(entity))

    if CONF_THERMISTOR_2TH_SWITCH in config:
        entity = await switch.new_switch(config[CONF_THERMISTOR_2TH_SWITCH])
        cg.add(var.set_thermistor_2th_switch(entity))

    for key, (field, _) in FIELD_SWITCHES.items():
        if key in config:
            entity = await switch.new_switch(config[key])
            cg.add(var.set_field_switch(getattr(Field, field), entity))

    for key, (field, _, options) in FIELD_SELECTS.items():
        if key in config:
            entity = await select.new_select(config[key], options=options)
            cg.add(var.set_field_select(getattr(Field, field), entity))

    for key, (field, _, minimum, maximum) in FIELD_NUMBERS.items():
        if key in config:
            entity = await number.new_number(
                config[key], min_value=minimum, max_value=maximum, step=1
            )
            cg.add(var.set_field_number(getattr(Field, field), entity))

    for key, (setter, _, minimum, maximum, step) in OTHER_NUMBERS.items():
        if key in config:
            entity = await number.new_number(
                config[key], min_value=minimum, max_value=maximum, step=step
            )
            cg.add(getattr(var, setter)(entity))

    for key, (setter, _) in BUTTONS.items():
        if key in config:
            entity = await button.new_button(config[key])
            cg.add(getattr(var, setter)(entity))

    await climate.register_climate(var, config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
