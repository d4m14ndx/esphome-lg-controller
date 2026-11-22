#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome::lg_controller::protocol {

constexpr std::size_t kFrameSize = 13;
constexpr std::size_t kChecksumIndex = kFrameSize - 1;

using Frame = std::array<uint8_t, kFrameSize>;
using ZoneStates = std::array<bool, 8>;

enum class MessageSource : uint8_t {
    Slave,
    Master,
    Unit,
    Invalid,
};

enum class TransactionKind : uint8_t {
    None,
    Status,
    TypeA,
    TypeBQuery,
    TypeBWrite,
    TypeC,
    TypeD,
    TypeE,
};

constexpr bool transaction_active(TransactionKind expected, TransactionKind pending,
                                  TransactionKind queued_retry) {
    return pending == expected || queued_retry == expected;
}

constexpr bool type_b_response_timed_out(bool awaiting_response, uint32_t now,
                                         uint32_t request_millis, uint32_t timeout_millis) {
    return awaiting_response && now - request_millis > timeout_millis;
}

constexpr bool can_start_bounded_type_b_query(bool slave, bool query_pending,
                                              bool awaiting_response, uint8_t attempts,
                                              uint8_t maximum_attempts) {
    return !slave && !query_pending && !awaiting_response && attempts < maximum_attempts;
}

constexpr MessageSource message_source(uint8_t header) {
    switch (header & 0xF8U) {
        case 0x28:
            return MessageSource::Slave;
        case 0xA8:
            return MessageSource::Master;
        case 0xC8:
            return MessageSource::Unit;
        default:
            return MessageSource::Invalid;
    }
}

constexpr uint8_t message_type(uint8_t header) {
    return header & 0x07U;
}

constexpr bool has_valid_header(uint8_t header) {
    return message_source(header) != MessageSource::Invalid;
}

// High-confidence Type-1 capability bits used by the controller. Keeping these in the pure
// protocol layer makes the byte/mask mapping host-testable.
constexpr bool supports_heating(const uint8_t* capabilities) {
    return (capabilities[2] & 0x20U) != 0U;
}

constexpr bool supports_fan_mode(const uint8_t* capabilities) {
    return (capabilities[2] & 0x40U) != 0U;
}

constexpr bool supports_dry_mode(const uint8_t* capabilities) {
    return (capabilities[2] & 0x80U) != 0U;
}

constexpr bool supports_high_fan(const uint8_t* capabilities) {
    return (capabilities[3] & 0x04U) != 0U;
}

constexpr bool is_duct_unit(const uint8_t* capabilities) {
    return (capabilities[1] & 0x07U) == 2U;
}

constexpr bool supports_zone_state(const uint8_t* capabilities) {
    return (capabilities[1] & 0x10U) != 0U;
}

constexpr bool supports_five_to_eight_zones(const uint8_t* capabilities) {
    return (capabilities[8] & 0x08U) != 0U;
}

constexpr uint8_t maximum_zone_count(const uint8_t* capabilities) {
    return is_duct_unit(capabilities) && supports_zone_state(capabilities)
               ? (supports_five_to_eight_zones(capabilities) ? 8U : 4U)
               : 0U;
}

inline uint8_t calculate_checksum(const uint8_t* data) {
    uint8_t sum = 0;
    for (std::size_t i = 0; i < kChecksumIndex; ++i) {
        sum = static_cast<uint8_t>(sum + data[i]);
    }
    return static_cast<uint8_t>(sum ^ 0x55U);
}

inline uint8_t calculate_checksum(const Frame& frame) {
    return calculate_checksum(frame.data());
}

inline void update_checksum(Frame& frame) {
    frame[kChecksumIndex] = calculate_checksum(frame);
}

inline bool has_valid_checksum(const uint8_t* data) {
    return calculate_checksum(data) == data[kChecksumIndex];
}

