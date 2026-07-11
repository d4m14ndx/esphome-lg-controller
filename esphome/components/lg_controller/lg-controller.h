#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <utility>

#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "lg-protocol.h"

static const char* const TAG = "lg-controller";

namespace esphome::lg_controller {

// Native implementation for the custom `lg_controller` climate platform. ESPHome generates the
// wiring to Home Assistant via the Python component in climate.py; this file focuses on the UART
// protocol and maps ESPHome's climate traits (https://esphome.io/components/climate/) to the LG
// indoor unit message format.
static constexpr size_t MIN_TEMP_SETPOINT = 16;
static constexpr size_t MAX_TEMP_SETPOINT = 30;

class LgSwitch final : public switch_::Switch {
    std::function<void(bool)> control_callback_{};
    std::function<bool(bool)> control_validator_{};

    void write_state(bool value) override {
        if (control_validator_ && !control_validator_(value)) {
            // Re-publish the actual value so Home Assistant does not show a command that the
            // protocol rejected (for example, a zone above the learned installed count).
            publish_state(this->state);
            return;
        }
        publish_state(value);
        if (control_callback_) {
            control_callback_(value);
        }
    }

public:
    void add_on_control_callback(std::function<void(bool)> callback) {
        control_callback_ = std::move(callback);
    }

    void set_control_validator(std::function<bool(bool)> validator) {
        control_validator_ = std::move(validator);
    }

    void restore_and_set_mode(switch_::SwitchRestoreMode mode) {
        // Mirror ESPHome's switch restore behaviour so HA reflects the saved state on boot.
        set_restore_mode(mode);
        if (auto state = get_initial_state_with_restore_mode()) {
            publish_state(*state);
        }
    }
};

class LgSelect final : public select::Select {
    void control(const std::string& value) override {
        if (this->current_option() != value) {
            // Only publish changes locally; installer settings get sent in batches elsewhere.
            this->publish_state(value); 
        }
    }
};

class LgNumber final : public number::Number {
    std::function<void(float)> control_callback_{};

    void control(float value) override {
        if (this->state != value) {
            // Persist number changes immediately so they are available when sending 0xCA/0xCB.
            this->publish_state(value);
        }
        if (control_callback_) {
            control_callback_(value);
        }
    }

public:
    void add_on_control_callback(std::function<void(float)> callback) {
        control_callback_ = std::move(callback);
    }
};

// The LG protocol always uses Celsius. The HA/ESPHome climate component internally
// converts between Fahrenheit and Celsius. Values from the Home Assistant room temperature sensor
// are not converted automatically so can be Celsius or Fahrenheit.
//
// Unfortunately LG uses their own Fahrenheit/Celsius mapping that's different from what you'd
// expect. For example, 78F is ~25.5C, but LG controllers will send 26C for 78F. A value of 25.5C
// would be interpreted by the AC as 77F.
//
// This class has some functions to convert between Fahrenheit, Celsius and "LG-Celsius" (values
// we send to or receive from the unit). This ensures Home Assistant and the LG unit always agree
// on the setpoint in Fahrenheit.
//
// These conversions are only used in Fahrenheit mode.
class TempConversion {
private:
    // Precomputed Fahrenheit->LG Celsius mapping derived from the controller display. Values are
    // doubled to preserve the 0.5°C step. LG uses this non-linear mapping instead of the standard
    // Fahrenheit to Celsius formula, so we mirror it here.
    static constexpr int8_t FahToLGCel[] = {
        0  /* 32  */, 1  /* 33  */, 2  /* 34  */, 3  /* 35  */, 4  /* 36  */,
        5  /* 37  */, 6  /* 38  */, 7  /* 39  */, 8  /* 40  */, 10 /* 41  */,
        12 /* 42  */, 13 /* 43  */, 14 /* 44  */, 15 /* 45  */, 16 /* 46  */,
        17 /* 47  */, 18 /* 48  */, 19 /* 49  */, 20 /* 50  */, 21 /* 51  */,
        22 /* 52  */, 23 /* 53  */, 24 /* 54  */, 25 /* 55  */, 26 /* 56  */,
        27 /* 57  */, 28 /* 58  */, 30 /* 59  */, 32 /* 60  */, 33 /* 61  */,
        34 /* 62  */, 35 /* 63  */, 36 /* 64  */, 37 /* 65  */, 38 /* 66  */,
        39 /* 67  */, 40 /* 68  */, 41 /* 69  */, 42 /* 70  */, 43 /* 71  */,
        44 /* 72  */, 45 /* 73  */, 46 /* 74  */, 47 /* 75  */, 48 /* 76  */,
        50 /* 77  */, 52 /* 78  */, 53 /* 79  */, 54 /* 80  */, 55 /* 81  */,
        56 /* 82  */, 57 /* 83  */, 58 /* 84  */, 59 /* 85  */, 60 /* 86  */,
        61 /* 87  */, 62 /* 88  */, 63 /* 89  */, 64 /* 90  */, 65 /* 91  */,
        66 /* 92  */, 67 /* 93  */, 68 /* 94  */, 70 /* 95  */, 72 /* 96  */,
        73 /* 97  */, 74 /* 98  */, 75 /* 99  */, 76 /* 100 */, 77 /* 101 */,
        78 /* 102 */, 79 /* 103 */, 80 /* 104 */
    };
    static constexpr int8_t LGCelToCelAdjustment[] = {
         0 /* 0    */,  0 /* 0.5  */,  0 /* 1.0  */,  0 /* 1.5  */,  0 /* 2.0  */,
         1 /* 2.5  */,  1 /* 3.0  */,  1 /* 3.5  */,  1 /* 4.0  */,  0 /* 4.5  */,
         0 /* 5.0  */, -1 /* 5.5  */, -1 /* 6.0  */, -1 /* 6.5  */, -1 /* 7.0  */,
        -1 /* 7.5  */,  0 /* 8.0  */,  0 /* 8.5  */,  0 /* 9.0  */,  0 /* 9.5  */,
         0 /* 10.0 */,  0 /* 10.5 */,  0 /* 11.0 */,  0 /* 11.5 */,  0 /* 12.0 */,
         1 /* 12.5 */,  1 /* 13.0 */,  1 /* 13.5 */,  1 /* 14.0 */,  0 /* 14.5 */,
         0 /* 15.0 */, -1 /* 15.5 */, -1 /* 16.0 */, -1 /* 16.5 */, -1 /* 17.0 */,
        -1 /* 17.5 */,  0 /* 18.0 */,  0 /* 18.5 */,  0 /* 19.0 */,  0 /* 19.5 */,
         0 /* 20.0 */,  0 /* 20.5 */,  0 /* 21.0 */,  0 /* 21.5 */,  0 /* 22.0 */,
         1 /* 22.5 */,  1 /* 23.0 */,  1 /* 23.5 */,  1 /* 24.0 */,  0 /* 24.5 */,
         0 /* 25.0 */, -1 /* 25.5 */, -1 /* 26.0 */, -1 /* 26.5 */, -1 /* 27.0 */,
        -1 /* 27.5 */,  0 /* 28.0 */,  0 /* 28.5 */,  0 /* 29.0 */,  0 /* 29.5 */,
         0 /* 30.0 */,  0 /* 30.5 */,  0 /* 31.0 */,  0 /* 31.5 */,  0 /* 32.0 */,
         1 /* 32.5 */,  1 /* 33.0 */,  1 /* 33.5 */,  1 /* 34.0 */,  0 /* 34.5 */,
         0 /* 35.0 */, -1 /* 35.5 */, -1 /* 36.0 */, -1 /* 36.5 */, -1 /* 37.0 */,
        -1 /* 37.5 */,  0 /* 38.0 */,  0 /* 38.5 */,  0 /* 39.0 */,  0 /* 39.5 */,
         0 /* 40.0 */
    };

public:
    // Convert from Fahrenheit to LG-Celsius (using the LG-compatible conversion).
    static float fahrenheit_to_lgcelsius(float temp) {
        int temp_int = int(round(temp));
        if (temp_int < 32 || temp_int > 104) {
            return esphome::fahrenheit_to_celsius(temp);
        }
        int8_t val = FahToLGCel[temp_int - 32];
        return float(val / 2) + ((val & 1) ? 0.5f : 0.0f);
    }
    // Convert an LG-Celsius value to Celsius. This is done to ensure the LG unit and HA agree
    // on the value in Fahrenheit. For example, the unit sends 78F as 26C (LG Celsius), but HA
    // would convert this to 78.8F => 79F. To work around this, we adjust 26C to 25.5C because
    // this maps to 78F in HA.
    static float lgcelsius_to_celsius(float temp) {
        int index = int(temp * 2);
        if (index < 0 || index >= sizeof(LGCelToCelAdjustment)) {
            return temp;
        }
        int8_t adjustment = LGCelToCelAdjustment[index];
        if (adjustment == -1) {
            return temp - 0.5;
        }
        if (adjustment == 1) {
            return temp + 0.5;
        }
        return temp;
    }
    static float celsius_to_lgcelsius(float temp) {
        float fahrenheit = esphome::celsius_to_fahrenheit(temp);
        return fahrenheit_to_lgcelsius(fahrenheit);
    }
};
constexpr int8_t TempConversion::FahToLGCel[];
constexpr int8_t TempConversion::LGCelToCelAdjustment[];

class LgController final : public climate::Climate, public uart::UARTDevice, public Component {
    static constexpr size_t MsgLen = protocol::kFrameSize;

    climate::ClimateTraits supported_traits_{};

    InternalGPIOPin& rx_pin_;
    esphome::sensor::Sensor* temperature_sensor_;

    LgSelect& vane_select_1_;
    LgSelect& vane_select_2_;
    LgSelect& vane_select_3_;
    LgSelect& vane_select_4_;
    LgSelect& overheating_select_;

    LgNumber& fan_speed_slow_;
    LgNumber& fan_speed_low_;
    LgNumber& fan_speed_medium_;
    LgNumber& fan_speed_high_;
    LgNumber* fan_speed_power_;

    LgNumber& sleep_timer_;

    esphome::sensor::Sensor& error_code_;
    esphome::sensor::Sensor& pipe_temp_in_;
    esphome::sensor::Sensor& pipe_temp_mid_;
    esphome::sensor::Sensor& pipe_temp_out_;
    esphome::binary_sensor::BinarySensor& defrost_;
    esphome::binary_sensor::BinarySensor& preheat_;
    esphome::binary_sensor::BinarySensor& outdoor_;
    esphome::binary_sensor::BinarySensor& auto_dry_active_;
    uint32_t last_outdoor_change_millis_ = 0;

    LgSwitch& purifier_;
    LgSwitch& internal_thermistor_;
    LgSwitch& auto_dry_;
    LgSwitch* zone_switches_[8] = {};

