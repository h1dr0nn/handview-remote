#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace handview::encode {
using NalUnit = std::span<const uint8_t>;
std::vector<NalUnit> split_annex_b(std::span<const uint8_t> data);
uint8_t nal_type(NalUnit nal);
bool to_annex_b(std::span<const uint8_t> data, std::vector<uint8_t> &out, uint8_t length_bytes = 4);
struct SpsInfo {
    uint8_t profile_idc, level_idc;
    uint32_t width, height;
};
std::optional<SpsInfo> parse_sps(NalUnit sps);
} // namespace handview::encode
