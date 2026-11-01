// Minimal self-contained test runner: no external test framework dependency.

#include "handview/protocol/wire.hpp"
#include "test_support.hpp"

#include <cstdio>
#include <cstring>

namespace {

using namespace handview::protocol;

void test_header_round_trip() {
    std::uint8_t buf[kHeaderSize];
    encode_header({0x0100, 0x00012345}, buf);
    const std::uint8_t expected[kHeaderSize] = {0x00, 0x01, 0x45, 0x23, 0x01, 0x00};
    CHECK(std::memcmp(buf, expected, kHeaderSize) == 0);

    Header out;
    CHECK(decode_header(buf, sizeof buf, Channel::Video, out) == HeaderStatus::Ok);
    CHECK(out.type == 0x0100);
    CHECK(out.length == 0x00012345);
}

void test_header_need_more() {
    std::uint8_t buf[kHeaderSize] = {};
    Header out;
    CHECK(decode_header(buf, kHeaderSize - 1, Channel::Control, out) == HeaderStatus::NeedMore);
}

void test_header_length_limits() {
    std::uint8_t buf[kHeaderSize];
    Header out;

    encode_header({0x0010, kMaxControlPayload}, buf);
    CHECK(decode_header(buf, sizeof buf, Channel::Control, out) == HeaderStatus::Ok);
    encode_header({0x0010, kMaxControlPayload + 1}, buf);
    CHECK(decode_header(buf, sizeof buf, Channel::Control, out) == HeaderStatus::TooLarge);

    encode_header({0x0100, kMaxVideoPayload}, buf);
    CHECK(decode_header(buf, sizeof buf, Channel::Video, out) == HeaderStatus::Ok);
    encode_header({0x0100, kMaxVideoPayload + 1}, buf);
    CHECK(decode_header(buf, sizeof buf, Channel::Video, out) == HeaderStatus::TooLarge);
}

void test_little_endian_fields() {
    std::uint8_t buf[8];
    write_u64(buf, 0x0102030405060708ull);
    CHECK(buf[0] == 0x08 && buf[7] == 0x01);
    CHECK(read_u64(buf) == 0x0102030405060708ull);

    // Unaligned access must work.
    std::uint8_t odd[5] = {0xFF, 0x78, 0x56, 0x34, 0x12};
    CHECK(read_u32(odd + 1) == 0x12345678u);
}

void test_seq_serial_compare() {
    CHECK(seq_newer_or_equal(5, 5));
    CHECK(seq_newer_or_equal(6, 5));
    CHECK(!seq_newer_or_equal(5, 6));
    // Wraparound: 1 comes after 0xFFFFFFFF.
    CHECK(seq_newer_or_equal(1, 0xFFFFFFFFu));
    CHECK(!seq_newer_or_equal(0xFFFFFFFFu, 1));
    CHECK(seq_newer_or_equal(0x7FFFFFFFu, 0));
    CHECK(!seq_newer_or_equal(0x80000000u, 0));
}

}  // namespace

int main() {
    test_header_round_trip();
    test_header_need_more();
    test_header_length_limits();
    test_little_endian_fields();
    test_seq_serial_compare();

    return test_result("wire");
}