inline bool has_valid_checksum(const Frame& frame) {
    return has_valid_checksum(frame.data());
}

inline bool is_valid_frame(const uint8_t* data) {
    return has_valid_header(data[0]) && has_valid_checksum(data);
}

inline bool is_valid_frame(const Frame& frame) {
    return is_valid_frame(frame.data());
}

// Incremental parser for the fixed-size LG frames. Invalid candidates are
// discarded one byte at a time so a dropped or inserted byte cannot keep the
// stream permanently misaligned. Header validation also skips zero padding and
// unrelated noise without waiting for another complete candidate frame.
class FrameParser {
public:
    bool push(uint8_t byte, Frame& output) {
        buffer_[buffered_++] = byte;
        discard_until_header();

        if (buffered_ < kFrameSize) {
            return false;
        }

        if (is_valid_frame(buffer_.data())) {
            std::copy(buffer_.begin(), buffer_.end(), output.begin());
            buffered_ = 0;
            ++frames_emitted_;
            return true;
        }

        ++invalid_candidates_;
        discard_front();
        discard_until_header();
        return false;
    }

    void reset() {
        buffered_ = 0;
    }

    std::size_t buffered_size() const {
        return buffered_;
    }

    std::size_t discarded_bytes() const {
        return discarded_bytes_;
    }

    std::size_t frames_emitted() const {
        return frames_emitted_;
    }

    std::size_t invalid_candidates() const {
        return invalid_candidates_;
    }

private:
    void discard_front() {
        if (buffered_ == 0) {
            return;
        }
        std::move(buffer_.begin() + 1, buffer_.begin() + buffered_, buffer_.begin());
        --buffered_;
        ++discarded_bytes_;
    }

    void discard_until_header() {
        while (buffered_ > 0 && !has_valid_header(buffer_[0])) {
            discard_front();
        }
    }

    Frame buffer_{};
    std::size_t buffered_ = 0;
    std::size_t discarded_bytes_ = 0;
    std::size_t frames_emitted_ = 0;
    std::size_t invalid_candidates_ = 0;
};

constexpr bool is_packed_bcd_byte(uint8_t value) {
    return (value & 0x0FU) <= 9U && ((value >> 4U) & 0x0FU) <= 9U;
}

// Type-4 energy is three big-endian packed-BCD bytes in tenths of a kWh.
// For example 64 91 29 represents 649129 tenths, or 64912.9 kWh.
inline bool decode_type4_energy_tenths(const uint8_t* packed_bcd, uint32_t& tenths) {
    uint32_t result = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const uint8_t value = packed_bcd[i];
        if (!is_packed_bcd_byte(value)) {
            return false;
        }
        result = result * 100U + static_cast<uint32_t>((value >> 4U) * 10U + (value & 0x0FU));
    }
    tenths = result;
    return true;
}

inline bool decode_type4_energy_tenths(const Frame& frame, uint32_t& tenths) {
    if (message_type(frame[0]) != 4U) {
        return false;
    }
    return decode_type4_energy_tenths(frame.data() + 3, tenths);
}

inline bool encode_type4_energy_tenths(uint32_t tenths, uint8_t* packed_bcd) {
    if (tenths > 999999U) {
        return false;
    }

    for (std::size_t i = 0; i < 3; ++i) {
        const std::size_t index = 2U - i;
        const uint8_t pair = static_cast<uint8_t>(tenths % 100U);
        packed_bcd[index] = static_cast<uint8_t>(((pair / 10U) << 4U) | (pair % 10U));
        tenths /= 100U;
    }
    return true;
}

inline bool encode_type4_energy_tenths(uint32_t tenths, Frame& frame) {
    if (message_type(frame[0]) != 4U) {
        return false;
    }
    return encode_type4_energy_tenths(tenths, frame.data() + 3);
}

inline double type4_energy_kwh(uint32_t tenths) {
    return static_cast<double>(tenths) / 10.0;
}

