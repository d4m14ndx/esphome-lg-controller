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

}  // namespace esphome::lg_controller::protocol
