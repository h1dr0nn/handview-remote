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
// The SPS with VUI bitstream_restriction saying no frame is reordered
// (max_num_reorder_frames 0), so decoders output each frame as soon as it is
// decoded instead of filling a reorder buffer first. Empty if unparsable.
std::optional<std::vector<uint8_t>> sps_without_reordering(NalUnit sps);
} // namespace handview::encode