constexpr std::size_t status_zone_byte(std::size_t zone_index) {
    return zone_index < 4U ? 5U : 10U;
}

constexpr uint8_t status_zone_mask(std::size_t zone_index) {
    constexpr std::array<uint8_t, 8> kMasks{{0x40, 0x20, 0x10, 0x08, 0x10, 0x08, 0x04, 0x02}};
    return zone_index < kMasks.size() ? kMasks[zone_index] : 0U;
}

// Zone count is explicit because Type-0 byte 10 overlaps auto-dry/robot-clean
// fields on units with four or fewer zones.
inline bool decode_status_zones(const Frame& frame, uint8_t zone_count, ZoneStates& zones) {
    if (message_type(frame[0]) != 0U || zone_count == 0U || zone_count > zones.size()) {
        return false;
    }

    zones.fill(false);
    for (std::size_t i = 0; i < zone_count; ++i) {
        zones[i] = (frame[status_zone_byte(i)] & status_zone_mask(i)) != 0U;
    }
    return true;
}

inline bool encode_status_zones(Frame& frame, uint8_t zone_count, const ZoneStates& zones,
                                const ZoneStates& controlled_zones) {
    if (message_type(frame[0]) != 0U || zone_count == 0U || zone_count > zones.size()) {
        return false;
    }

    for (std::size_t i = 0; i < zone_count; ++i) {
        if (!controlled_zones[i]) {
            continue;
        }
        frame[status_zone_byte(i)] &= static_cast<uint8_t>(~status_zone_mask(i));
        if (zones[i]) {
            frame[status_zone_byte(i)] |= status_zone_mask(i);
        }
    }
    return true;
}

inline bool encode_status_zones(Frame& frame, uint8_t zone_count, const ZoneStates& zones) {
    ZoneStates controlled_zones{};
    for (std::size_t i = 0; i < std::min<std::size_t>(zone_count, controlled_zones.size()); ++i) {
        controlled_zones[i] = true;
    }
    return encode_status_zones(frame, zone_count, zones, controlled_zones);
}

inline bool decode_type3_zone_count(const Frame& frame, uint8_t& zone_count) {
    if (message_type(frame[0]) != 3U) {
        return false;
    }
    zone_count = frame[10] >> 4U;
    return true;
}

inline bool encode_type3_zone_count(Frame& frame, uint8_t zone_count) {
    if (message_type(frame[0]) != 3U || zone_count > 8U) {
        return false;
    }
    frame[10] = static_cast<uint8_t>((frame[10] & 0x0FU) | (zone_count << 4U));
    return true;
}

// ---- Bit-field settings -------------------------------------------------------------------
//
// Most documented settings are a plain bit field in one byte of one message type. They are
// described once here so decoding, encoding and the entity wiring share a single layout.

// Message carrying a field. TypeE is only the installer-settings subtype (byte 1 == 0x10).
enum class FieldFrame : uint8_t { Status, TypeA, TypeB, TypeC, TypeD, TypeE };

constexpr uint8_t kTypeESettingsSubtype = 0x10;

constexpr uint8_t field_frame_type(FieldFrame frame) {
    switch (frame) {
        case FieldFrame::Status: return 0;
        case FieldFrame::TypeA: return 2;
        case FieldFrame::TypeB: return 3;
        case FieldFrame::TypeC: return 4;
        case FieldFrame::TypeD: return 5;
        case FieldFrame::TypeE: return 6;
    }
    return 0;
}