    struct ProtocolEntities {
        binary_sensor::BinarySensor* filter_warning = nullptr;
        binary_sensor::BinarySensor* energy_saving = nullptr;
        binary_sensor::BinarySensor* external_ventilation = nullptr;
        binary_sensor::BinarySensor* humidifier_active = nullptr;
        binary_sensor::BinarySensor* auxiliary_heater_active = nullptr;
        binary_sensor::BinarySensor* swirl_active = nullptr;
        binary_sensor::BinarySensor* fan_auto_active = nullptr;
        binary_sensor::BinarySensor* robot_clean_active = nullptr;
        binary_sensor::BinarySensor* cooling_active = nullptr;
        binary_sensor::BinarySensor* heating_active = nullptr;
        binary_sensor::BinarySensor* oil_warning = nullptr;
        binary_sensor::BinarySensor* half_degree_control = nullptr;
        binary_sensor::BinarySensor* emergency_heater = nullptr;
        binary_sensor::BinarySensor* emergency_heater_fan = nullptr;
        binary_sensor::BinarySensor* group_control = nullptr;
        binary_sensor::BinarySensor* wifi_ap = nullptr;
        binary_sensor::BinarySensor* refrigerant_leak_detector = nullptr;
        binary_sensor::BinarySensor* occupancy = nullptr;
        binary_sensor::BinarySensor* dual_setpoint = nullptr;
        binary_sensor::BinarySensor* himalaya_cooling = nullptr;
        binary_sensor::BinarySensor* mosquito_away = nullptr;
        binary_sensor::BinarySensor* comfort_cooling = nullptr;

        sensor::Sensor* fan_mode_code = nullptr;
        sensor::Sensor* dred_mode = nullptr;
        sensor::Sensor* overcooling_mode = nullptr;
        sensor::Sensor* outdoor_model = nullptr;
        sensor::Sensor* indoor_model = nullptr;
        sensor::Sensor* model_capacity = nullptr;
        sensor::Sensor* static_pressure = nullptr;
        sensor::Sensor* thermal_off_fan_mode = nullptr;
        sensor::Sensor* low_ambient_heating = nullptr;
        sensor::Sensor* configured_zone_count = nullptr;
        sensor::Sensor* filter_time_remaining = nullptr;
        sensor::Sensor* energy_total = nullptr;
        sensor::Sensor* high_setpoint = nullptr;
        sensor::Sensor* low_setpoint = nullptr;
        sensor::Sensor* type4_room_temperature = nullptr;
        sensor::Sensor* deadband = nullptr;
        sensor::Sensor* humidity = nullptr;
        sensor::Sensor* fan_runtime = nullptr;
        sensor::Sensor* unit_runtime = nullptr;
        sensor::Sensor* precise_room_temperature = nullptr;
        sensor::Sensor* type7_power_raw = nullptr;
        sensor::Sensor* invalid_frames = nullptr;

        text_sensor::TextSensor* last_unsupported_message = nullptr;
    } protocol_entities_;

    protocol::FrameParser frame_parser_{};
    uint32_t last_recv_millis_ = 0;
    uint32_t last_bus_activity_millis_ = 0;
    uint32_t invalid_frame_count_ = 0;

    // Last received 0xC8 message.
    uint8_t last_recv_status_[MsgLen] = {};

    // Last received 0xCA message.
    uint8_t last_recv_type_a_settings_[MsgLen] = {};

    // Last received 0xCB message.
    uint8_t last_recv_type_b_settings_[MsgLen] = {};

    // Last received 0xCC extended status message.
    uint8_t last_recv_type_c_status_[MsgLen] = {};

    uint8_t send_buf_[MsgLen] = {};
    uint32_t last_sent_status_millis_ = 0;
    uint32_t last_sent_recv_type_b_millis_ = 0;
    uint32_t last_initial_type_b_request_millis_ = 0;
    uint32_t pending_send_millis_ = 0;

    using PendingSendKind = protocol::TransactionKind;
    PendingSendKind pending_send_ = PendingSendKind::None;
    PendingSendKind pending_echo_retry_kind_ = PendingSendKind::None;
    uint8_t echo_retry_buf_[MsgLen] = {};
    bool pending_echo_retry_ = false;
    uint8_t send_echo_retry_count_ = 0;
    bool status_write_in_flight_ = false;

    bool pending_status_change_ = false;
    bool pending_type_a_settings_change_ = false;
    bool pending_type_b_settings_change_ = false;
    bool pending_type_b_query_ = false;
    bool pending_type_c_response_ = false;
    bool awaiting_type_b_response_ = false;
    bool initial_status_sent_ = false;
    bool transmit_enabled_ = false;
    uint8_t initial_type_b_request_count_ = 0;

    bool is_initializing_ = true;

    uint8_t vane_position_[4] = {0,0,0,0};
    bool vane_dirty_[4] = {};
    uint8_t fan_speed_[5] = {0,0,0,0,0};
    bool fan_speed_dirty_[5] = {};
    bool auto_dry_dirty_ = false;
    bool preserve_extended_fan_mode_ = false;
    uint8_t overheating_ = 0;
    bool zones_on_[8] = {};
    uint8_t zone_count_ = 0;
    uint8_t zone_capacity_ = 0;
    bool zone_protocol_supported_ = false;
    bool zone_state_known_[8] = {};

    float min_setpoint_ = MIN_TEMP_SETPOINT;
    float max_setpoint_ = MAX_TEMP_SETPOINT;
    bool type_a_limits_known_ = false;

    optional<uint32_t> sleep_timer_target_millis_{};
    bool active_reservation_ = false;

    uint32_t NVS_STORAGE_VERSION = 2843654U; // Change version if the NVSStorage struct changes
    struct NVSStorage {
        uint8_t capabilities_message[13] = {};
    };
    NVSStorage nvs_storage_;

    const bool fahrenheit_;

    // Whether a message came from the HVAC unit, a master controller, or a slave controller.
    enum class MessageSender : uint8_t { Unit, Master, Slave };
    // Set if this controller is configured as slave controller.
    const bool slave_;
    const uint32_t type_b_refresh_interval_ms_;
    const uint8_t configured_zone_count_;
    const uint8_t initial_type_b_retry_count_;
    const bool respond_to_type_c_;

    enum class LgCapability {
        FAN_AUTO,
        FAN_SLOW,
        FAN_LOW,
        FAN_LOW_MEDIUM,
        FAN_MEDIUM,
        FAN_MEDIUM_HIGH,
        FAN_HIGH,
        MODE_HEATING,
        MODE_FAN,
        MODE_AUTO,
        MODE_DEHUMIDIFY,
        VERTICAL_SWING,
        HORIZONTAL_SWING,
    };

    // Interpret the capability bits from the stored 0xC9 message so we can expose only the modes
    // and entities the indoor unit supports.
    bool parse_capability(LgCapability capability) {
        switch (capability) {
            case LgCapability::FAN_AUTO:
                return (nvs_storage_.capabilities_message[3] & 0x01) != 0;
            case LgCapability::FAN_SLOW:
                return (nvs_storage_.capabilities_message[3] & 0x20) != 0;
            case LgCapability::FAN_LOW:
                return (nvs_storage_.capabilities_message[3] & 0x10) != 0;
            case LgCapability::FAN_LOW_MEDIUM:
                return (nvs_storage_.capabilities_message[6] & 0x08) != 0;
            case LgCapability::FAN_MEDIUM:
                return (nvs_storage_.capabilities_message[3] & 0x08) != 0;
            case LgCapability::FAN_MEDIUM_HIGH:
                return (nvs_storage_.capabilities_message[6] & 0x10) != 0;
            case LgCapability::FAN_HIGH:
                return protocol::supports_high_fan(nvs_storage_.capabilities_message);
            case LgCapability::MODE_HEATING:
                return protocol::supports_heating(nvs_storage_.capabilities_message);
            case LgCapability::MODE_FAN:
                return protocol::supports_fan_mode(nvs_storage_.capabilities_message);
            case LgCapability::MODE_AUTO:
                return (nvs_storage_.capabilities_message[2] & 0x08) != 0;
            case LgCapability::MODE_DEHUMIDIFY:
                return protocol::supports_dry_mode(nvs_storage_.capabilities_message);
            case LgCapability::VERTICAL_SWING:
                return (nvs_storage_.capabilities_message[1] & 0x80) != 0;
            case LgCapability::HORIZONTAL_SWING:
                return (nvs_storage_.capabilities_message[1] & 0x40) != 0;
        }
        return false;
    }

