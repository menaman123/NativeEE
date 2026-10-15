// tests/ipu/test_frame_writer.cpp -- ctest FrameWriter: the macroblock-column strip
// layouts. Expected bytes are computed independently from the layout rule
// (strip = 16 columns x aligned_h rows; pixel (x, y) of strip s lives at
// s*aligned_h*16*bpp + y*16*bpp + (x%16)*bpp), not from the writer's loops.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/ipu/hle/frame_writer.h"

using ps2r::ipu::DecodedPicture;
using ps2r::ipu::FrameWriter;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failed;                                                      \
            std::printf("FAIL: %s (line %d): %s\n", msg, __LINE__, #cond);   \
        }                                                                    \
    } while (0)

// A 20x18 gradient picture: R = x*7, G = y*11, B = (x+y)*3, A = 0xFF (the
// decoder's alpha, which the writer must NOT carry over).
static DecodedPicture MakeGradient(int w, int h) {
    DecodedPicture p;
    p.width = w;
    p.height = h;
    p.rgba.resize(static_cast<size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* px = &p.rgba[(static_cast<size_t>(y) * w + x) * 4];
            px[0] = static_cast<uint8_t>(x * 7);
            px[1] = static_cast<uint8_t>(y * 11);
            px[2] = static_cast<uint8_t>((x + y) * 3);
            px[3] = 0xFF;
        }
    }
    return p;
}

static size_t StripOffset(uint32_t x, uint32_t y, uint32_t aligned_h, uint32_t bpp) {
    const uint32_t strip = x / 16;
    return static_cast<size_t>(strip) * aligned_h * 16 * bpp + static_cast<size_t>(y) * 16 * bpp +
           static_cast<size_t>(x % 16) * bpp;
}

static void TestSizes() {
    CHECK(FrameWriter::Rgb32Bytes(20, 18) == 32u * 32u * 4u, "20x18 RGB32 -> 32x32x4 = 4096");
    CHECK(FrameWriter::Rgb32Bytes(640, 480) == 640u * 480u * 4u, "640x480 already aligned");
    CHECK(FrameWriter::Rgb32Bytes(512, 448) == 512u * 448u * 4u, "512x448 already aligned");
    CHECK(FrameWriter::Rgb32Bytes(1, 1) == 16u * 16u * 4u, "1x1 -> one macroblock");
    CHECK(FrameWriter::Rgb16Bytes(24, 20) == 32u * 32u * 2u, "24x20 RGB16 -> 32x32x2 = 2048");
    CHECK(FrameWriter::Rgb16Bytes(0, 0) == 0u, "0x0 -> 0 bytes");
}

// 20x18 -> aligned 32x32, 2 strips of 32 rows x 64 bytes; strip 1 begins at 2048.
static void TestRgb32Strips() {
    const DecodedPicture pic = MakeGradient(20, 18);
    const size_t total = FrameWriter::Rgb32Bytes(20, 18);
    std::vector<uint8_t> dst(total + 16, 0xEE);  // guard bytes past the end
    FrameWriter::WriteRgb32Strips(dst.data(), pic);

    bool inside_ok = true, outside_ok = true, alpha_ok = true;
    for (uint32_t y = 0; y < 32; ++y) {
        for (uint32_t x = 0; x < 32; ++x) {
            const uint8_t* d = dst.data() + StripOffset(x, y, 32, 4);
            if (d[3] != 0x80) alpha_ok = false;
            if (x < 20 && y < 18) {
                const uint8_t* s = &pic.rgba[(static_cast<size_t>(y) * 20 + x) * 4];
                if (d[0] != s[0] || d[1] != s[1] || d[2] != s[2]) inside_ok = false;
            } else {
                if (d[0] != 0 || d[1] != 0 || d[2] != 0) outside_ok = false;
            }
        }
    }
    CHECK(inside_ok, "rgb32: every in-picture pixel carries the picture's RGB");
    CHECK(outside_ok, "rgb32: every padding pixel is black");
    CHECK(alpha_ok, "rgb32: every pixel's alpha is 0x80 (PS2 opaque), never the decoder's 0xFF");

    // Strip 1 begins at byte 32*64 = 2048: its first pixel is picture (16, 0).
    CHECK(dst[2048 + 0] == static_cast<uint8_t>(16 * 7) && dst[2048 + 1] == 0 &&
          dst[2048 + 2] == static_cast<uint8_t>(16 * 3) && dst[2048 + 3] == 0x80,
          "rgb32: strip 1 starts at 2048 with pixel (16,0)");
    // Row 17 of strip 0 = picture row 17; row 18 = padding. Pixel (0,17) at 17*64.
    CHECK(dst[17 * 64 + 1] == static_cast<uint8_t>(17 * 11), "rgb32: row 17 of strip 0 is picture row 17");
    CHECK(dst[18 * 64 + 1] == 0 && dst[18 * 64 + 3] == 0x80, "rgb32: row 18 of strip 0 is opaque black");
    // Strip 1 column 4 (x = 20) is padding on every row.
    CHECK(dst[2048 + 4 * 4 + 0] == 0 && dst[2048 + 4 * 4 + 3] == 0x80, "rgb32: x = 20 is padding");
    bool guard_ok = true;
    for (size_t i = total; i < dst.size(); ++i) if (dst[i] != 0xEE) guard_ok = false;
    CHECK(guard_ok, "rgb32: nothing written past Rgb32Bytes()");
}

