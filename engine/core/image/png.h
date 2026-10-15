// core/image/png.h -- Png: the one PNG codec of the host (texture packs,
// --dump-textures, contact sheets). RGBA8 in, RGBA8 out; no third-party
// library, no title knowledge.
//
//   Decode : any non-interlaced PNG a modder's tool writes -- colour types
//            grey / grey+alpha / RGB / RGBA / palette (tRNS honoured), bit
//            depths 1..16 (16 takes the high byte), every filter type, zlib
//            streams with stored, fixed and dynamic Huffman blocks. The result
//            is top-row-first RGBA8, straight (not premultiplied) alpha.
//            Interlaced (Adam7) images and anything outside the spec are
//            refused with the reason -- never a partial image.
//   Encode : a valid PNG of the RGBA8 rows, zlib STORED blocks (no
//            compression: the writer is for dumps a modder edits, size is not
//            the point), CRC32/Adler-32 exact.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ps2r {
namespace image {

class Png {
public:
    struct Image {
        uint32_t width = 0, height = 0;
        std::vector<uint8_t> rgba;   // width*height*4, top row first
    };
    static constexpr uint32_t kMaxSide = 16384;   // 16384^2 RGBA8 = 1 GiB: the refusal bound

    // False leaves `out` untouched and names the reason in `error`.
    static bool Decode(const uint8_t* bytes, size_t size, Image& out, std::string* error = nullptr);
    static bool DecodeFile(const std::string& path, Image& out, std::string* error = nullptr);
    // `rgba` is width*height*4 bytes, top row first. Empty on an invalid size.
    static std::vector<uint8_t> Encode(const uint8_t* rgba, uint32_t width, uint32_t height);
    static bool EncodeFile(const std::string& path, const uint8_t* rgba, uint32_t width, uint32_t height,
                           std::string* error = nullptr);
    // The two checksums PNG/zlib carry, exposed for the tests' vectors.
    static uint32_t Crc32(const uint8_t* bytes, size_t size, uint32_t seed = 0);
    static uint32_t Adler32(const uint8_t* bytes, size_t size);
    // zlib inflate (RFC 1950/1951) of a complete stream; false names the defect.
    static bool Inflate(const uint8_t* bytes, size_t size, std::vector<uint8_t>& out, std::string* error = nullptr);
};

}  // namespace image
}  // namespace ps2r