    void configure_capabilities() {
        // Map the unit's capability bits to the ClimateTraits ESPHome exposes to Home Assistant.
        // See https://esphome.io/components/climate/ for how these traits influence the UI.
        // Default traits
        climate::ClimateModeMask device_modes;
        device_modes.insert(climate::CLIMATE_MODE_OFF);
        device_modes.insert(climate::CLIMATE_MODE_COOL);
        device_modes.insert(climate::CLIMATE_MODE_HEAT);
        device_modes.insert(climate::CLIMATE_MODE_DRY);
        device_modes.insert(climate::CLIMATE_MODE_FAN_ONLY);
        device_modes.insert(climate::CLIMATE_MODE_HEAT_COOL);
        
        climate::ClimateFanModeMask fan_modes;
        fan_modes.insert(climate::CLIMATE_FAN_LOW);
        fan_modes.insert(climate::CLIMATE_FAN_MEDIUM);
        fan_modes.insert(climate::CLIMATE_FAN_HIGH);
        fan_modes.insert(climate::CLIMATE_FAN_AUTO);
        
        climate::ClimateSwingModeMask swing_modes;
        swing_modes.insert(climate::CLIMATE_SWING_OFF);
        swing_modes.insert(climate::CLIMATE_SWING_BOTH);
        swing_modes.insert(climate::CLIMATE_SWING_VERTICAL);
        swing_modes.insert(climate::CLIMATE_SWING_HORIZONTAL);

        supported_traits_.set_supported_modes(device_modes);
        supported_traits_.set_supported_fan_modes(fan_modes);
        supported_traits_.set_supported_swing_modes(swing_modes);
        supported_traits_.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
        if (!type_a_limits_known_) {
            min_setpoint_ = MIN_TEMP_SETPOINT;
            max_setpoint_ = MAX_TEMP_SETPOINT;
        }
        supported_traits_.set_visual_min_temperature(min_setpoint_);
        supported_traits_.set_visual_max_temperature(max_setpoint_);
        supported_traits_.set_visual_current_temperature_step(fahrenheit_ ? 1 : 0.5);
        supported_traits_.set_visual_target_temperature_step(fahrenheit_ ? 1 : 0.5);

        // Only override defaults if the capabilities are known
        if (nvs_storage_.capabilities_message[0] != 0) {
            // Configure the climate traits
            climate::ClimateModeMask override_device_modes;
            override_device_modes.insert(climate::CLIMATE_MODE_OFF);
            override_device_modes.insert(climate::CLIMATE_MODE_COOL);
            if (parse_capability(LgCapability::MODE_HEATING))
                override_device_modes.insert(climate::CLIMATE_MODE_HEAT);
            if (parse_capability(LgCapability::MODE_FAN))
                override_device_modes.insert(climate::CLIMATE_MODE_FAN_ONLY);
            if (parse_capability(LgCapability::MODE_AUTO))
                override_device_modes.insert(climate::CLIMATE_MODE_HEAT_COOL);
            if (parse_capability(LgCapability::MODE_DEHUMIDIFY))
                override_device_modes.insert(climate::CLIMATE_MODE_DRY);
            supported_traits_.set_supported_modes(override_device_modes);

            climate::ClimateFanModeMask override_fan_modes;
            if (parse_capability(LgCapability::FAN_AUTO))
                override_fan_modes.insert(climate::CLIMATE_FAN_AUTO);
            if (parse_capability(LgCapability::FAN_SLOW))
                override_fan_modes.insert(climate::CLIMATE_FAN_QUIET);
            if (parse_capability(LgCapability::FAN_LOW))
                override_fan_modes.insert(climate::CLIMATE_FAN_LOW);
            if (parse_capability(LgCapability::FAN_MEDIUM))
                override_fan_modes.insert(climate::CLIMATE_FAN_MEDIUM);
            if (parse_capability(LgCapability::FAN_HIGH))
                override_fan_modes.insert(climate::CLIMATE_FAN_HIGH);
            supported_traits_.set_supported_fan_modes(override_fan_modes);

            climate::ClimateSwingModeMask override_swing_modes;
            override_swing_modes.insert(climate::CLIMATE_SWING_OFF);
            if (parse_capability(LgCapability::VERTICAL_SWING) && parse_capability(LgCapability::HORIZONTAL_SWING))
                override_swing_modes.insert(climate::CLIMATE_SWING_BOTH);
            if (parse_capability(LgCapability::VERTICAL_SWING))
                override_swing_modes.insert(climate::CLIMATE_SWING_VERTICAL);
            if (parse_capability(LgCapability::HORIZONTAL_SWING))
                override_swing_modes.insert(climate::CLIMATE_SWING_HORIZONTAL);
            supported_traits_.set_supported_swing_modes(override_swing_modes);

            // Some units only support whole-degree setpoints and/or a minimum of 18 C.
            if (!type_a_limits_known_) {
                min_setpoint_ = (nvs_storage_.capabilities_message[6] & 0x20) ? 16 : 18;
            }
            float target_step =
                fahrenheit_ ? 1.0f : ((nvs_storage_.capabilities_message[5] & 0x20) ? 1.0f : 0.5f);
            supported_traits_.set_visual_min_temperature(min_setpoint_);
            supported_traits_.set_visual_max_temperature(max_setpoint_);
            supported_traits_.set_visual_target_temperature_step(target_step);
        }

        zone_capacity_ = protocol::maximum_zone_count(nvs_storage_.capabilities_message);
        zone_protocol_supported_ = zone_capacity_ > 0;

        // A configured count is authoritative. In auto mode, wait for the exact Type-B count;
        // capability bits describe the maximum, not the number actually installed.
        if (configured_zone_count_ > 0) {
            set_zone_count(configured_zone_count_);
        } else if (!zone_protocol_supported_) {
            set_zone_count(0);
        }
    }

public:
    LgController(InternalGPIOPin* rx_pin,
                 sensor::Sensor* temperature_sensor,
                 LgSelect* vane_select_1,
                 LgSelect* vane_select_2,
                 LgSelect* vane_select_3,
                 LgSelect* vane_select_4,
                 LgSelect* overheating_select,
                 LgNumber* fan_speed_slow,
                 LgNumber* fan_speed_low,
                 LgNumber* fan_speed_medium,
                 LgNumber* fan_speed_high,
                 LgNumber* fan_speed_power,
                 LgNumber* sleep_timer,
                 sensor::Sensor* error_code,
                 sensor::Sensor* pipe_temp_in,
                 sensor::Sensor* pipe_temp_mid,
                 sensor::Sensor* pipe_temp_out,
                 binary_sensor::BinarySensor* defrost,
                 binary_sensor::BinarySensor* preheat,
                 binary_sensor::BinarySensor* outdoor,
                 binary_sensor::BinarySensor* auto_dry_active,
                 LgSwitch* purifier,
                 LgSwitch* internal_thermistor,
                 LgSwitch* auto_dry,
                 LgSwitch* zone1,
                 LgSwitch* zone2,
                 LgSwitch* zone3,
                 LgSwitch* zone4,
                 LgSwitch* zone5,
                 LgSwitch* zone6,
                 LgSwitch* zone7,
                 LgSwitch* zone8,
                 bool fahrenheit, bool is_slave_controller,
                 uint32_t type_b_refresh_interval_seconds,
                 uint8_t configured_zone_count,
                 uint8_t initial_type_b_retry_count,
                 bool respond_to_type_c)
      : rx_pin_(*rx_pin),
        temperature_sensor_(temperature_sensor),
        vane_select_1_(*vane_select_1),
        vane_select_2_(*vane_select_2),
        vane_select_3_(*vane_select_3),
        vane_select_4_(*vane_select_4),
        overheating_select_(*overheating_select),
        fan_speed_slow_(*fan_speed_slow),
        fan_speed_low_(*fan_speed_low),
        fan_speed_medium_(*fan_speed_medium),
        fan_speed_high_(*fan_speed_high),
        fan_speed_power_(fan_speed_power),
        sleep_timer_(*sleep_timer),
        error_code_(*error_code),
        pipe_temp_in_(*pipe_temp_in),
        pipe_temp_mid_(*pipe_temp_mid),
        pipe_temp_out_(*pipe_temp_out),
        defrost_(*defrost),
        preheat_(*preheat),
        outdoor_(*outdoor),
        auto_dry_active_(*auto_dry_active),
        purifier_(*purifier),
        internal_thermistor_(*internal_thermistor),
        auto_dry_(*auto_dry),
        fahrenheit_(fahrenheit),
        slave_(is_slave_controller),
        type_b_refresh_interval_ms_(type_b_refresh_interval_seconds * 1000),
        configured_zone_count_(configured_zone_count),
        initial_type_b_retry_count_(initial_type_b_retry_count),
        respond_to_type_c_(respond_to_type_c)
    {
        // Route per-entity callbacks back into the controller so outbound frames are updated.
        zone_switches_[0] = zone1;
        zone_switches_[1] = zone2;
        zone_switches_[2] = zone3;
        zone_switches_[3] = zone4;
        zone_switches_[4] = zone5;
        zone_switches_[5] = zone6;
        zone_switches_[6] = zone7;
        zone_switches_[7] = zone8;
        vane_select_1_.add_on_state_callback([this](size_t index) {
            set_vane_position(1, index);
        });
        vane_select_2_.add_on_state_callback([this](size_t index) {
            set_vane_position(2, index);
        });
        vane_select_3_.add_on_state_callback([this](size_t index) {
            set_vane_position(3, index);
        });
        vane_select_4_.add_on_state_callback([this](size_t index) {
            set_vane_position(4, index);
        });
        overheating_select_.add_on_state_callback([this](size_t index) {
            set_overheating(index);
        });

        fan_speed_slow_.add_on_control_callback([this](float v) {
            set_fan_speed(0, v);
        });
        fan_speed_low_.add_on_control_callback([this](float v) {
            set_fan_speed(1, v);
        });
        fan_speed_medium_.add_on_control_callback([this](float v) {
            set_fan_speed(2, v);
        });
        fan_speed_high_.add_on_control_callback([this](float v) {
            set_fan_speed(3, v);
        });
        if (fan_speed_power_ != nullptr) {
            fan_speed_power_->add_on_control_callback([this](float v) {
                set_fan_speed(4, v);
            });
        }
        sleep_timer_.add_on_control_callback([this](float v) {
            set_sleep_timer(v);
        });

        purifier_.add_on_control_callback([this](bool) {
            pending_status_change_ = true;
        });
        internal_thermistor_.add_on_control_callback([this](bool) {
            pending_status_change_ = true;
        });
        auto_dry_.add_on_control_callback([this](bool) {
            auto_dry_dirty_ = true;
            pending_type_a_settings_change_ = true;
        });

        for (size_t i = 0; i < 8; ++i) {
            if (zone_switches_[i] != nullptr) {
                zone_switches_[i]->set_control_validator([this, i](bool) {
                    return validate_zone_control(i);
                });
                zone_switches_[i]->add_on_control_callback([this, i](bool state) {
                    set_zone_state(i, state);
                });
            }
        }
        set_zone_count(configured_zone_count_);
    }