enum class Field : uint8_t {
    // Type 0 status.
    Humidifier,
    AuxiliaryHeater,
    Swirl,
    ExternalVentilation,
    FanAuto,
    ElevationGrill,
    EnergySaving,
    CeilingHeight,
    // Type 2 settings.
    UnitAddress,
    AutoChangeTemperature,
    AuxiliaryHeaterInstalled,
    MinSetpoint,
    MaxSetpoint,
    DryContactAuto,
    ZoneStateFixed,
    RobotCleanAuto,
    // Type 3 settings.
    Overcooling,
    HalfDegreeControl,
    EmergencyHeater,
    EmergencyHeaterFan,
    GroupControl,
    WifiAp,
    RefrigerantLeakDetector,
    ThermalOffFan,
    StaticPressure,
    ZoneNumber,
    LowAmbientHeating,
    // Type 4 status.
    DualSetpoint,
    HimalayaCooling,
    MosquitoAway,
    ComfortCooling,
    // Type 5 installer settings.
    AutoStart,
    VentilationFanInterlock,
    AuxiliaryHeaterDuctType,
    PrimaryHeaterControl,
    SimpleDryContact,
    // Type 6 subtype 0x10 installer settings.
    SilentMode,
    DefrostMode,
    FanContinuous,
    TemperatureFanAuto,
    CnExt,
    OutdoorUnitFunction,
    Count,
};

constexpr std::size_t kFieldCount = static_cast<std::size_t>(Field::Count);

struct FieldSpec {
    FieldFrame frame;
    uint8_t byte;
    uint8_t mask;
    uint8_t offset;           // Added to the raw value for numeric entities.
    uint8_t capability_byte;  // Type-1 byte announcing support; 0 when no bit is documented.
    uint8_t capability_mask;
    const char* name;
};

// Indexed by Field. Masks for the Type 5/6 fields cover only the values shown in protocol.md.
constexpr std::array<FieldSpec, kFieldCount> kFieldSpecs{{
    {FieldFrame::Status, 2, 0x08, 0, 2, 0x04, "humidifier"},
    {FieldFrame::Status, 2, 0x10, 0, 0, 0x00, "auxiliary heater"},
    {FieldFrame::Status, 2, 0x20, 0, 1, 0x20, "swirl"},
    {FieldFrame::Status, 3, 0x01, 0, 0, 0x00, "external ventilation"},
    {FieldFrame::Status, 3, 0x02, 0, 2, 0x01, "fan auto"},
    {FieldFrame::Status, 3, 0x60, 0, 0, 0x00, "elevation grill"},
    {FieldFrame::Status, 5, 0x02, 0, 5, 0x01, "energy saving"},
    {FieldFrame::Status, 6, 0xC0, 0, 4, 0x08, "ceiling height"},

    {FieldFrame::TypeA, 1, 0xFF, 0, 0, 0x00, "unit address"},
    {FieldFrame::TypeA, 9, 0x07, 0, 5, 0x04, "auto change temperature"},
    {FieldFrame::TypeA, 9, 0x80, 0, 7, 0x04, "auxiliary heater installed"},
    {FieldFrame::TypeA, 10, 0x0F, 15, 0, 0x00, "minimum setpoint"},
    {FieldFrame::TypeA, 10, 0xF0, 15, 0, 0x00, "maximum setpoint"},
    {FieldFrame::TypeA, 11, 0x01, 0, 0, 0x00, "dry contact auto"},
    {FieldFrame::TypeA, 11, 0x02, 0, 1, 0x10, "zone state fixed"},
    {FieldFrame::TypeA, 11, 0x04, 0, 4, 0x40, "robot clean auto"},

    {FieldFrame::TypeB, 2, 0xC0, 0, 9, 0x04, "over cooling"},
    {FieldFrame::TypeB, 6, 0x04, 0, 8, 0x02, "half degree control"},
    {FieldFrame::TypeB, 6, 0x08, 0, 8, 0x10, "emergency heater"},
    {FieldFrame::TypeB, 6, 0x40, 0, 8, 0x10, "emergency heater fan"},
    {FieldFrame::TypeB, 6, 0x80, 0, 8, 0x20, "group control"},
    {FieldFrame::TypeB, 7, 0x40, 0, 10, 0x04, "wifi access point"},
    {FieldFrame::TypeB, 9, 0x10, 0, 9, 0x80, "refrigerant leak detector"},
    {FieldFrame::TypeB, 9, 0xC0, 0, 10, 0x40, "thermal off fan"},
    {FieldFrame::TypeB, 10, 0x0F, 0, 10, 0x02, "static pressure step"},
    {FieldFrame::TypeB, 10, 0xF0, 0, 0, 0x00, "zone number"},
    {FieldFrame::TypeB, 11, 0x0F, 0, 8, 0x10, "low ambient heating"},

    {FieldFrame::TypeC, 2, 0x80, 0, 0, 0x00, "dual setpoint"},
    {FieldFrame::TypeC, 11, 0x01, 0, 11, 0x04, "himalaya cooling"},
    {FieldFrame::TypeC, 11, 0x02, 0, 11, 0x10, "mosquito away"},
    {FieldFrame::TypeC, 11, 0x04, 0, 11, 0x20, "comfort cooling"},

    {FieldFrame::TypeD, 1, 0x01, 0, 11, 0x01, "indoor unit auto start"},
    {FieldFrame::TypeD, 1, 0x02, 0, 11, 0x02, "fan interlocked with ventilation"},
    {FieldFrame::TypeD, 1, 0x04, 0, 7, 0x04, "auxiliary heater duct type"},
    {FieldFrame::TypeD, 1, 0x40, 0, 10, 0x80, "primary heater control"},
    {FieldFrame::TypeD, 6, 0x03, 0, 11, 0x80, "simple dry contact"},

    {FieldFrame::TypeE, 3, 0x03, 0, 0, 0x00, "silent mode"},
    {FieldFrame::TypeE, 3, 0x30, 0, 0, 0x00, "defrost mode"},
    {FieldFrame::TypeE, 4, 0x02, 0, 0, 0x00, "fan continuous"},
    {FieldFrame::TypeE, 4, 0x04, 0, 0, 0x00, "temperature based fan auto"},
    {FieldFrame::TypeE, 4, 0x70, 0, 0, 0x00, "CN_EXT"},
    {FieldFrame::TypeE, 5, 0x10, 0, 0, 0x00, "outdoor unit function"},
}};

