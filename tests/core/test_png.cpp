// tests/core/test_png.cpp -- Png: decode vectors written by Python's zlib (every
// filter type, stored / fixed / dynamic Huffman blocks, the colour types and bit
// depths the codec takes, an interlaced refusal), the encoder's round trip and
// the two checksums' reference values.
//
// png_vectors.h is generated (the script lives in the commit that added it:
// Python `zlib.compress` at level 9 / 0 over hand-filtered rows, two IDAT chunks
// per image); regenerate only when a new case is needed.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/image/png.h"
#include "png_vectors.h"

static int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

using ps2r::image::Png;

int main(int argc, char** argv) {
    // Checksums: the published reference values.
    CHECK(Png::Crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926u);
    CHECK(Png::Adler32(reinterpret_cast<const uint8_t*>("Wikipedia"), 9) == 0x11E60398u);
    CHECK(Png::Crc32(nullptr, 0) == 0u);

    for (const auto& v : kPngVectors) {
        Png::Image image;
        image.width = 0x1234;
        std::string error;
        const bool ok = Png::Decode(v.png, v.png_len, image, &error);
        if (v.width == 0) {
            CHECK(!ok);
            CHECK(image.width == 0x1234 && image.rgba.empty());   // a refusal leaves the output untouched
            CHECK(error.find("interlaced") != std::string::npos);
            continue;
        }
        if (!ok) std::printf("  %s: %s\n", v.name, error.c_str());
        CHECK(ok);
        CHECK(image.width == v.width && image.height == v.height);
        CHECK(image.rgba.size() == v.rgba_len);
        if (image.rgba.size() == v.rgba_len) {
            const bool same = std::memcmp(image.rgba.data(), v.rgba, v.rgba_len) == 0;
            if (!same) {
                for (size_t i = 0; i < v.rgba_len; ++i)
                    if (image.rgba[i] != v.rgba[i]) {
                        std::printf("  %s: first difference at byte %zu (%u vs %u)\n", v.name, i, image.rgba[i], v.rgba[i]);
                        break;
                    }
            }
            CHECK(same);
        }
        // Encode -> Decode is the identity (stored blocks, filter none).
        const auto bytes = Png::Encode(image.rgba.data(), image.width, image.height);
        CHECK(!bytes.empty());
        Png::Image again;
        CHECK(Png::Decode(bytes.data(), bytes.size(), again, &error));
        CHECK(again.width == image.width && again.height == image.height && again.rgba == image.rgba);
        // A corrupted chunk CRC and a truncated stream refuse, never a partial image.
        auto corrupt = std::vector<uint8_t>(v.png, v.png + v.png_len);
        corrupt[40] ^= 0x55;
        Png::Image rejected;
        CHECK(!Png::Decode(corrupt.data(), corrupt.size(), rejected, &error));
        CHECK(!Png::Decode(v.png, v.png_len / 2, rejected, &error));
        CHECK(rejected.rgba.empty());
    }
    // The encoder refuses an impossible size.
    CHECK(Png::Encode(nullptr, 1, 1).empty());
    const uint8_t px[4] = {1, 2, 3, 4};
    CHECK(Png::Encode(px, 0, 1).empty());
    CHECK(Png::Encode(px, Png::kMaxSide + 1, 1).empty());
    // A stored zlib stream of more than one block (> 65535 bytes) round-trips.
    {
        std::vector<uint8_t> big(300 * 100 * 4);
        for (size_t i = 0; i < big.size(); ++i) big[i] = uint8_t(i * 31 + (i >> 9));
        const auto bytes = Png::Encode(big.data(), 300, 100);
        Png::Image image;
        std::string error;
        CHECK(Png::Decode(bytes.data(), bytes.size(), image, &error));
        CHECK(image.rgba == big);
    }
    // File round trip in the scratch directory.
    if (argc > 1) {
        const std::string path = std::string(argv[1]) + "/png_roundtrip.png";
        std::string error;
        CHECK(Png::EncodeFile(path, kPngVectors[0].rgba, kPngVectors[0].width, kPngVectors[0].height, &error));
        Png::Image image;
        CHECK(Png::DecodeFile(path, image, &error));
        CHECK(image.rgba.size() == kPngVectors[0].rgba_len &&
              std::memcmp(image.rgba.data(), kPngVectors[0].rgba, kPngVectors[0].rgba_len) == 0);
        CHECK(!Png::DecodeFile(std::string(argv[1]) + "/does_not_exist.png", image, &error));
        std::remove(path.c_str());
    }
    if (g_fail) { std::printf("test_png: %d failure(s)\n", g_fail); return 1; }
    std::printf("test_png: OK (%zu vectors)\n", sizeof(kPngVectors) / sizeof(kPngVectors[0]));
    return 0;
}
