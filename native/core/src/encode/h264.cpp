#include "handview/encode/h264.hpp"
#include <limits>
namespace handview::encode {
namespace {
size_t prefix(std::span<const uint8_t> d, size_t i) {
    if (i + 3 <= d.size() && d[i] == 0 && d[i + 1] == 0) {
        if (d[i + 2] == 1)
            return 3;
        if (i + 4 <= d.size() && d[i + 2] == 0 && d[i + 3] == 1)
            return 4;
    }
    return 0;
}
struct Bits {
    std::vector<uint8_t> d;
    size_t pos = 0;
    bool ok = true;
    uint32_t read(unsigned n) {
        if (n > 32 || pos + n > d.size() * 8) {
            ok = false;
            return 0;
        }
        uint32_t v = 0;
        while (n--) {
            v = (v << 1) | ((d[pos / 8] >> (7 - pos % 8)) & 1);
            ++pos;
        }
        return v;
    }
    uint32_t ue() {
        unsigned z = 0;
        while (ok && read(1) == 0) {
            if (++z > 31) {
                ok = false;
                return 0;
            }
        }
        return ((uint32_t{1} << z) - 1) + read(z);
    }
    int64_t se() {
        auto v = ue();
        return v & 1 ? (int64_t(v) + 1) / 2 : -int64_t(v) / 2;
    }
};
struct Writer {
    std::vector<uint8_t> d;
    size_t pos = 0;
    void write(uint32_t v, unsigned n) {
        while (n--) {
            if (pos % 8 == 0)
                d.push_back(0);
            d.back() |= uint8_t(((v >> n) & 1) << (7 - pos % 8));
            ++pos;
        }
    }
    void ue(uint32_t v) {
        const uint64_t x = uint64_t(v) + 1;
        unsigned width = 0;
        for (auto t = x; t; t >>= 1)
            ++width;
        write(0, width - 1);
        for (unsigned i = width; i--;)
            write(uint32_t((x >> i) & 1), 1);
    }
};
// Copies syntax elements from an RBSP reader to a writer unchanged.
struct Copier {
    Bits &in;
    Writer &out;
    uint32_t u(unsigned n) {
        auto v = in.read(n);
        out.write(v, n);
        return v;
    }
    uint32_t ue() {
        auto v = in.ue();
        out.ue(v);
        return v;
    }
    void se() {
        ue();
    }
    void hrd() {
        auto count = ue();
        if (count > 31) {
            in.ok = false;
            return;
        }
        u(4);
        u(4);
        for (uint32_t i = 0; i <= count; ++i) {
            ue();
            ue();
            u(1);
        }
        u(5);
        u(5);
        u(5);
        u(5);
    }
};
Bits unescape(NalUnit nal) {
    Bits b;
    unsigned zeros = 0;
    for (size_t i = 1; i < nal.size(); ++i) {
        if (zeros >= 2 && nal[i] == 3) {
            zeros = 0;
            continue;
        }
        b.d.push_back(nal[i]);
        zeros = nal[i] == 0 ? zeros + 1 : 0;
    }
    return b;
}
} // namespace
std::vector<NalUnit> split_annex_b(std::span<const uint8_t> d) {
    std::vector<NalUnit> result;
    size_t start = d.size();
    for (size_t i = 0; i < d.size();) {
        auto n = prefix(d, i);
        if (!n) {
            ++i;
            continue;
        }
        if (start < i) {
            size_t end = i;
            while (end > start && d[end - 1] == 0)
                --end;
            if (end > start)
                result.push_back(d.subspan(start, end - start));
        }
        start = i + n;
        i = start;
    }
    size_t end = d.size();
    while (end > start && d[end - 1] == 0)
        --end;
    if (start < end)
        result.push_back(d.subspan(start, end - start));
    return result;
}
uint8_t nal_type(NalUnit nal) {
    return nal.empty() ? 0 : nal[0] & 31;
}
bool to_annex_b(std::span<const uint8_t> d, std::vector<uint8_t> &out, uint8_t bytes) {
    std::vector<uint8_t> converted;
    if (bytes < 1 || bytes > 4)
        return false;
    bool valid = !d.empty();
    for (size_t i = 0; i < d.size();) {
        if (d.size() - i < bytes) {
            valid = false;
            break;
        }
        uint32_t n = 0;
        for (unsigned j = 0; j < bytes; ++j)
            n = (n << 8) | d[i++];
        if (!n || n > d.size() - i) {
            valid = false;
            break;
        }
        converted.insert(converted.end(), {0, 0, 0, 1});
        converted.insert(converted.end(), d.begin() + i, d.begin() + i + n);
        i += n;
    }
    // A length such as 0x00000100 resembles an Annex-B prefix.
    if (valid) {
        out = std::move(converted);
        return true;
    }
    size_t start = 0;
    while (start < d.size() && d[start] == 0 && !prefix(d, start))
        ++start;
    if (!prefix(d, start))
        return false;
    auto nals = split_annex_b(d);
    if (nals.empty())
        return false;
    for (auto nal : nals)
        if (!nal_type(nal) || (nal[0] & 0x80))
            return false;
    out.assign(d.begin(), d.end());
    return true;
}
std::optional<SpsInfo> parse_sps(NalUnit sps) {
    if (nal_type(sps) != 7 || sps.size() < 4)
        return {};
    Bits b = unescape(sps);
    SpsInfo r{};
    r.profile_idc = static_cast<uint8_t>(b.read(8));
    b.read(8);
    r.level_idc = static_cast<uint8_t>(b.read(8));
    b.ue();
    uint32_t chroma = 1, separate = 0;
    if (r.profile_idc == 100 || r.profile_idc == 110 || r.profile_idc == 122 ||
        r.profile_idc == 244 || r.profile_idc == 44 || r.profile_idc == 83 || r.profile_idc == 86 ||
        r.profile_idc == 118 || r.profile_idc == 128 || r.profile_idc == 138 ||
        r.profile_idc == 139 || r.profile_idc == 134 || r.profile_idc == 135) {
        chroma = b.ue();
        if (chroma > 3)
            return {};
        if (chroma == 3)
            separate = b.read(1);
        b.ue();
        b.ue();
        b.read(1);
        if (b.read(1))
            for (unsigned i = 0; i < (chroma == 3 ? 12u : 8u); ++i)
                if (b.read(1)) {
                    int64_t last = 8, next = 8;
                    for (unsigned j = 0; j < (i < 6 ? 16u : 64u); ++j) {
                        if (next)
                            next = (last + b.se() + 256) % 256;
                        last = next ? next : last;
                    }
                }
    }
    b.ue();
    auto poc = b.ue();
    if (poc == 0)
        b.ue();
    else if (poc == 1) {
        b.read(1);
        b.se();
        b.se();
        auto n = b.ue();
        if (n > 255)
            return {};
        while (n--)
            b.se();
    } else if (poc > 2)
        return {};
    b.ue();
    b.read(1);
    uint64_t w = (uint64_t(b.ue()) + 1) * 16, h = (uint64_t(b.ue()) + 1) * 16;
    auto frame = b.read(1);
    h *= 2 - frame;
    if (!frame)
        b.read(1);
    b.read(1);
    uint64_t left = 0, right = 0, top = 0, bottom = 0;
    if (b.read(1)) {
        left = b.ue();
        right = b.ue();
        top = b.ue();
        bottom = b.ue();
    }
    uint32_t array = separate ? 0 : chroma;
    uint64_t cx = array == 1 || array == 2 ? 2 : 1;
    uint64_t cy = (array == 1 ? 2 : 1) * (2 - frame);
    if (!b.ok || (left + right) * cx >= w || (top + bottom) * cy >= h || w > UINT32_MAX ||
        h > UINT32_MAX)
        return {};
    r.width = static_cast<uint32_t>(w - (left + right) * cx);
    r.height = static_cast<uint32_t>(h - (top + bottom) * cy);
    return r;
}
std::optional<std::vector<uint8_t>> sps_without_reordering(NalUnit sps) {
    if (nal_type(sps) != 7 || sps.size() < 4)
        return {};
    Bits in = unescape(sps);
    Writer out;
    Copier c{in, out};
    const auto profile = c.u(8);
    c.u(8);
    c.u(8);
    c.ue();
    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 ||
        profile == 83 || profile == 86 || profile == 118 || profile == 128 || profile == 138 ||
        profile == 139 || profile == 134 || profile == 135) {
        const auto chroma = c.ue();
        if (chroma == 3)
            c.u(1);
        c.ue();
        c.ue();
        c.u(1);
        if (c.u(1))
            for (unsigned i = 0; i < (chroma == 3 ? 12u : 8u); ++i)
                if (c.u(1)) {
                    int64_t last = 8, next = 8;
                    for (unsigned j = 0; j < (i < 6 ? 16u : 64u); ++j) {
                        if (next) {
                            const auto v = in.se();
                            out.ue(uint32_t(v > 0 ? 2 * v - 1 : -2 * v));
                            next = (last + v + 256) % 256;
                        }
                        last = next ? next : last;
                    }
                }
    }
    c.ue();
    const auto poc = c.ue();
    if (poc == 0)
        c.ue();
    else if (poc == 1) {
        c.u(1);
        c.se();
        c.se();
        const auto n = c.ue();
        if (n > 255)
            return {};
        for (uint32_t i = 0; i < n; ++i)
            c.se();
    } else if (poc > 2)
        return {};
    const auto refs = c.ue();
    c.u(1);
    c.ue();
    c.ue();
    if (!c.u(1))
        c.u(1);
    c.u(1);
    if (c.u(1)) {
        c.ue();
        c.ue();
        c.ue();
        c.ue();
    }
    const bool vui = in.read(1);
    out.write(1, 1);
    if (vui) {
        if (c.u(1) && c.u(8) == 255) {
            c.u(16);
            c.u(16);
        }
        if (c.u(1))
            c.u(1);
        if (c.u(1)) {
            c.u(3);
            c.u(1);
            if (c.u(1)) {
                c.u(8);
                c.u(8);
                c.u(8);
            }
        }
        if (c.u(1)) {
            c.ue();
            c.ue();
        }
        if (c.u(1)) {
            c.u(32);
            c.u(32);
            c.u(1);
        }
        const bool nal_hrd = c.u(1);
        if (nal_hrd)
            c.hrd();
        const bool vcl_hrd = c.u(1);
        if (vcl_hrd)
            c.hrd();
        if (nal_hrd || vcl_hrd)
            c.u(1);
        c.u(1);
        // An existing bitstream_restriction is replaced.
        if (in.read(1))
            for (unsigned i = 0; i < 7; ++i)
                i == 0 ? (void)in.read(1) : (void)in.ue();
    } else {
        // aspect ratio, overscan, video signal, chroma location, timing,
        // NAL HRD, VCL HRD, pic_struct: all absent.
        out.write(0, 8);
    }
    if (!in.ok)
        return {};
    out.write(1, 1);  // bitstream_restriction_flag
    out.write(1, 1);  // motion_vectors_over_pic_boundaries_flag
    out.ue(2);        // max_bytes_per_pic_denom (default)
    out.ue(1);        // max_bits_per_mb_denom (default)
    out.ue(16);       // log2_max_mv_length_horizontal (default)
    out.ue(16);       // log2_max_mv_length_vertical (default)
    out.ue(0);        // max_num_reorder_frames
    out.ue(refs > 1 ? refs : 1);  // max_dec_frame_buffering
    out.write(1, 1);  // rbsp_stop_one_bit, then byte alignment
    std::vector<uint8_t> result{sps[0]};
    unsigned zeros = 0;
    for (auto byte : out.d) {
        if (zeros >= 2 && byte <= 3) {
            result.push_back(3);
            zeros = 0;
        }
        result.push_back(byte);
        zeros = byte == 0 ? zeros + 1 : 0;
    }
    return result;
}
} // namespace handview::encode