constexpr const FieldSpec& field_spec(Field field) {
    return kFieldSpecs[static_cast<std::size_t>(field)];
}

constexpr uint8_t field_shift(uint8_t mask) {
    uint8_t shift = 0;
    while (mask != 0U && (mask & 1U) == 0U) {
        mask = static_cast<uint8_t>(mask >> 1U);
        ++shift;
    }
    return shift;
}

// Largest raw value the field can hold.
constexpr uint8_t field_max(const FieldSpec& spec) {
    return static_cast<uint8_t>(spec.mask >> field_shift(spec.mask));
}

constexpr uint8_t decode_field(const FieldSpec& spec, const uint8_t* frame) {
    return static_cast<uint8_t>((frame[spec.byte] & spec.mask) >> field_shift(spec.mask));
}

inline void encode_field(const FieldSpec& spec, uint8_t* frame, uint8_t raw) {
    frame[spec.byte] = static_cast<uint8_t>(
        (frame[spec.byte] & ~spec.mask) | ((raw << field_shift(spec.mask)) & spec.mask));
}

// True when the unit's Type-1 message announces the field, or when no bit is documented for it.
constexpr bool field_advertised(const FieldSpec& spec, const uint8_t* capabilities) {
    return spec.capability_byte == 0U ||
           (capabilities[spec.capability_byte] & spec.capability_mask) != 0U;
}

// ---- Fan speeds without an ESPHome enum ------------------------------------------------------

constexpr const char* extended_fan_name(uint8_t code) {
    return code == 5U ? "Low-Medium" : code == 6U ? "Medium-High" : code == 7U ? "Power" : nullptr;
}