static void TestRgb32NoOps() {
    std::vector<uint8_t> dst(64, 0x55);
    DecodedPicture empty;
    FrameWriter::WriteRgb32Strips(dst.data(), empty);
    CHECK(dst[0] == 0x55, "rgb32: empty picture writes nothing");
    DecodedPicture sized;
    sized.width = 4;
    sized.height = 4;  // rgba empty
    FrameWriter::WriteRgb32Strips(dst.data(), sized);
    CHECK(dst[0] == 0x55, "rgb32: picture without pixels writes nothing");
    FrameWriter::WriteRgb32Strips(nullptr, MakeGradient(4, 4));
    CHECK(true, "rgb32: null destination tolerated");
}

static uint16_t Pack16(const uint8_t* rgb) {
    return static_cast<uint16_t>((rgb[0] >> 3) | ((rgb[1] >> 3) << 5) | ((rgb[2] >> 3) << 10) | 0x8000u);
}

// The 20x18 picture on a 24x20 target plane: aligned 32x32, 2 strips x 32 rows x
// 32 bytes; samples only inside the decoded 20x18, the rest of the plane 0x8000.
static void TestRgb16Strips() {
    const DecodedPicture pic = MakeGradient(20, 18);
    const size_t total = FrameWriter::Rgb16Bytes(24, 20);
    std::vector<uint8_t> dst(total + 16, 0xEE);
    FrameWriter::WriteRgb16Strips(dst.data(), 24, 20, pic);

    bool inside_ok = true, outside_ok = true, le_ok = true;
    for (uint32_t y = 0; y < 32; ++y) {
        for (uint32_t x = 0; x < 32; ++x) {
            const uint8_t* d = dst.data() + StripOffset(x, y, 32, 2);
            const uint16_t got = static_cast<uint16_t>(d[0] | (d[1] << 8));
            if ((got & 0x8000u) == 0) le_ok = false;  // A bit is the high bit of the LE word
            if (x < 20 && y < 18) {
                if (got != Pack16(&pic.rgba[(static_cast<size_t>(y) * 20 + x) * 4])) inside_ok = false;
            } else {
                if (got != 0x8000u) outside_ok = false;
            }
        }
    }
    CHECK(inside_ok, "rgb16: in-picture pixels are r5 | g5<<5 | b5<<10 | 0x8000");
    CHECK(outside_ok, "rgb16: pixels outside the decoded extent (incl. target columns 20..23) are 0x8000");
    CHECK(le_ok, "rgb16: little-endian store (alpha bit lands in the second byte)");
    // Explicit packing check: pixel (19, 17) = R 133, G 187, B 108 -> 16,23,13.
    {
        const uint8_t* d = dst.data() + StripOffset(19, 17, 32, 2);
        const uint16_t want = static_cast<uint16_t>(16 | (23 << 5) | (13 << 10) | 0x8000);
        CHECK(d[0] == (want & 0xFF) && d[1] == (want >> 8), "rgb16: pixel (19,17) packs to 0xB6F0");
        CHECK(want == 0xB6F0, "rgb16: independent packing arithmetic");
    }
    // Strip 1 begins at 32 rows * 32 bytes = 1024 with pixel (16, 0):
    // R 112 -> 14, G 0, B 48 -> 6 => 14 | 6<<10 | 0x8000 = 0x980E.
    CHECK(dst[1024] == 0x0E && dst[1025] == 0x98, "rgb16: strip 1 starts at 1024 with pixel (16,0) = 0x980E");
    bool guard_ok = true;
    for (size_t i = total; i < dst.size(); ++i) if (dst[i] != 0xEE) guard_ok = false;
    CHECK(guard_ok, "rgb16: nothing written past Rgb16Bytes()");
}

// A target smaller than the picture: only the plane's area is written and the
// samples come from the picture's own row stride.
static void TestRgb16SmallTarget() {
    const DecodedPicture pic = MakeGradient(20, 18);
    const size_t total = FrameWriter::Rgb16Bytes(16, 16);  // one strip, 16 rows
    std::vector<uint8_t> dst(total + 16, 0xEE);
    FrameWriter::WriteRgb16Strips(dst.data(), 16, 16, pic);
    CHECK(total == 512u, "rgb16: 16x16 target is one 512-byte strip");
    bool ok = true;
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            const uint8_t* d = dst.data() + y * 32 + x * 2;
            const uint16_t got = static_cast<uint16_t>(d[0] | (d[1] << 8));
            if (got != Pack16(&pic.rgba[(static_cast<size_t>(y) * 20 + x) * 4])) ok = false;
        }
    }
    CHECK(ok, "rgb16: small target samples the picture with the picture's 20-pixel stride");
    bool guard_ok = true;
    for (size_t i = total; i < dst.size(); ++i) if (dst[i] != 0xEE) guard_ok = false;
    CHECK(guard_ok, "rgb16: small target writes exactly one strip");
}

static void TestRgb16NoOps() {
    std::vector<uint8_t> dst(64, 0x55);
    FrameWriter::WriteRgb16Strips(dst.data(), 0, 16, MakeGradient(4, 4));
    FrameWriter::WriteRgb16Strips(dst.data(), 16, 0, MakeGradient(4, 4));
    DecodedPicture empty;
    FrameWriter::WriteRgb16Strips(dst.data(), 16, 16, empty);
    CHECK(dst[0] == 0x55, "rgb16: zero target / empty picture write nothing");
}

int main() {
    TestSizes();
    TestRgb32Strips();
    TestRgb32NoOps();
    TestRgb16Strips();
    TestRgb16SmallTarget();
    TestRgb16NoOps();

    std::printf("test_frame_writer: %d/%d checks passed\n", g_checks - g_failed, g_checks);
    if (g_failed == 0) {
        std::printf("test_frame_writer: PASS (sizes, rgb32 strips, rgb16 strips, clamp, no-ops)\n");
        return 0;
    }
    std::printf("test_frame_writer: %d FAILED\n", g_failed);
    return 1;
}
