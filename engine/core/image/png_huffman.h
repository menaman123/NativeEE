// core/image/png_huffman.h -- PngHuffman: a canonical Huffman decode table
// (RFC 1951 3.2.2) over PngBitReader (Png's private helper).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "engine/core/image/png_bit_reader.h"

namespace ps2r {
namespace image {

struct PngHuffman {
    // Canonical decode by code length: counts per length, symbols ordered by
    // (length, value).
    std::array<uint16_t, 16> count{};
    std::array<uint16_t, 320> symbol{};
    // False for an over-subscribed set of code lengths.
    bool Build(const uint8_t* lengths, size_t n);
    bool Decode(PngBitReader& in, int& out) const;
};

}  // namespace image
}  // namespace ps2r
