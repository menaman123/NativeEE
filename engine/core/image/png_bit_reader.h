// core/image/png_bit_reader.h -- PngBitReader: the LSB-first bit reader an
// RFC 1951 inflate consumes (Png's private helper).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2r {
namespace image {

class PngBitReader {
public:
    PngBitReader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    // `count` (<= 32) bits, LSB first; false past the end of the stream.
    bool Bits(unsigned count, uint32_t& value);
    void AlignByte();
    size_t Position() const { return at_; }
    bool Skip(size_t bytes);
    const uint8_t* Cursor() const { return p_ + at_; }

private:
    const uint8_t* p_;
    size_t n_, at_ = 0;
    uint32_t acc_ = 0;
    unsigned have_ = 0;
};

}  // namespace image
}  // namespace ps2r