constexpr bool supports_extended_fan(const uint8_t* capabilities, uint8_t code) {
    return code == 5U   ? (capabilities[6] & 0x08U) != 0U
           : code == 6U ? (capabilities[6] & 0x10U) != 0U
           : code == 7U ? (capabilities[3] & 0x82U) != 0U  // power, or power in heating mode
                        : false;
}

// Returns the Type-0 fan code (5-7) for a custom fan mode name, or 0 when it is not one.
inline uint8_t extended_fan_code(const char* name, std::size_t length) {
    for (uint8_t code = 5; code <= 7; ++code) {
        const char* candidate = extended_fan_name(code);
        std::size_t candidate_length = 0;
        while (candidate[candidate_length] != '\0') {
            ++candidate_length;
        }
        if (candidate_length == length && std::equal(name, name + length, candidate)) {
            return code;
        }
    }
    return 0;
}

// ---- Reservations (Type 0 bytes 8-9) ---------------------------------------------------------

enum class ReservationType : uint8_t {
    None = 0,
    TurnOn = 1,
    TurnOff = 2,
    Sleep = 3,
    ClearAll = 4,
    SimpleTimer = 5,
};

constexpr uint16_t kMaxReservationMinutes = 0x7FF;

constexpr ReservationType decode_reservation_type(const uint8_t* frame) {
    return static_cast<ReservationType>((frame[8] >> 3U) & 0x07U);
}

constexpr uint16_t decode_reservation_minutes(const uint8_t* frame) {
    return static_cast<uint16_t>((static_cast<uint16_t>(frame[8] & 0x07U) << 8U) | frame[9]);
}

// Replaces the type and minutes, keeping the two unrelated high bits of byte 8.
inline void encode_reservation(uint8_t* frame, ReservationType type, uint16_t minutes) {
    minutes = std::min(minutes, kMaxReservationMinutes);
    frame[8] = static_cast<uint8_t>((frame[8] & 0xC0U) | (static_cast<uint8_t>(type) << 3U) |
                                    ((minutes >> 8U) & 0x07U));
    frame[9] = static_cast<uint8_t>(minutes & 0xFFU);
}

// ---- Type 4 dual setpoints -------------------------------------------------------------------

// Setpoints are an integer byte plus a tenths nibble in byte 8; values are tenths of a degree.
constexpr uint16_t decode_type4_setpoint_tenths(const uint8_t* frame, bool high) {
    return high ? static_cast<uint16_t>((frame[6] & 0x7FU) * 10U + (frame[8] >> 4U))
                : static_cast<uint16_t>((frame[7] & 0x7FU) * 10U + (frame[8] & 0x0FU));
}

inline void encode_type4_setpoint_tenths(uint8_t* frame, bool high, uint16_t tenths) {
    const uint8_t whole = static_cast<uint8_t>(std::min<uint16_t>(tenths / 10U, 0x7FU));
    const uint8_t fraction = static_cast<uint8_t>(tenths % 10U);
    if (high) {
        frame[6] = static_cast<uint8_t>((frame[6] & 0x80U) | whole);
        frame[8] = static_cast<uint8_t>((frame[8] & 0x0FU) | (fraction << 4U));
    } else {
        frame[7] = static_cast<uint8_t>((frame[7] & 0x80U) | whole);
        frame[8] = static_cast<uint8_t>((frame[8] & 0xF0U) | fraction);
    }
}

// The deadband is one packed-BCD byte in tenths of a degree (0x25 is 2.5).
inline bool decode_type4_deadband_tenths(const uint8_t* frame, uint8_t& tenths) {
    if (!is_packed_bcd_byte(frame[10])) {
        return false;
    }
    tenths = static_cast<uint8_t>((frame[10] >> 4U) * 10U + (frame[10] & 0x0FU));
    return true;
}

inline void encode_type4_deadband_tenths(uint8_t* frame, uint8_t tenths) {
    tenths = std::min<uint8_t>(tenths, 99U);
    frame[10] = static_cast<uint8_t>(((tenths / 10U) << 4U) | (tenths % 10U));
}

