#include "esphome/components/lg_controller/lg-protocol.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace protocol = esphome::lg_controller::protocol;

namespace {

#define CHECK(expression)                                                                            \
    do {                                                                                             \
        if (!(expression)) {                                                                         \
            std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: " #expression << '\n';    \
            std::exit(1);                                                                            \
        }                                                                                            \
    } while (false)

protocol::Frame make_frame(uint8_t header, uint8_t seed) {
    protocol::Frame frame{};
    frame[0] = header;
    for (std::size_t i = 1; i < protocol::kChecksumIndex; ++i) {
        frame[i] = static_cast<uint8_t>(seed + i);
    }
    protocol::update_checksum(frame);
    return frame;
}

std::vector<protocol::Frame> parse(const std::vector<uint8_t>& bytes,
                                   protocol::FrameParser* parser = nullptr) {
    protocol::FrameParser local_parser;
    protocol::FrameParser& selected = parser == nullptr ? local_parser : *parser;
    std::vector<protocol::Frame> result;
    for (uint8_t byte : bytes) {
        protocol::Frame frame{};
        if (selected.push(byte, frame)) {
            result.push_back(frame);
        }
    }
    return result;
}

void append(std::vector<uint8_t>& bytes, const protocol::Frame& frame) {
    bytes.insert(bytes.end(), frame.begin(), frame.end());
}

void test_checksum_and_header_validation() {
    const protocol::Frame frame = make_frame(0xC8, 0x10);
    CHECK(protocol::has_valid_header(0x28));
    CHECK(protocol::has_valid_header(0xAF));
    CHECK(protocol::has_valid_header(0xCC));
    CHECK(!protocol::has_valid_header(0x00));
    CHECK(!protocol::has_valid_header(0x20));
    CHECK(!protocol::has_valid_header(0xB0));
    CHECK(!protocol::has_valid_header(0xD0));
    CHECK(protocol::message_source(0x2B) == protocol::MessageSource::Slave);
    CHECK(protocol::message_source(0xAC) == protocol::MessageSource::Master);
    CHECK(protocol::message_source(0xCF) == protocol::MessageSource::Unit);
    CHECK(protocol::message_type(0xCC) == 4);
    CHECK(protocol::is_valid_frame(frame));

    protocol::Frame corrupt = frame;
    corrupt[4] ^= 0x01;
    CHECK(!protocol::has_valid_checksum(corrupt));
    CHECK(!protocol::is_valid_frame(corrupt));

    protocol::Frame invalid_header = frame;
    invalid_header[0] = 0xD0;
    protocol::update_checksum(invalid_header);
    CHECK(protocol::has_valid_checksum(invalid_header));
    CHECK(!protocol::is_valid_frame(invalid_header));
}

void test_capability_masks() {
    protocol::Frame capabilities{};
    capabilities[0] = 0xC9;
    capabilities[1] = 0x12;  // Duct + zone-state support.
    capabilities[2] = 0xE0;  // Heat, fan and dry.
    capabilities[3] = 0x04;  // High fan.
    capabilities[8] = 0x08;  // Supports zones 5-8.

    CHECK(protocol::supports_heating(capabilities.data()));
    CHECK(protocol::supports_fan_mode(capabilities.data()));
    CHECK(protocol::supports_dry_mode(capabilities.data()));
    CHECK(protocol::supports_high_fan(capabilities.data()));
    CHECK(protocol::maximum_zone_count(capabilities.data()) == 8);

    capabilities[8] = 0x10;  // Emergency-heater capability, not the 8-zone bit.
    CHECK(protocol::maximum_zone_count(capabilities.data()) == 4);
    capabilities[1] = 0x14;  // Wall unit + zone-state bit must not enable zone writes.
    CHECK(protocol::maximum_zone_count(capabilities.data()) == 0);
}

void test_transaction_state_helpers() {
    using protocol::TransactionKind;
    CHECK(protocol::transaction_active(TransactionKind::Status, TransactionKind::Status,
                                       TransactionKind::None));
    CHECK(protocol::transaction_active(TransactionKind::Status, TransactionKind::None,
                                       TransactionKind::Status));
    CHECK(!protocol::transaction_active(TransactionKind::Status, TransactionKind::TypeA,
                                        TransactionKind::TypeBQuery));

    CHECK(!protocol::type_b_response_timed_out(false, 20000U, 0U, 10000U));
    CHECK(!protocol::type_b_response_timed_out(true, 10000U, 0U, 10000U));
    CHECK(protocol::type_b_response_timed_out(true, 10001U, 0U, 10000U));
    // Unsigned subtraction keeps timeout handling correct across the millis() rollover.
    CHECK(protocol::type_b_response_timed_out(true, 5U, UINT32_MAX - 10000U, 10000U));

    CHECK(protocol::can_start_bounded_type_b_query(false, false, false, 0, 2));
    CHECK(!protocol::can_start_bounded_type_b_query(true, false, false, 0, 2));
    CHECK(!protocol::can_start_bounded_type_b_query(false, true, false, 0, 2));
    CHECK(!protocol::can_start_bounded_type_b_query(false, false, true, 0, 2));
    CHECK(!protocol::can_start_bounded_type_b_query(false, false, false, 2, 2));
    CHECK(!protocol::can_start_bounded_type_b_query(false, false, false, 0, 0));

    uint8_t attempts = 0;
    bool pending = false;
    bool awaiting = false;
    for (int cycle = 0; cycle < 5; ++cycle) {
        if (protocol::can_start_bounded_type_b_query(false, pending, awaiting, attempts, 2)) {
            pending = true;
            ++attempts;
        }
        if (pending) {
            pending = false;
            awaiting = true;
        }
        CHECK(!protocol::can_start_bounded_type_b_query(false, pending, awaiting, attempts, 2));
        awaiting = false;  // Model a response timeout before the next scheduler pass.
    }
    CHECK(attempts == 2);
    CHECK(!protocol::can_start_bounded_type_b_query(false, pending, awaiting, attempts, 2));
}

void test_parser_clean_and_padding() {
    const protocol::Frame first = make_frame(0xC8, 0x20);
    const protocol::Frame second = make_frame(0xA9, 0x40);
    std::vector<uint8_t> bytes(19, 0x00);
    append(bytes, first);
    bytes.insert(bytes.end(), 13, 0x00);
    append(bytes, second);

    protocol::FrameParser parser;
    const std::vector<protocol::Frame> frames = parse(bytes, &parser);
    CHECK(frames.size() == 2);
    CHECK(frames[0] == first);
    CHECK(frames[1] == second);
    CHECK(parser.buffered_size() == 0);
    CHECK(parser.frames_emitted() == 2);
    CHECK(parser.discarded_bytes() == 32);
}

void test_parser_inserted_byte_resync() {
    const protocol::Frame damaged = make_frame(0xCA, 0x11);
    const protocol::Frame expected = make_frame(0xCB, 0x55);
    std::vector<uint8_t> bytes;
    bytes.insert(bytes.end(), damaged.begin(), damaged.begin() + 6);
    bytes.push_back(0x7E);
    bytes.insert(bytes.end(), damaged.begin() + 6, damaged.end());
    append(bytes, expected);

    const std::vector<protocol::Frame> frames = parse(bytes);
    CHECK(frames.size() == 1);
    CHECK(frames[0] == expected);
}

void test_parser_dropped_byte_resync() {
    const protocol::Frame damaged = make_frame(0xA8, 0x13);
    const protocol::Frame expected = make_frame(0xC9, 0x33);
    std::vector<uint8_t> bytes;
    bytes.insert(bytes.end(), damaged.begin(), damaged.begin() + 7);
    bytes.insert(bytes.end(), damaged.begin() + 8, damaged.end());
    append(bytes, expected);

    const std::vector<protocol::Frame> frames = parse(bytes);
    CHECK(frames.size() == 1);
    CHECK(frames[0] == expected);
}

void test_parser_noise_that_looks_like_a_header() {
    const protocol::Frame expected = make_frame(0x2C, 0x61);
    std::vector<uint8_t> bytes{0xA8, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
                               0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
    append(bytes, expected);

    const std::vector<protocol::Frame> frames = parse(bytes);
    CHECK(frames.size() == 1);
    CHECK(frames[0] == expected);
}

void test_type4_energy_bcd() {
    protocol::Frame frame = make_frame(0xCC, 0x00);
    frame[3] = 0x64;
    frame[4] = 0x91;
    frame[5] = 0x29;
    protocol::update_checksum(frame);

    uint32_t tenths = 0;
    CHECK(protocol::decode_type4_energy_tenths(frame, tenths));
    CHECK(tenths == 649129U);
    CHECK(std::fabs(protocol::type4_energy_kwh(tenths) - 64912.9) < 0.0001);

    CHECK(protocol::encode_type4_energy_tenths(12345U, frame));
    CHECK(frame[3] == 0x01);
    CHECK(frame[4] == 0x23);
    CHECK(frame[5] == 0x45);
    protocol::update_checksum(frame);
    CHECK(protocol::is_valid_frame(frame));
    CHECK(protocol::decode_type4_energy_tenths(frame, tenths));
    CHECK(tenths == 12345U);

    const protocol::Frame unchanged = frame;
    CHECK(!protocol::encode_type4_energy_tenths(1000000U, frame));
    CHECK(frame == unchanged);

    frame[4] = 0xFA;
    CHECK(!protocol::decode_type4_energy_tenths(frame, tenths));

    protocol::Frame wrong_type = make_frame(0xCB, 0x00);
    CHECK(!protocol::decode_type4_energy_tenths(wrong_type, tenths));
    CHECK(!protocol::encode_type4_energy_tenths(10U, wrong_type));
}

void test_eight_zone_encode_decode() {
    protocol::Frame frame = make_frame(0xC8, 0x00);
    frame[5] = 0x87;
    frame[10] = 0xE1;
    const protocol::ZoneStates desired{{true, false, true, false, true, false, true, false}};
    CHECK(protocol::encode_status_zones(frame, 8, desired));
    CHECK(frame[5] == 0xD7);
    CHECK(frame[10] == 0xF5);

    protocol::ZoneStates decoded{};
    CHECK(protocol::decode_status_zones(frame, 8, decoded));
    CHECK(decoded == desired);
}

void test_four_zone_preserves_overlapping_byte10() {
    protocol::Frame frame = make_frame(0xA8, 0x00);
    frame[5] = 0xFF;
    frame[10] = 0x1C;
    const protocol::ZoneStates desired{{false, true, false, true, true, true, true, true}};
    CHECK(protocol::encode_status_zones(frame, 4, desired));
    CHECK(frame[5] == 0xAF);
    CHECK(frame[10] == 0x1C);

    protocol::ZoneStates decoded{};
    CHECK(protocol::decode_status_zones(frame, 4, decoded));
    CHECK(!decoded[0]);
    CHECK(decoded[1]);
    CHECK(!decoded[2]);
    CHECK(decoded[3]);
    CHECK(!decoded[4]);
    CHECK(!decoded[7]);

    const protocol::Frame unchanged = frame;
    CHECK(!protocol::encode_status_zones(frame, 0, desired));
    CHECK(frame == unchanged);
    CHECK(!protocol::encode_status_zones(frame, 9, desired));
    CHECK(frame == unchanged);
}

void test_partial_zone_control_preserves_unowned_bits() {
    protocol::Frame frame = make_frame(0xA8, 0x00);
    frame[5] = 0x7F;
    frame[10] = 0x1F;
    const protocol::ZoneStates desired{{false, false, false, false, false, false, false, false}};
    const protocol::ZoneStates controlled{{true, false, false, false, false, false, true, false}};

    CHECK(protocol::encode_status_zones(frame, 8, desired, controlled));
    CHECK(frame[5] == 0x3F);   // Zone 1 changed; zones 2-4 and unrelated bits are untouched.
    CHECK(frame[10] == 0x1B);  // Zone 7 changed; zones 5, 6, 8 and unrelated bits are untouched.
}

void test_type3_zone_count() {
    protocol::Frame frame = make_frame(0xCB, 0x00);
    frame[10] = 0x0B;
    CHECK(protocol::encode_type3_zone_count(frame, 6));
    CHECK(frame[10] == 0x6B);

    uint8_t count = 0;
    CHECK(protocol::decode_type3_zone_count(frame, count));
    CHECK(count == 6);
    CHECK(!protocol::encode_type3_zone_count(frame, 9));

    protocol::Frame wrong_type = make_frame(0xC8, 0x00);
    CHECK(!protocol::decode_type3_zone_count(wrong_type, count));
    CHECK(!protocol::encode_type3_zone_count(wrong_type, 4));
}

}  // namespace

int main() {
    test_checksum_and_header_validation();
    test_capability_masks();
    test_transaction_state_helpers();
    test_parser_clean_and_padding();
    test_parser_inserted_byte_resync();
    test_parser_dropped_byte_resync();
    test_parser_noise_that_looks_like_a_header();
    test_type4_energy_bcd();
    test_eight_zone_encode_decode();
    test_four_zone_preserves_overlapping_byte10();
    test_partial_zone_control_preserves_unowned_bits();
    test_type3_zone_count();
    std::cout << "lg_protocol_test: all checks passed\n";
    return 0;
}