    void set_filter_warning_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.filter_warning = entity; }
    void set_energy_saving_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.energy_saving = entity; }
    void set_external_ventilation_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.external_ventilation = entity; }
    void set_humidifier_active_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.humidifier_active = entity; }
    void set_auxiliary_heater_active_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.auxiliary_heater_active = entity; }
    void set_swirl_active_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.swirl_active = entity; }
    void set_fan_auto_active_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.fan_auto_active = entity; }
    void set_robot_clean_active_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.robot_clean_active = entity; }
    void set_cooling_active_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.cooling_active = entity; }
    void set_heating_active_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.heating_active = entity; }
    void set_oil_warning_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.oil_warning = entity; }
    void set_half_degree_control_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.half_degree_control = entity; }
    void set_emergency_heater_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.emergency_heater = entity; }
    void set_emergency_heater_fan_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.emergency_heater_fan = entity; }
    void set_group_control_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.group_control = entity; }
    void set_wifi_ap_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.wifi_ap = entity; }
    void set_refrigerant_leak_detector_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.refrigerant_leak_detector = entity; }
    void set_occupancy_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.occupancy = entity; }
    void set_dual_setpoint_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.dual_setpoint = entity; }
    void set_himalaya_cooling_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.himalaya_cooling = entity; }
    void set_mosquito_away_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.mosquito_away = entity; }
    void set_comfort_cooling_binary_sensor(binary_sensor::BinarySensor* entity) { protocol_entities_.comfort_cooling = entity; }

    void set_fan_mode_code_sensor(sensor::Sensor* entity) { protocol_entities_.fan_mode_code = entity; }
    void set_dred_mode_sensor(sensor::Sensor* entity) { protocol_entities_.dred_mode = entity; }
    void set_overcooling_mode_sensor(sensor::Sensor* entity) { protocol_entities_.overcooling_mode = entity; }
    void set_outdoor_model_sensor(sensor::Sensor* entity) { protocol_entities_.outdoor_model = entity; }
    void set_indoor_model_sensor(sensor::Sensor* entity) { protocol_entities_.indoor_model = entity; }
    void set_model_capacity_sensor(sensor::Sensor* entity) { protocol_entities_.model_capacity = entity; }
    void set_static_pressure_sensor(sensor::Sensor* entity) { protocol_entities_.static_pressure = entity; }
    void set_thermal_off_fan_mode_sensor(sensor::Sensor* entity) { protocol_entities_.thermal_off_fan_mode = entity; }
    void set_low_ambient_heating_sensor(sensor::Sensor* entity) { protocol_entities_.low_ambient_heating = entity; }
    void set_configured_zone_count_sensor(sensor::Sensor* entity) { protocol_entities_.configured_zone_count = entity; }
    void set_filter_time_remaining_sensor(sensor::Sensor* entity) { protocol_entities_.filter_time_remaining = entity; }
    void set_energy_total_sensor(sensor::Sensor* entity) { protocol_entities_.energy_total = entity; }
    void set_high_setpoint_sensor(sensor::Sensor* entity) { protocol_entities_.high_setpoint = entity; }
    void set_low_setpoint_sensor(sensor::Sensor* entity) { protocol_entities_.low_setpoint = entity; }
    void set_type4_room_temperature_sensor(sensor::Sensor* entity) { protocol_entities_.type4_room_temperature = entity; }
    void set_deadband_sensor(sensor::Sensor* entity) { protocol_entities_.deadband = entity; }
    void set_humidity_sensor(sensor::Sensor* entity) { protocol_entities_.humidity = entity; }
    void set_fan_runtime_sensor(sensor::Sensor* entity) { protocol_entities_.fan_runtime = entity; }
    void set_unit_runtime_sensor(sensor::Sensor* entity) { protocol_entities_.unit_runtime = entity; }
    void set_precise_room_temperature_sensor(sensor::Sensor* entity) { protocol_entities_.precise_room_temperature = entity; }
    void set_type7_power_raw_sensor(sensor::Sensor* entity) { protocol_entities_.type7_power_raw = entity; }
    void set_invalid_frames_sensor(sensor::Sensor* entity) { protocol_entities_.invalid_frames = entity; }

    void set_last_unsupported_message_text_sensor(text_sensor::TextSensor* entity) {
        protocol_entities_.last_unsupported_message = entity;
    }

    float get_setup_priority() const override {
        return esphome::setup_priority::BUS;
    }

    void setup() override {
        // Load our custom NVS storage to get the capabilities message
        // (mirrors ESPHome preference handling at https://esphome.io/components/preferences.html).
        ESPPreferenceObject pref = global_preferences->make_preference<NVSStorage>(this->get_object_id_hash() ^ NVS_STORAGE_VERSION);
        pref.load(&nvs_storage_);

        auto restore = this->restore_state_();
        if (restore.has_value()) {
            restore->apply(this);
        } else {
            this->mode = climate::CLIMATE_MODE_OFF;
            this->target_temperature = 20;
            this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
            this->swing_mode = climate::CLIMATE_SWING_OFF;
            this->publish_state();
        }

        internal_thermistor_.restore_and_set_mode(esphome::switch_::SWITCH_RESTORE_DEFAULT_OFF);

        sleep_timer_.publish_state(0);

        // Configure climate traits and entities based on the capabilities message (if available)
        configure_capabilities();

        while (UARTDevice::available() > 0) {
            uint8_t b;
            UARTDevice::read_byte(&b);
        }
        last_bus_activity_millis_ = millis();
        pending_status_change_ = true;

        // Receive continuously in loop(), but give the physical bus and unit ten seconds to
        // settle before enabling transmissions. The scheduler itself never busy-waits.
        set_interval("update", 250, [this]() { update(); });
        set_timeout("initial_send", 10000, [this]() { transmit_enabled_ = true; });
    }

    // Process changes from HA.
    void control(const climate::ClimateCall &call) override {
        if (call.get_mode().has_value()) {
            if (this->mode != *call.get_mode()) {
                pending_type_a_settings_change_ = true;
            }
            this->mode = *call.get_mode();
        }
        if (call.get_target_temperature().has_value()) {
            this->target_temperature = *call.get_target_temperature();
        }
        if (call.get_fan_mode().has_value()) {
            this->fan_mode = *call.get_fan_mode();
            preserve_extended_fan_mode_ = false;
        }
        if (call.get_swing_mode().has_value()) {
            set_swing_mode(*call.get_swing_mode());
        }
        this->pending_status_change_ = true;
        this->publish_state();
    }

    climate::ClimateTraits traits() override {
        return supported_traits_;
    }

