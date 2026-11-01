#pragma once

// Wire Format v1 framing primitives. See protocol/SPEC.md.

#include <cstddef>
#include <cstdint>

namespace handview::protocol {

inline constexpr std::uint16_t kProtocolVersion = 1;

// Every message: [type: u16][length: u32], little-endian, length = payload bytes.
inline constexpr std::size_t kHeaderSize = 6;
inline constexpr std::uint32_t kMaxControlPayload = 64u * 1024u;
inline constexpr std::uint32_t kMaxVideoPayload = 32u * 1024u * 1024u;

enum class Channel : std::uint8_t { Control, Video, Link };

struct Header {
    std::uint16_t type = 0;
    std::uint32_t length = 0;
};

enum class HeaderStatus : std::uint8_t {
    Ok,
    NeedMore,  // fewer than kHeaderSize bytes available
    TooLarge,  // length over the channel limit: close with Bye{ProtocolError}
};

std::uint32_t max_payload(Channel channel, std::uint16_t type = 0);

void encode_header(const Header& header, std::uint8_t* out);
HeaderStatus decode_header(const std::uint8_t* data, std::size_t size, Channel channel, Header& out);

// Field-by-field little-endian access. Fields are not aligned, so never cast
// a pointer into the buffer to a struct.
void write_u16(std::uint8_t* out, std::uint16_t value);
void write_u32(std::uint8_t* out, std::uint32_t value);
void write_u64(std::uint8_t* out, std::uint64_t value);
std::uint16_t read_u16(const std::uint8_t* in);
std::uint32_t read_u32(const std::uint8_t* in);
std::uint64_t read_u64(const std::uint8_t* in);

// Serial number comparison for u32 seq: a is newer than or equal to b when
// (int32)(a - b) >= 0, which stays correct across wraparound.
constexpr bool seq_newer_or_equal(std::uint32_t a, std::uint32_t b) {
    return static_cast<std::int32_t>(a - b) >= 0;
}

}  // namespace handview::protocol
