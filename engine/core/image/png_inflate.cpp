// core/image/png_inflate.cpp -- PngBitReader and PngHuffman bodies.
#include "engine/core/image/png_huffman.h"

namespace ps2r {
namespace image {

bool PngBitReader::Bits(unsigned count, uint32_t& value) {
    while (have_ < count) {
        if (at_ >= n_) return false;
        acc_ |= uint32_t(p_[at_++]) << have_;
        have_ += 8;
    }
    value = acc_ & ((count == 32) ? 0xffffffffu : ((1u << count) - 1));
    acc_ >>= count;
    have_ -= count;
    return true;
}
void PngBitReader::AlignByte() { acc_ = 0; have_ = 0; }
bool PngBitReader::Skip(size_t bytes) {
    if (bytes > n_ - at_) return false;
    at_ += bytes;
    return true;
}

bool PngHuffman::Build(const uint8_t* lengths, size_t n) {
    count.fill(0);
    for (size_t i = 0; i < n; ++i) ++count[lengths[i]];
    count[0] = 0;
    int left = 1;
    for (unsigned len = 1; len < 16; ++len) {
        left <<= 1;
        left -= count[len];
        if (left < 0) return false;   // over-subscribed
    }
    std::array<uint16_t, 16> offset{};
    for (unsigned len = 1; len < 15; ++len) offset[len + 1] = uint16_t(offset[len] + count[len]);
    for (size_t i = 0; i < n; ++i)
        if (lengths[i]) symbol[offset[lengths[i]]++] = uint16_t(i);
    return true;
}
bool PngHuffman::Decode(PngBitReader& in, int& out) const {
    int code = 0, first = 0, index = 0;
    for (unsigned len = 1; len < 16; ++len) {
        uint32_t bit = 0;
        if (!in.Bits(1, bit)) return false;
        code |= int(bit);
        const int n = count[len];
        if (code - n < first) { out = symbol[size_t(index + (code - first))]; return true; }
        index += n;
        first += n;
        first <<= 1;
        code <<= 1;
    }
    return false;
}

}  // namespace image
}  // namespace ps2r