// ---- Type 7 power ----------------------------------------------------------------------------

// The PREMTB100 shows bytes 2-4 as six decimal positions from hundreds of kW down to watts. It
// does not reject nibbles above nine, so AB CD EF reads as 1123.455 kW. Returns watts.
constexpr uint32_t decode_type7_power_watts(const uint8_t* frame) {
    uint32_t watts = 0;
    for (std::size_t i = 2; i <= 4; ++i) {
        watts = watts * 10U + (frame[i] >> 4U);
        watts = watts * 10U + (frame[i] & 0x0FU);
    }
    return watts;
}

// ---- Type 1 capability names -----------------------------------------------------------------

struct CapabilityFlag {
    uint8_t byte;
    uint8_t mask;
    const char* name;
};

constexpr CapabilityFlag kCapabilityFlags[] = {
    {1, 0x10, "zone state setting"},
    {1, 0x20, "swirl"},
    {1, 0x40, "horizontal swing"},
    {1, 0x80, "vertical swing"},
    {2, 0x01, "fan auto"},
    {2, 0x02, "plasma (air purifier)"},
    {2, 0x04, "humidifier"},
    {2, 0x08, "mode auto"},
    {2, 0x10, "mode AI"},
    {2, 0x20, "mode heating"},
    {2, 0x40, "mode fan"},
    {2, 0x80, "mode dehumidify"},
    {3, 0x01, "fan speed auto"},
    {3, 0x02, "fan speed power"},
    {3, 0x04, "fan speed high"},
    {3, 0x08, "fan speed medium"},
    {3, 0x10, "fan speed low"},
    {3, 0x20, "fan speed slow"},
    {3, 0x80, "fan speed power in heating mode"},
    {4, 0x01, "vertical vane control"},
    {4, 0x02, "ESP value setting"},
    {4, 0x04, "static pressure setting"},
    {4, 0x08, "ceiling height setting"},
    {4, 0x40, "robot clean"},
    {4, 0x80, "auto clean (auto dry)"},
    {5, 0x01, "energy saving"},
    {5, 0x02, "override master/slave setting"},
    {5, 0x04, "auto change temperature"},
    {5, 0x20, "whole degrees only"},
    {5, 0x40, "single vane"},
    {5, 0x80, "two vanes"},
    {6, 0x01, "extra airflow option"},
    {6, 0x08, "fan speed low-medium"},
    {6, 0x10, "fan speed medium-high"},
    {6, 0x20, "minimum cooling setpoint 16"},
    {7, 0x04, "auxiliary heater"},
    {7, 0x80, "over heating setting"},
    {8, 0x01, "pipe temperature setting"},
    {8, 0x02, "centigrade setting (0.5C or 1C)"},
    {8, 0x08, "5-8 zones"},
    {8, 0x10, "emergency heater"},
    {8, 0x20, "group control"},
    {8, 0x40, "unit information"},
    {8, 0x80, "clear filter timer"},
    {9, 0x02, "indoor unit address check"},
    {9, 0x04, "over cooling setting"},
    {9, 0x10, "energy usage"},
    {9, 0x80, "refrigerant leak detector"},
    {10, 0x01, "DRED"},
    {10, 0x02, "static pressure step"},
    {10, 0x04, "indoor unit wifi access point"},
    {10, 0x40, "fan in cooling thermal off setting"},
    {10, 0x80, "primary heater control"},
    {11, 0x01, "indoor unit auto start"},
    {11, 0x02, "fan interlocked with ventilation"},
    {11, 0x04, "himalaya cool"},
    {11, 0x08, "monsoon comfort"},
    {11, 0x10, "mosquito away"},
    {11, 0x20, "comfort cooling (hum+e)"},
    {11, 0x80, "simple dry contact"},
};

}  // namespace esphome::lg_controller::protocol