private:
    bool validate_zone_control(size_t index) const {
        if (index >= 8) {
            ESP_LOGE(TAG, "Unexpected zone index: %u", static_cast<unsigned>(index));
            return false;
        }
        if (zone_count_ == 0 || index >= zone_count_) {
            ESP_LOGD(TAG, "Ignoring zone %u change; zone control not available",
                     static_cast<unsigned>(index + 1));
            return false;
        }
        return true;
    }

    // Toggle a zone relay while keeping Home Assistant and the controller state in sync.
    void set_zone_state(size_t index, bool enabled, bool from_status = false) {
        if (!validate_zone_control(index)) {
            return;
        }
        bool changed = !zone_state_known_[index] || zones_on_[index] != enabled;
        zone_state_known_[index] = true;
        if (!changed) {
            // Make sure HA state stays in sync even if the value didn't change.
            if (from_status && zone_switches_[index] != nullptr) {
                zone_switches_[index]->publish_state(enabled);
            }
            return;
        }
        zones_on_[index] = enabled;
        if (zone_switches_[index] != nullptr) {
            zone_switches_[index]->publish_state(enabled);
        }
        if (!from_status) {
            pending_status_change_ = true;
        }
    }

    // Enable/disable the exposed zone switches to match the unit capability.
    void set_zone_count(uint8_t count) {
        if (count > 8) {
            count = 8;
        }
        zone_count_ = count;
        if (protocol_entities_.configured_zone_count != nullptr) {
            protocol_entities_.configured_zone_count->publish_state(zone_count_);
        }
        if (zone_count_ == 0) {
            std::fill(zone_state_known_, zone_state_known_ + 8, false);
        } else {
            std::fill(zone_state_known_ + zone_count_, zone_state_known_ + 8, false);
        }
    }

    // Sets position of vane index (1-4) to position (0-6).
    void set_vane_position(int index, int position) {
        if (index < 1 || index > 4) {
            ESP_LOGE(TAG, "Unexpected vane index: %d", index);
            return;
        }
        if (position < 0 || position > 6) {
            ESP_LOGE(TAG, "Unexpected vane position: %d", position);
            return;
        }
        if (vane_position_[index-1] == position) {
            return;
        }
        ESP_LOGD(TAG, "Setting vane %d position: %d", index, position);
        vane_position_[index-1] = position;
        vane_dirty_[index - 1] = true;
        pending_type_a_settings_change_ = true;
    }

    // Sets installer fan speed index (0-4 for slow-power) to 0-255; zero is factory default.
    void set_fan_speed(int index, int value) {
        if (index < 0 || index > 4) {
            ESP_LOGE(TAG, "Unexpected fan speed index: %d", index);
            return;
        }
        if (value<0 || value>255) {
            ESP_LOGE(TAG, "Unexpected fan speed: %d", value);
            return;
        }

        if (fan_speed_[index] == value) {
            return;
        }

        fan_speed_[index] = value;
        fan_speed_dirty_[index] = true;
        pending_type_a_settings_change_ = true;
    }

    // Set overheating installer setting 15 to 0-4.
    void set_overheating(int value) {
        if (value < 0 || value > 4) {
            ESP_LOGE(TAG, "Unexpected overheating value: %d", value);
            return;
        }

        if (overheating_ == value) {
            return;
        }

        ESP_LOGD(TAG, "Setting overheating installer setting: %d", value);

        overheating_ = value;
        pending_type_b_settings_change_ = true;
    }

    void set_sleep_timer(int minutes) {
        // 0 clears the timer. Accept max 7 hours.
        if (minutes < 0 || minutes > 7 * 60) {
            ESP_LOGE(TAG, "Ignoring invalid sleep timer value: %d minutes", minutes);
            return;
        }
        ESP_LOGD(TAG, "Setting sleep timer: %d minutes", minutes);
        if (minutes > 0) {
            sleep_timer_target_millis_ = millis() + unsigned(minutes) * 60 * 1000;
            active_reservation_ = true;
        } else {
            sleep_timer_target_millis_.reset();
            active_reservation_ = false;
        }
        pending_status_change_ = true;
    }

    optional<float> get_room_temp() const {
        if (temperature_sensor_ == nullptr) {
            return {};
        }

        float temp = temperature_sensor_->get_state();
        if (std::isnan(temp) || temp == 0) {
            return {};
        }
        if (fahrenheit_) {
            temp = TempConversion::fahrenheit_to_lgcelsius(temp);
        }
        if (temp < 11) {
            return 11;
        }
        if (temp > 35) {
            return 35;
        }
        // Round to nearest 0.5 degrees.
        return round(temp * 2) / 2;
    }

    optional<uint32_t> get_sleep_timer_minutes() const {
        if (!sleep_timer_target_millis_.has_value()) {
            return {};
        }
        int32_t diff = int32_t(sleep_timer_target_millis_.value() - millis());
        if (diff <= 0) {
            return {};
        }
        uint32_t minutes = uint32_t(diff) / 1000 / 60 + 1;
        return minutes;
    }

    static uint8_t calc_checksum(const uint8_t* buffer) {
        return protocol::calculate_checksum(buffer);
    }

    // Track requested swing mode; if vertical swing stops we need to resend vane positions.
    void set_swing_mode(climate::ClimateSwingMode mode, bool from_status = false) {
        if (!from_status && this->swing_mode != mode) {
            // If vertical swing is off, send a 0xAA message to restore the vane position.
            if (mode == climate::CLIMATE_SWING_OFF || mode == climate::CLIMATE_SWING_HORIZONTAL) {
                pending_type_a_settings_change_ = true;
            }
        }
        this->swing_mode = mode;
    }

    // Build and send the 0xA8/0x28 status frame that carries the user-facing climate state.
    // ESPHome maps climate traits to HA; this mirrors that state onto LG's protocol.
    void send_status_message() {
        bool initialization_request = !initial_status_sent_ && !slave_;

        // Byte 0: message type.
        send_buf_[0] = slave_ ? 0x28 : 0xA8;

        // Byte 1: changed flag (0x1), power on (0x2), mode (0x1C), fan speed (0x70).
        uint8_t b = 0;
        if (pending_status_change_) {
            b |= 0x1;
        }
        switch (this->mode) {
            case climate::CLIMATE_MODE_COOL:
                b |= (0 << 2) | 0x2;
                break;
            case climate::CLIMATE_MODE_DRY:
                b |= (1 << 2) | 0x2;
                break;
            case climate::CLIMATE_MODE_FAN_ONLY:
                b |= (2 << 2) | 0x2;
                break;
            case climate::CLIMATE_MODE_HEAT_COOL:
                b |= (3 << 2) | 0x2;
                break;
            case climate::CLIMATE_MODE_HEAT:
                b |= (4 << 2) | 0x2;
                break;
            case climate::CLIMATE_MODE_OFF:
                // Don't set power-on flag, but preserve previous operation mode.
                b |= (last_recv_status_[1] & 0x1C);
                break;
            default:
                ESP_LOGE(TAG, "unknown operation mode, turning off");
                b |= (2 << 2);
                break;
        }
        
        // Preserve protocol-only low-medium, medium-high and power speeds until HA explicitly
        // selects a standard fan mode. This prevents periodic status frames downgrading them.
        if (preserve_extended_fan_mode_) {
            b |= last_recv_status_[1] & 0xE0;
        } else if (this->fan_mode.has_value()) {
            switch (this->fan_mode.value()) {
                case climate::CLIMATE_FAN_LOW:
                    b |= 0 << 5;
                    break;
                case climate::CLIMATE_FAN_MEDIUM:
                    b |= 1 << 5;
                    break;
                case climate::CLIMATE_FAN_HIGH:
                    b |= 2 << 5;
                    break;
                case climate::CLIMATE_FAN_AUTO:
                    b |= 3 << 5;
                    break;
                case climate::CLIMATE_FAN_QUIET:
                    b |= 4 << 5;
                    break;
                default:
                    ESP_LOGE(TAG, "unknown fan mode, using Medium");
                    b |= 1 << 5;
                    break;
            }
        } else {
            // Default to Medium if no fan mode is set
            ESP_LOGD(TAG, "no fan mode set, using Medium as default");
            b |= 1 << 5;
        }
        send_buf_[1] = b;

        // Byte 2: swing mode and purifier/plasma setting. Preserve the other bits.
        b = last_recv_status_[2] & ~(0x4|0x40|0x80);
        if (purifier_.state) {
            b |= 0x4;
        }
        switch (this->swing_mode) {
            case climate::CLIMATE_SWING_OFF:
                break;
            case climate::CLIMATE_SWING_HORIZONTAL:
                b |= 0x40;
                break;
            case climate::CLIMATE_SWING_VERTICAL:
                b |= 0x80;
                break;
            case climate::CLIMATE_SWING_BOTH:
                b |= 0x40 | 0x80;
                break;
            default:
                ESP_LOGE(TAG, "unknown swing mode");
                break;
        }
        send_buf_[2] = b;

        // Byte 3.
        send_buf_[3] = last_recv_status_[3];
        if (active_reservation_) {
            send_buf_[3] |= 0x10;
        } else {
            send_buf_[3] &= ~0x10;
        }

        // Byte 4.
        send_buf_[4] = last_recv_status_[4];

        float target = this->target_temperature;
        if (fahrenheit_) {
            target = TempConversion::celsius_to_lgcelsius(target);
        }
        if (target < min_setpoint_) {
            target = min_setpoint_;
        } else if (target > max_setpoint_) {
            target = max_setpoint_;
        }

        // Byte 5. Unchanged except for the low bit which indicates the target temperature has a
        // 0.5 fractional part.
        send_buf_[5] = last_recv_status_[5] & ~0x1;
        if (target - uint8_t(target) == 0.5) {
            send_buf_[5] |= 0x1;
        }

        // Byte 6: thermistor setting and target temperature (fractional part in byte 5).
        // Byte 7: room temperature. Preserve the (unknown) upper two bits.
        enum ThermistorSetting { Unit = 0, Controller = 1, TwoTH = 2 };
        ThermistorSetting thermistor =
            internal_thermistor_.state ? ThermistorSetting::Unit : ThermistorSetting::Controller;
        float temp;
        if (auto maybe_temp = get_room_temp()) {
            temp = *maybe_temp;
        } else {
            // Room temperature isn't available. Use the unit's thermistor and send something
            // reasonable.
            thermistor = ThermistorSetting::Unit;
            temp = 20;
        }
        // Preserve ceiling-height bits 6-7 while replacing thermistor and target fields.
        send_buf_[6] = (last_recv_status_[6] & 0xC0) | (thermistor << 4) |
                       ((uint8_t(target) - 15) & 0xf);
        send_buf_[7] = (last_recv_status_[7] & 0xC0) | uint8_t((temp - 10) * 2);

        // Preserve reservation data we do not own. Clearing bytes 8-9 while leaving the active
        // reservation flag set can corrupt turn-on/off reservations created by another control.
        send_buf_[8] = active_reservation_ ? last_recv_status_[8] : 0;
        send_buf_[9] = active_reservation_ ? last_recv_status_[9] : 0;
        send_buf_[10] = last_recv_status_[10];

        if (initialization_request) {
            // Request settings when controller turns on.
            send_buf_[8] |= 0x40;
            // Set bit 0x80 of byte 10 to use byte 9 for the Fahrenheit setting flag (0x40).
            if (fahrenheit_) {
                send_buf_[9] |= 0x40;
            }
            send_buf_[10] = 0x80;
        } else if (optional<uint32_t> minutes = get_sleep_timer_minutes()) {
            // Set sleep timer.
            // Byte 8 stores the kind (0x38) and high bits of number of minutes (0x7).
            // Byte 9 stores the low bits of the number of minutes.
            constexpr uint8_t timer_kind_sleep = 3;
            send_buf_[8] = timer_kind_sleep << 3;
            send_buf_[8] |= (*minutes >> 8) & 0b111;
            send_buf_[9] = *minutes & 0xff;
        }
        if (zone_count_ > 0) {
            protocol::Frame status_frame{};
            protocol::ZoneStates zone_states{};
            protocol::ZoneStates controlled_zones{};
            std::copy(send_buf_, send_buf_ + MsgLen, status_frame.begin());
            std::copy(zones_on_, zones_on_ + 8, zone_states.begin());
            std::copy(zone_state_known_, zone_state_known_ + 8, controlled_zones.begin());
            protocol::encode_status_zones(status_frame, zone_count_, zone_states, controlled_zones);
            std::copy(status_frame.begin(), status_frame.end(), send_buf_);
        }

        // Byte 11.
        send_buf_[11] = last_recv_status_[11];

        // Byte 12.
        send_buf_[12] = calc_checksum(send_buf_);

        ESP_LOGD(TAG, "sending %s", format_hex_pretty(send_buf_, MsgLen).c_str());
        UARTDevice::write_array(send_buf_, MsgLen);

        status_write_in_flight_ = pending_status_change_;
        pending_status_change_ = false;
        pending_send_ = PendingSendKind::Status;
        pending_send_millis_ = millis();
        last_sent_status_millis_ = millis();
        if (initialization_request) {
            initial_status_sent_ = true;
            last_initial_type_b_request_millis_ = millis();
        }

        // If we sent an updated temperature to the AC, update temperature in HA too.
        // Slave controller temperature sensor is ignored.
        if (!slave_ && thermistor == ThermistorSetting::Controller) {
            float ha_temp = temp;
            if (fahrenheit_) {
                ha_temp = TempConversion::lgcelsius_to_celsius(ha_temp);
            }
            if (this->current_temperature != ha_temp) {
                this->current_temperature = ha_temp;
                publish_state();
            }
        }
    }

    // Send installer "Type A" settings (fan curves, vane positions, auto-dry flag).
    bool send_type_a_settings_message() {
        if (last_recv_type_a_settings_[0] != 0xCA && last_recv_type_a_settings_[0] != 0xAA) {
            ESP_LOGW(TAG, "Cannot send Type-A settings before receiving CA/AA");
            return false;
        }

        // Copy settings from the CA/AA message we received.
        memcpy(send_buf_, last_recv_type_a_settings_, MsgLen);
        send_buf_[0] = slave_ ? 0x2A : 0xAA;

        // Bytes 2-6 store the installer fan speeds
        send_buf_[2] = fan_speed_[0];
        send_buf_[3] = fan_speed_[1];
        send_buf_[4] = fan_speed_[2];
        send_buf_[5] = fan_speed_[3];
        if (fan_speed_power_ != nullptr) {
            send_buf_[6] = fan_speed_[4];
        }

        // Bytes 7-8 store vane positions.
        send_buf_[7] = (send_buf_[7] & 0xf0) | (vane_position_[0] & 0x0f); // Set vane 1
        send_buf_[7] = (send_buf_[7] & 0x0f) | ((vane_position_[1] & 0x0f) << 4); // Set vane 2
        send_buf_[8] = (send_buf_[8] & 0xf0) | (vane_position_[2] & 0x0f); // Set vane 3
        send_buf_[8] = (send_buf_[8] & 0x0f) | ((vane_position_[3] & 0x0f) << 4); // Set vane 4

        // Set auto dry setting.
        uint8_t b = send_buf_[11] & ~0x8;
        if (auto_dry_.state) {
            b |= 0x8;
        }
        send_buf_[11] = b;

        send_buf_[12] = calc_checksum(send_buf_);

        ESP_LOGD(TAG, "sending %s", format_hex_pretty(send_buf_, MsgLen).c_str());
        UARTDevice::write_array(send_buf_, MsgLen);

        pending_type_a_settings_change_ = false;
        std::fill(vane_dirty_, vane_dirty_ + 4, false);
        std::fill(fan_speed_dirty_, fan_speed_dirty_ + 5, false);
        auto_dry_dirty_ = false;
        pending_send_ = PendingSendKind::TypeA;
        pending_send_millis_ = millis();
        return true;
    }

    // Send installer Type-B settings or a bounded query for a fresh CB response.
    bool send_type_b_settings_message(bool query, bool allow_empty = false) {
        if (query) {
            ESP_LOGD(TAG, "sending Type-B query");
        }
        if (last_recv_type_b_settings_[0] != 0xCB && last_recv_type_b_settings_[0] != 0xAB) {
            if (!allow_empty) {
                ESP_LOGW(TAG, "Cannot send Type-B settings before receiving CB/AB");
                return false;
            }
            // AB.80 is the documented minimal query. It is only used after the normal A8
            // initialization request has timed out, and retries are bounded by configuration.
            memset(send_buf_, 0, MsgLen);
        } else {
            // Copy settings from the CB/AB message we received.
            memcpy(send_buf_, last_recv_type_b_settings_, MsgLen);
        }
        send_buf_[0] = slave_ ? 0x2B : 0xAB;

        // Set the high bit of the second byte to request a CB message from the unit.
        if (query) {
            send_buf_[1] |= 0x80;
        } else {
            send_buf_[1] &= ~0x80;
        }

        // Byte 2 stores installer setting 15.
        send_buf_[2] = (send_buf_[2] & 0xC7) | (overheating_ << 3);

        // Clear pipe temperature fields; we should not send values here.
        send_buf_[3] = 0;
        send_buf_[4] = 0;
        send_buf_[5] = 0;

        send_buf_[12] = calc_checksum(send_buf_);

        ESP_LOGD(TAG, "sending %s", format_hex_pretty(send_buf_, MsgLen).c_str());
        UARTDevice::write_array(send_buf_, MsgLen);

        uint32_t now = millis();
        pending_type_b_settings_change_ = query ? pending_type_b_settings_change_ : false;
        pending_type_b_query_ = query ? false : pending_type_b_query_;
        pending_send_ = query ? PendingSendKind::TypeBQuery : PendingSendKind::TypeBWrite;
        pending_send_millis_ = now;
        last_sent_recv_type_b_millis_ = now;
        if (query) {
            awaiting_type_b_response_ = true;
            last_initial_type_b_request_millis_ = now;
        }
        return true;
    }

    void process_message(const uint8_t* buffer) {
        ESP_LOGD(TAG, "received %s", format_hex_pretty(buffer, MsgLen).c_str());

        if (pending_send_ != PendingSendKind::None && memcmp(send_buf_, buffer, MsgLen) == 0) {
            ESP_LOGD(TAG, "verified send");
            if (pending_send_ == PendingSendKind::Status) {
                status_write_in_flight_ = false;
            }
            pending_send_ = PendingSendKind::None;
            send_echo_retry_count_ = 0;
            return;
        }

        // Determine message type.
        optional<MessageSender> sender;
        switch (buffer[0] & 0xf8) {
            case 0xC8:
                sender = MessageSender::Unit;
                break;
            case 0xA8:
                if (!slave_) {
                    // Ignore (our own?) master controller messages.
                    return;
                }
                sender = MessageSender::Master;
                break;
            case 0x28:
                if (slave_) {
                    // Ignore (our own?) slave controller messages.
                    return;
                }
                sender = MessageSender::Slave;
                break;
            default:
                return; // Unknown message sender. Ignore.
        }

        switch (buffer[0] & 0b111) {
            case 0: // 0xC8/A8/28
                process_status_message(*sender, buffer);
                break;
            case 1: // 0xC9
                process_capabilities_message(*sender, buffer);
                break;
            case 2: // 0xCA/AA/2A
                process_type_a_settings_message(*sender, buffer);
                break;
            case 3: // 0xCB/AB/2B
                process_type_b_settings_message(*sender, buffer);
                break;
            case 4: // 0xCC/AC/2C
                process_type_c_status_message(*sender, buffer);
                break;
            case 6: // 0xCE/AE/2E
                process_type_e_message(*sender, buffer);
                break;
            case 7: // 0xCF/AF/2F
                process_type_f_message(*sender, buffer);
                break;
            default:
                publish_unsupported_message(buffer);
                return;
        }
    }

    void process_status_message(MessageSender sender, const uint8_t* buffer) {
        // Consider slave controller initialized if we received a status message from the other
        // controller or the unit.
        if (slave_) {
            is_initializing_ = false;
        }

        // Unit-only telemetry must not be cleared by a peer controller's status frame.
        if (sender == MessageSender::Unit) {
            // A unit status supersedes an unchanged heartbeat that is only awaiting its echo.
            // Never retry a stale heartbeat over a newer app/remote state.
            if (!status_write_in_flight_ && pending_send_ == PendingSendKind::Status) {
                pending_send_ = PendingSendKind::None;
                send_echo_retry_count_ = 0;
            }
            if (!status_write_in_flight_ &&
                pending_echo_retry_kind_ == PendingSendKind::Status) {
                pending_echo_retry_kind_ = PendingSendKind::None;
                pending_echo_retry_ = false;
                send_echo_retry_count_ = 0;
            }

            defrost_.publish_state(buffer[3] & 0x4);
            preheat_.publish_state(buffer[3] & 0x8);
            error_code_.publish_state(buffer[11]);

            // When turning on the outdoor unit, some units briefly report ON => OFF => ON.
            // Only advance the debounce timestamp when a state is actually published.
            bool outdoor_on = (buffer[5] & 0x4) != 0;
            if (outdoor_.state != outdoor_on &&
                (outdoor_on || millis() - last_outdoor_change_millis_ > 8000)) {
                outdoor_.publish_state(outdoor_on);
                last_outdoor_change_millis_ = millis();
            }

            if (zone_count_ <= 4) {
                bool unit_off = (buffer[1] & 0x2) == 0;
                auto_dry_active_.publish_state((buffer[10] & 0x10) && unit_off);
            }

            if (protocol_entities_.filter_warning != nullptr) {
                protocol_entities_.filter_warning->publish_state(buffer[2] & 0x01);
            }
            if (protocol_entities_.energy_saving != nullptr) {
                protocol_entities_.energy_saving->publish_state(buffer[5] & 0x02);
            }
            if (protocol_entities_.external_ventilation != nullptr) {
                protocol_entities_.external_ventilation->publish_state(buffer[3] & 0x01);
            }
            if (protocol_entities_.humidifier_active != nullptr) {
                protocol_entities_.humidifier_active->publish_state(buffer[2] & 0x08);
            }
            if (protocol_entities_.auxiliary_heater_active != nullptr) {
                protocol_entities_.auxiliary_heater_active->publish_state(buffer[2] & 0x10);
            }
            if (protocol_entities_.swirl_active != nullptr) {
                protocol_entities_.swirl_active->publish_state(buffer[2] & 0x20);
            }
            if (protocol_entities_.fan_auto_active != nullptr) {
                protocol_entities_.fan_auto_active->publish_state(buffer[3] & 0x02);
            }
            if (protocol_entities_.robot_clean_active != nullptr && zone_count_ <= 4) {
                protocol_entities_.robot_clean_active->publish_state(buffer[10] & 0x08);
            }
            if (protocol_entities_.cooling_active != nullptr) {
                protocol_entities_.cooling_active->publish_state(buffer[7] & 0x40);
            }
            if (protocol_entities_.heating_active != nullptr) {
                protocol_entities_.heating_active->publish_state(buffer[7] & 0x80);
            }
        }

        // Keep an up-to-date shadow frame for unknown bits even while a local change is pending.
        // All user-controlled fields are explicitly overlaid when constructing the next send.
        if (sender != MessageSender::Slave) {
            memcpy(last_recv_status_, buffer, MsgLen);
        }

        bool read_temp = false;
        if (slave_) {
            // Let the slave controller report the temperature from the master.
            read_temp = (sender == MessageSender::Master);
        } else {
            // Report the unit's room temperature only if we're using the internal thermistor.
            // With an external temperature sensor, some units report the temperature we sent and
            // others always send the internal temperature.
            read_temp = (sender == MessageSender::Unit && internal_thermistor_.state);
        }
        if (read_temp) {
            float room_temp = float(buffer[7] & 0x3F) / 2 + 10;
            if (fahrenheit_) {
                room_temp = TempConversion::lgcelsius_to_celsius(room_temp);
            }
            if (this->current_temperature != room_temp) {
                this->current_temperature = room_temp;
                publish_state();
            }
        }

        // Don't update our settings if we have a pending change/send, because else we overwrite
        // changes we still have to send (or are sending) to the AC.
        if (pending_status_change_) {
            ESP_LOGD(TAG, "ignoring because pending change");
            return;
        }
        if (status_write_in_flight_ &&
            protocol::transaction_active(PendingSendKind::Status, pending_send_,
                                         pending_echo_retry_kind_)) {
            ESP_LOGD(TAG, "ignoring because pending send");
            return;
        }

        // Zone bits alias unrelated functions on non-zoned and <=4-zone units. Decode them only
        // after an exact configured/Type-B count is known, and only after the pending-write guard.
        if (zone_count_ > 0) {
            protocol::Frame status_frame{};
            protocol::ZoneStates zone_states{};
            std::copy(buffer, buffer + MsgLen, status_frame.begin());
            protocol::decode_status_zones(status_frame, zone_count_, zone_states);
            for (size_t i = 0; i < zone_count_; ++i) {
                set_zone_state(i, zone_states[i], true);
            }
        }

        uint8_t b = buffer[1];
        if ((b & 0x2) == 0) {
            this->mode = climate::CLIMATE_MODE_OFF;
        } else {
            uint8_t mode_val = (b >> 2) & 0b111;
            switch (mode_val) {
                case 0:
                    this->mode = climate::CLIMATE_MODE_COOL;
                    break;
                case 1:
                    this->mode = climate::CLIMATE_MODE_DRY;
                    break;
                case 2:
                    this->mode = climate::CLIMATE_MODE_FAN_ONLY;
                    break;
                case 3:
                    this->mode = climate::CLIMATE_MODE_HEAT_COOL;
                    break;
                case 4:
                    this->mode = climate::CLIMATE_MODE_HEAT;
                    break;
                default:
                    ESP_LOGE(TAG, "received invalid operation mode from AC (%u)", mode_val);
                    return;
            }
        }

        uint8_t fan_val = b >> 5;
        if (protocol_entities_.fan_mode_code != nullptr) {
            protocol_entities_.fan_mode_code->publish_state(fan_val);
        }
        preserve_extended_fan_mode_ = fan_val >= 5;
        switch (fan_val) {
            case 0:
                this->fan_mode = climate::CLIMATE_FAN_LOW;
                break;
            case 1:
                this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
                break;
            case 2:
                this->fan_mode = climate::CLIMATE_FAN_HIGH;
                break;
            case 3:
                this->fan_mode = climate::CLIMATE_FAN_AUTO;
                break;
            case 4:
                this->fan_mode = climate::CLIMATE_FAN_QUIET;
                break;
            case 5: // Low-medium has no standard ESPHome enum; show Medium and preserve raw 5.
                this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
                break;
            case 6: // Medium-high has no standard enum; show High and preserve raw 6.
            case 7: // Power has no standard enum; show High and preserve raw 7.
                this->fan_mode = climate::CLIMATE_FAN_HIGH;
                break;
            default:
                return;
        }

        purifier_.publish_state(buffer[2] & 0x4);

        bool horiz_swing = buffer[2] & 0x40;
        bool vert_swing = buffer[2] & 0x80;
        if (horiz_swing && vert_swing) {
            set_swing_mode(climate::CLIMATE_SWING_BOTH, true);
        } else if (horiz_swing) {
            set_swing_mode(climate::CLIMATE_SWING_HORIZONTAL, true);
        } else if (vert_swing) {
            set_swing_mode(climate::CLIMATE_SWING_VERTICAL, true);
        } else {
            set_swing_mode(climate::CLIMATE_SWING_OFF, true);
        }

        float target = float((buffer[6] & 0xf) + 15);
        if (buffer[5] & 0x1) {
            target += 0.5;
        }
        if (fahrenheit_) {
            target = TempConversion::lgcelsius_to_celsius(target);
        }
        this->target_temperature = target;

        active_reservation_ = buffer[3] & 0x10;

        // Set or clear the locally tracked sleep timer without invoking a write callback.
        if (!active_reservation_) {
            sleep_timer_target_millis_.reset();
            sleep_timer_.publish_state(0);
        } else if (((buffer[8] >> 3) & 0x7) == 3) {
            uint32_t minutes = (uint32_t(buffer[8] & 0x7) << 8) | buffer[9];
            if (minutes > 0) {
                sleep_timer_target_millis_ = millis() + minutes * 60 * 1000;
            } else {
                sleep_timer_target_millis_.reset();
            }
            sleep_timer_.publish_state(minutes);
        }

        publish_state();
    }

    void process_capabilities_message(MessageSender sender, const uint8_t* buffer) {
        // Capabilities message. The unit sends this with the other settings so we're now
        // initialized.

        if (sender != MessageSender::Unit) {
            ESP_LOGE(TAG, "ignoring capabilities message not from unit");
            return;
        }

        // Check if we need to update the capabilities message.
        if (nvs_storage_.capabilities_message[0] == 0 ||
            std::memcmp(nvs_storage_.capabilities_message, buffer, MsgLen - 1) != 0) {

            bool needsRestart = (nvs_storage_.capabilities_message[0] == 0);

            ESPPreferenceObject pref = global_preferences->make_preference<NVSStorage>(this->get_object_id_hash() ^ NVS_STORAGE_VERSION);
            memcpy(nvs_storage_.capabilities_message, buffer, MsgLen);
            pref.save(&nvs_storage_);

            // Reboot only for the first discovery, but persist later capability changes too.
            if (needsRestart) {
                global_preferences->sync();
                ESP_LOGD(TAG, "restarting to apply initial capabilities");
                App.safe_reboot();
            } else {
                ESP_LOGD(TAG, "updated device capabilities");
            }
        }

        // Apply newly received capabilities immediately so zone entities become available without
        // requiring a restart.
        configure_capabilities();

        is_initializing_ = false;
    }

    void process_type_a_settings_message(MessageSender sender, const uint8_t* buffer) {
        if (sender != MessageSender::Slave) {
            memcpy(last_recv_type_a_settings_, buffer, MsgLen);
        }

        // Keep every controlled value stable until an in-flight Type-A frame is echoed.
        if (pending_send_ == PendingSendKind::TypeA ||
            pending_echo_retry_kind_ == PendingSendKind::TypeA) {
            return;
        }

        uint8_t received_vanes[4] = {
            static_cast<uint8_t>(buffer[7] & 0x0F),
            static_cast<uint8_t>((buffer[7] >> 4) & 0x0F),
            static_cast<uint8_t>(buffer[8] & 0x0F),
            static_cast<uint8_t>((buffer[8] >> 4) & 0x0F),
        };
        LgSelect* vane_selects[4] = {
            &vane_select_1_, &vane_select_2_, &vane_select_3_, &vane_select_4_,
        };
        for (size_t i = 0; i < 4; ++i) {
            if (received_vanes[i] <= 6) {
                if (!vane_dirty_[i]) {
                    vane_position_[i] = received_vanes[i];
                    vane_selects[i]->publish_state(*vane_selects[i]->at(received_vanes[i]));
                }
            } else {
                ESP_LOGE(TAG, "Unexpected vane %u position: %u",
                         static_cast<unsigned>(i + 1), received_vanes[i]);
            }
        }

        if (!auto_dry_dirty_) {
            auto_dry_.publish_state(buffer[11] & 0x8);
        }

        if (sender != MessageSender::Slave) {
            uint8_t received_fan_speeds[5] = {
                buffer[2], buffer[3], buffer[4], buffer[5], buffer[6],
            };
            LgNumber* fan_speed_entities[5] = {
                &fan_speed_slow_, &fan_speed_low_, &fan_speed_medium_, &fan_speed_high_,
                fan_speed_power_,
            };
            for (size_t i = 0; i < 5; ++i) {
                if (!fan_speed_dirty_[i]) {
                    fan_speed_[i] = received_fan_speeds[i];
                    if (fan_speed_entities[i] != nullptr) {
                        fan_speed_entities[i]->publish_state(fan_speed_[i]);
                    }
                }
            }

            uint8_t reported_min = (buffer[10] & 0x0F) + 15;
            uint8_t reported_max = (buffer[10] >> 4) + 15;
            if (reported_min >= 15 && reported_max <= 30 && reported_min <= reported_max) {
                min_setpoint_ = reported_min;
                max_setpoint_ = reported_max;
                type_a_limits_known_ = true;
                supported_traits_.set_visual_min_temperature(min_setpoint_);
                supported_traits_.set_visual_max_temperature(max_setpoint_);
            }
        }
    }

    void process_type_b_settings_message(MessageSender sender, const uint8_t* buffer) {
        // Determine whether to process this message based on controller role.
        bool from_unit = (sender == MessageSender::Unit);
        bool from_other_controller =
            (slave_ && sender == MessageSender::Master) || (!slave_ && sender == MessageSender::Slave);
        if (!from_unit && !from_other_controller) {
            return;
        }

        if (from_unit) {
            awaiting_type_b_response_ = false;
            pending_type_b_query_ = false;
            if (pending_send_ == PendingSendKind::TypeBQuery) {
                pending_send_ = PendingSendKind::None;
                send_echo_retry_count_ = 0;
            }
            if (pending_echo_retry_kind_ == PendingSendKind::TypeBQuery) {
                pending_echo_retry_kind_ = PendingSendKind::None;
                pending_echo_retry_ = false;
                send_echo_retry_count_ = 0;
            }
            memcpy(last_recv_type_b_settings_, buffer, MsgLen);
            last_sent_recv_type_b_millis_ = millis();
        }

        // Type-B byte 10 carries the actual configured count. A fixed YAML count wins; otherwise
        // accept a valid unit or peer value, bounded by the C9 maximum when known.
        uint8_t zones = buffer[10] >> 4;
        if (configured_zone_count_ == 0 && zones >= 1 && zones <= 8) {
            bool capabilities_known = nvs_storage_.capabilities_message[0] != 0;
            if (!capabilities_known || (zone_protocol_supported_ && zones <= zone_capacity_)) {
                set_zone_count(zones);
            } else {
                ESP_LOGW(TAG, "Ignoring zone count %u above capability maximum %u", zones,
                         zone_capacity_);
            }
        }

        // Peer AB/2B frames are controller-originated requests/settings. Their pipe bytes are
        // normally zero, so never treat them as unit telemetry.
        if (!from_unit) {
            return;
        }

        if (protocol_entities_.dred_mode != nullptr) {
            protocol_entities_.dred_mode->publish_state(buffer[1] & 0x03);
        }
        if (protocol_entities_.overcooling_mode != nullptr) {
            protocol_entities_.overcooling_mode->publish_state((buffer[2] >> 6) & 0x03);
        }
        if (protocol_entities_.oil_warning != nullptr) {
            protocol_entities_.oil_warning->publish_state(buffer[6] & 0x02);
        }
        if (protocol_entities_.half_degree_control != nullptr) {
            protocol_entities_.half_degree_control->publish_state(buffer[6] & 0x04);
        }
        if (protocol_entities_.emergency_heater != nullptr) {
            protocol_entities_.emergency_heater->publish_state(buffer[6] & 0x08);
        }
        if (protocol_entities_.emergency_heater_fan != nullptr) {
            protocol_entities_.emergency_heater_fan->publish_state(buffer[6] & 0x40);
        }
        if (protocol_entities_.group_control != nullptr) {
            protocol_entities_.group_control->publish_state(buffer[6] & 0x80);
        }
        if (protocol_entities_.wifi_ap != nullptr) {
            protocol_entities_.wifi_ap->publish_state(buffer[7] & 0x40);
        }
        if (protocol_entities_.outdoor_model != nullptr) {
            protocol_entities_.outdoor_model->publish_state(buffer[8] & 0x0F);
        }
        if (protocol_entities_.indoor_model != nullptr) {
            protocol_entities_.indoor_model->publish_state(buffer[8] >> 4);
        }
        if (protocol_entities_.model_capacity != nullptr) {
            protocol_entities_.model_capacity->publish_state(buffer[9] & 0x0F);
        }
        if (protocol_entities_.refrigerant_leak_detector != nullptr) {
            protocol_entities_.refrigerant_leak_detector->publish_state(buffer[9] & 0x10);
        }
        if (protocol_entities_.thermal_off_fan_mode != nullptr) {
            protocol_entities_.thermal_off_fan_mode->publish_state((buffer[9] >> 6) & 0x03);
        }
        if (protocol_entities_.static_pressure != nullptr) {
            protocol_entities_.static_pressure->publish_state(buffer[10] & 0x0F);
        }
        if (protocol_entities_.low_ambient_heating != nullptr) {
            protocol_entities_.low_ambient_heating->publish_state(buffer[11] & 0x0F);
        }

        if (!pending_type_b_settings_change_ && pending_send_ != PendingSendKind::TypeBWrite &&
            pending_echo_retry_kind_ != PendingSendKind::TypeBWrite) {
            uint8_t overheating = (buffer[2] >> 3) & 0b111;
            if (overheating <= 4) {
                overheating_ = overheating;
                overheating_select_.publish_state(*overheating_select_.at(overheating));
            } else {
                ESP_LOGE(TAG, "Unexpected overheating value: %u", overheating);
            }
        }

        // Table mapping a byte value to degrees Celsius based on values displayed by PREMTB100.
        // INT8_MIN indicates an invalid value.
        static constexpr int8_t PipeTempTable[] = {
            /* 0x00 */ INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN,
                       INT8_MIN, INT8_MIN, INT8_MIN, 108, 104, 101, 100, 98, 95,
            /* 0x10 */ 93, 91, 89, 87, 85, 84, 82, 81, 79, 78, 76, 75, 74, 73, 72, 71,
            /* 0x20 */ 70, 68, 68, 67, 66, 65, 64, 63, 62, 61, 60, 60, 59, 58, 57, 57,
            /* 0x30 */ 56, 55, 55, 54, 53, 53, 52, 52, 51, 50, 50, 49, 49, 48, 47, 47,
            /* 0x40 */ 46, 46, 45, 45, 44, 44, 43, 43, 42, 42, 41, 41, 40, 40, 39, 39,
            /* 0x50 */ 39, 38, 38, 37, 37, 36, 36, 36, 35, 35, 34, 34, 33, 33, 33, 32,
            /* 0x60 */ 32, 31, 31, 31, 30, 30, 30, 29, 29, 29, 28, 28, 27, 27, 27, 26,
            /* 0x70 */ 26, 26, 25, 25, 24, 24, 24, 23, 23, 23, 22, 22, 22, 21, 21, 21,
            /* 0x80 */ 20, 20, 20, 19, 19, 19, 18, 18, 18, 17, 17, 17, 16, 16, 16, 15,
            /* 0x90 */ 15, 15, 14, 14, 14, 13, 13, 13, 12, 12, 12, 11, 11, 11, 10, 10,
            /* 0xa0 */ 10, 9, 9, 9, 8, 8, 8, 7, 7, 6, 6, 6, 5, 5, 5, 4,
            /* 0xb0 */ 4, 4, 3, 3, 3, 2, 2, 2, 1, 1, 0, 0, 0, 0, 0, -1,
            /* 0xc0 */ -1, -2, -2, -2, -3, -3, -4, -4, -5, -5, -5, -6, -6, -7, -7, -8,
            /* 0xd0 */ -8, -9, -9, -9, -10, -10, -11, -11, -12, -12, -13, -14, -14, -15, -15, -16,
            /* 0xe0 */ -16, -17, -18, -18, -19, -20, -20, -21, -22, -22, -23, -24, -25, -26, -27,
                       -28,
            /* 0xf0 */ -29, INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN,
                       INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN, INT8_MIN
        };
        static_assert(sizeof(PipeTempTable) == 256);
        static_assert(PipeTempTable[UINT8_MAX] == INT8_MIN);

        // Pipe temperatures are only reliable in the unit's CB response.
        int8_t pipe_temp_in = PipeTempTable[buffer[3]];
        if (pipe_temp_in != INT8_MIN) {
            pipe_temp_in_.publish_state(pipe_temp_in);
        }

        int8_t pipe_temp_out = PipeTempTable[buffer[4]];
        if (pipe_temp_out != INT8_MIN) {
            pipe_temp_out_.publish_state(pipe_temp_out);
        }

        int8_t pipe_temp_mid = PipeTempTable[buffer[5]];
        if (pipe_temp_mid != INT8_MIN) {
            pipe_temp_mid_.publish_state(pipe_temp_mid);
        }
    }

    void publish_unsupported_message(const uint8_t* buffer) {
        if (protocol_entities_.last_unsupported_message != nullptr) {
            protocol_entities_.last_unsupported_message->publish_state(
                format_hex_pretty(buffer, MsgLen));
        }
    }

    void process_type_c_status_message(MessageSender sender, const uint8_t* buffer) {
        if (sender != MessageSender::Unit) {
            return;
        }

        memcpy(last_recv_type_c_status_, buffer, MsgLen);
        if (!slave_ && respond_to_type_c_) {
            pending_type_c_response_ = true;
        }

        if (protocol_entities_.filter_time_remaining != nullptr) {
            uint16_t hours = buffer[1] | (uint16_t(buffer[2] & 0x0F) << 8);
            protocol_entities_.filter_time_remaining->publish_state(hours);
        }
        if (protocol_entities_.occupancy != nullptr) {
            protocol_entities_.occupancy->publish_state(buffer[2] & 0x40);
        }
        if (protocol_entities_.dual_setpoint != nullptr) {
            protocol_entities_.dual_setpoint->publish_state(buffer[2] & 0x80);
        }

        uint32_t energy_tenths = 0;
        if (protocol_entities_.energy_total != nullptr &&
            protocol::decode_type4_energy_tenths(buffer + 3, energy_tenths)) {
            protocol_entities_.energy_total->publish_state(
                protocol::type4_energy_kwh(energy_tenths));
        }

        if (protocol_entities_.high_setpoint != nullptr) {
            float high = (buffer[6] & 0x7F) + ((buffer[8] >> 4) & 0x0F) / 10.0f;
            protocol_entities_.high_setpoint->publish_state(high);
        }
        if (protocol_entities_.low_setpoint != nullptr) {
            float low = (buffer[7] & 0x7F) + (buffer[8] & 0x0F) / 10.0f;
            protocol_entities_.low_setpoint->publish_state(low);
        }
        if (protocol_entities_.type4_room_temperature != nullptr) {
            protocol_entities_.type4_room_temperature->publish_state(buffer[9] / 2.0f);
        }
        if (protocol_entities_.deadband != nullptr && protocol::is_packed_bcd_byte(buffer[10])) {
            float deadband = ((buffer[10] >> 4) * 10 + (buffer[10] & 0x0F)) / 10.0f;
            protocol_entities_.deadband->publish_state(deadband);
        }
        if (protocol_entities_.himalaya_cooling != nullptr) {
            protocol_entities_.himalaya_cooling->publish_state(buffer[11] & 0x01);
        }
        if (protocol_entities_.mosquito_away != nullptr) {
            protocol_entities_.mosquito_away->publish_state(buffer[11] & 0x02);
        }
        if (protocol_entities_.comfort_cooling != nullptr) {
            protocol_entities_.comfort_cooling->publish_state(buffer[11] & 0x04);
        }
    }

    bool send_type_c_status_message() {
        if (last_recv_type_c_status_[0] != 0xCC) {
            pending_type_c_response_ = false;
            return false;
        }

        memcpy(send_buf_, last_recv_type_c_status_, MsgLen);
        send_buf_[0] = slave_ ? 0x2C : 0xAC;
        if (auto maybe_temp = get_room_temp()) {
            send_buf_[9] = uint8_t(std::min(127.5f, std::max(0.0f, *maybe_temp)) * 2);
        }
        send_buf_[12] = calc_checksum(send_buf_);

        ESP_LOGD(TAG, "sending %s", format_hex_pretty(send_buf_, MsgLen).c_str());
        UARTDevice::write_array(send_buf_, MsgLen);
        pending_type_c_response_ = false;
        pending_send_ = PendingSendKind::TypeC;
        pending_send_millis_ = millis();
        return true;
    }

    void process_type_e_message(MessageSender sender, const uint8_t* buffer) {
        if (sender != MessageSender::Unit || buffer[1] != 0x80) {
            publish_unsupported_message(buffer);
            return;
        }

        if (protocol_entities_.humidity != nullptr && buffer[2] <= 100) {
            protocol_entities_.humidity->publish_state(buffer[2]);
        }
        if (protocol_entities_.fan_runtime != nullptr) {
            protocol_entities_.fan_runtime->publish_state((uint16_t(buffer[3]) << 8) | buffer[4]);
        }
        if (protocol_entities_.unit_runtime != nullptr) {
            protocol_entities_.unit_runtime->publish_state((uint16_t(buffer[6]) << 8) | buffer[7]);
        }
        if (protocol_entities_.precise_room_temperature != nullptr && buffer[11] <= 9) {
            protocol_entities_.precise_room_temperature->publish_state(
                buffer[10] + buffer[11] / 10.0f);
        }
    }

    void process_type_f_message(MessageSender sender, const uint8_t* buffer) {
        if (sender == MessageSender::Unit && protocol_entities_.type7_power_raw != nullptr) {
            uint32_t raw = (uint32_t(buffer[2]) << 16) | (uint32_t(buffer[3]) << 8) | buffer[4];
            protocol_entities_.type7_power_raw->publish_state(raw);
        }
        publish_unsupported_message(buffer);
    }

    // Drain the very slow UART continuously. A rolling parser recovers alignment after noise,
    // collisions, dropped bytes and the zero padding used during initialization.
    void loop() override {
        protocol::Frame frame{};
        while (UARTDevice::available() > 0) {
            uint8_t byte = 0;
            if (!UARTDevice::read_byte(&byte)) {
                break;
            }

            last_recv_millis_ = millis();
            last_bus_activity_millis_ = last_recv_millis_;
            size_t invalid_before = frame_parser_.invalid_candidates();
            if (frame_parser_.push(byte, frame)) {
                process_message(frame.data());
            }
            if (frame_parser_.invalid_candidates() != invalid_before) {
                invalid_frame_count_ = frame_parser_.invalid_candidates();
                ESP_LOGW(TAG, "discarded invalid frame candidate while resynchronizing");
                if (protocol_entities_.invalid_frames != nullptr) {
                    protocol_entities_.invalid_frames->publish_state(invalid_frame_count_);
                }
            }
        }

        if (frame_parser_.buffered_size() > 0 && millis() - last_recv_millis_ > 2500) {
            ESP_LOGW(TAG, "discarding timed-out partial frame (%u bytes)",
                     static_cast<unsigned>(frame_parser_.buffered_size()));
            frame_parser_.reset();
        }
    }

    // Periodic non-blocking scheduler for outgoing transactions and timer maintenance.
    void update() {
        uint32_t now = millis();

        if (sleep_timer_target_millis_.has_value()) {
            int32_t diff = int32_t(sleep_timer_target_millis_.value() - now);
            if (diff <= 0) {
                ESP_LOGD(TAG, "Turning off for sleep timer");
                sleep_timer_target_millis_.reset();
                active_reservation_ = false;
                sleep_timer_.publish_state(0);
                this->mode = climate::CLIMATE_MODE_OFF;
                pending_status_change_ = true;
                publish_state();
            } else if (optional<uint32_t> minutes = get_sleep_timer_minutes()) {
                if (sleep_timer_.state != *minutes) {
                    sleep_timer_.publish_state(*minutes);
                }
            }
        }

        // A full echoed frame takes about 1.25 seconds at 104 baud. Wait three seconds before
        // classifying a send as collided/missing, then retry the same transaction kind.
        if (pending_send_ != PendingSendKind::None) {
            if (now - pending_send_millis_ <= 3000) {
                return;
            }

            bool retry = send_echo_retry_count_ < 2;
            ESP_LOGW(TAG, retry ? "missing send echo; retrying transaction"
                                : "missing send echo; retry limit reached");
            if (retry) {
                ++send_echo_retry_count_;
                memcpy(echo_retry_buf_, send_buf_, MsgLen);
                pending_echo_retry_kind_ = pending_send_;
                pending_echo_retry_ = true;
            } else {
                send_echo_retry_count_ = 0;
                if (pending_send_ == PendingSendKind::TypeBQuery) {
                    awaiting_type_b_response_ = false;
                }
                if (pending_send_ == PendingSendKind::Status) {
                    status_write_in_flight_ = false;
                }
            }
            pending_send_ = PendingSendKind::None;
            return;
        }

        // A query response timeout applies to startup, user-triggered and periodic refreshes. A
        // missed periodic response must not leave polling permanently disabled.
        if (protocol::type_b_response_timed_out(awaiting_type_b_response_, now,
                                                last_initial_type_b_request_millis_, 10000)) {
            awaiting_type_b_response_ = false;
            ESP_LOGW(TAG, "Type-B query timed out");
        }

        // The standard boot A8 asks for the complete settings batch first. Only if CB is still
        // absent do we issue a small, bounded number of explicit AB queries. Slaves stay passive.
        if (!slave_ && initial_status_sent_ && last_recv_type_b_settings_[0] == 0) {
            uint32_t retry_delay = initial_type_b_request_count_ == 0 ? 30000 : 10000;
            if (protocol::can_start_bounded_type_b_query(
                    slave_, pending_type_b_query_, awaiting_type_b_response_,
                    initial_type_b_request_count_, initial_type_b_retry_count_) &&
                now - last_initial_type_b_request_millis_ > retry_delay) {
                pending_type_b_query_ = true;
                ++initial_type_b_request_count_;
                last_initial_type_b_request_millis_ = now;
            }
        }

        if (!transmit_enabled_ || (slave_ && is_initializing_)) {
            return;
        }

        bool bus_idle = UARTDevice::available() == 0 && frame_parser_.buffered_size() == 0 &&
                        rx_pin_.digital_read() && now - last_bus_activity_millis_ >= 500;
        if (!bus_idle) {
            return;
        }

        if (pending_echo_retry_) {
            memcpy(send_buf_, echo_retry_buf_, MsgLen);
            ESP_LOGD(TAG, "retrying %s", format_hex_pretty(send_buf_, MsgLen).c_str());
            UARTDevice::write_array(send_buf_, MsgLen);
            pending_send_ = pending_echo_retry_kind_;
            pending_send_millis_ = millis();
            pending_echo_retry_kind_ = PendingSendKind::None;
            pending_echo_retry_ = false;
            return;
        }

        // Explicit user changes always outrank telemetry replies and periodic refreshes.
        if (pending_status_change_) {
            send_status_message();
            return;
        }
        if (pending_type_a_settings_change_ && last_recv_type_a_settings_[0] != 0) {
            send_type_a_settings_message();
            return;
        }
        if (pending_type_b_settings_change_) {
            if (last_recv_type_b_settings_[0] != 0) {
                send_type_b_settings_message(/* query = */ false);
                return;
            }
            // A user change before the first CB may request the missing shadow, but it shares the
            // configured startup-attempt budget and can never create an unbounded query loop.
            if (protocol::can_start_bounded_type_b_query(
                    slave_, pending_type_b_query_, awaiting_type_b_response_,
                    initial_type_b_request_count_, initial_type_b_retry_count_)) {
                pending_type_b_query_ = true;
                ++initial_type_b_request_count_;
            }
        }
        if (pending_type_c_response_) {
            send_type_c_status_message();
            return;
        }
        if (pending_type_b_query_ && !slave_) {
            bool allow_empty = last_recv_type_b_settings_[0] == 0;
            send_type_b_settings_message(/* query = */ true, allow_empty);
            return;
        }

        if (!slave_ && type_b_refresh_interval_ms_ > 0 && !awaiting_type_b_response_ &&
            now - last_sent_recv_type_b_millis_ > type_b_refresh_interval_ms_) {
            pending_type_b_query_ = true;
            return;
        }

        // Master status heartbeat. Slave controllers transmit only explicit local changes.
        if (!slave_ && now - last_sent_status_millis_ > 20000) {
            send_status_message();
        }
    }
};

} // namespace esphome::lg_controller
