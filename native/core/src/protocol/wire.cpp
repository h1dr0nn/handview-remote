#include "handview/protocol/wire.hpp"

namespace handview::protocol {

std::uint32_t max_payload(Channel channel) {
    return channel == Channel::Video ? kMaxVideoPayload : kMaxControlPayload;
}

void write_u16(std::uint8_t* out, std::uint16_t value) {
    out[0] = static_cast<std::uint8_t>(value);
    out[1] = static_cast<std::uint8_t>(value >> 8);
}

void write_u32(std::uint8_t* out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

void write_u64(std::uint8_t* out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

std::uint16_t read_u16(const std::uint8_t* in) {
    return static_cast<std::uint16_t>(in[0] | (in[1] << 8));
}

std::uint32_t read_u32(const std::uint8_t* in) {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(in[i]) << (8 * i);
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* in) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(in[i]) << (8 * i);
    }
    return value;
}

void encode_header(const Header& header, std::uint8_t* out) {
    write_u16(out, header.type);
    write_u32(out + 2, header.length);
}

HeaderStatus decode_header(const std::uint8_t* data, std::size_t size, Channel channel, Header& out) {
    if (size < kHeaderSize) {
        return HeaderStatus::NeedMore;
    }
    Header header;
    header.type = read_u16(data);
    header.length = read_u32(data + 2);
    if (header.length > max_payload(channel)) {
        return HeaderStatus::TooLarge;
    }
    out = header;
    return HeaderStatus::Ok;
}

}  // namespace handview::protocol
