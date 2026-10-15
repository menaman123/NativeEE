// tests/gs/format/test_block_decode.cpp -- BlockDecode properties: the generated
// page LUT equals PsmLayout::ElementInPage entry by entry and the LUT decode path
// equals the per-pixel PixelAddress reference byte for byte (all 13 PSMs, sampled
// rects incl. sub-page, page-straddling, wider-than-buffer and 4 MB-wrapping);
// Decode == DecodeScalar (the SIMD proof when PS2R_SIMD is on -- SimdEnabled()
// is printed); the value rules per mode (TexelValue); ReadPalette through
// ClutLayout; and the footprint / row-band page arithmetic against a brute-force
// max over PixelAddress.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gs/format/block_decode.h"
#include "engine/gs/format/clut_layout.h"
#include "engine/gs/format/pixel_convert.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/format/texa_expand.h"

using ps2r::gs::BlockDecode;
using ps2r::gs::ClutLayout;
using ps2r::gs::PixelConvert;
using ps2r::gs::Psm;
using ps2r::gs::PsmDescriptor;
using ps2r::gs::PsmLayout;
using ps2r::gs::TexaExpand;

namespace {

int g_checks = 0, g_failures = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        g_checks++;                                                   \
        if (!(cond)) {                                                \
            g_failures++;                                             \
            std::printf("  FAIL line %d: ", __LINE__);                \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

uint64_t g_rng = 0x9E3779B97F4A7C15ull;
uint64_t Rnd64() {
    uint64_t x = g_rng;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    g_rng = x;
    return x * 0x2545F4914F6CDD1Dull;
}
uint32_t Rnd(uint32_t lo, uint32_t hi) { return lo + uint32_t(Rnd64() % (uint64_t(hi) - lo + 1u)); }

struct Shape { uint32_t tbp, tbw, w, h; BlockDecode::Rect r; const char* what; };

}  // namespace

int main() {
    std::vector<uint8_t> vram(BlockDecode::kVramBytes);
    for (size_t i = 0; i < vram.size(); i += 8) { const uint64_t v = Rnd64(); std::memcpy(&vram[i], &v, 8); }
    const TexaExpand::Texa texa{0x33, 0x80, true};

    std::printf("test_block_decode: SIMD %s\n", BlockDecode::SimdEnabled() ? "ON (Decode uses the NEON 16-bit expansion)" : "OFF (Decode == DecodeScalar by identity)");

    // --- 1. the generated page LUT is PsmLayout::ElementInPage, entry by entry, and a
    //        permutation of the page's elements (every element exactly once).
    for (int i = 0; i < ps2r::gs::kPsmCount; i++) {
        const PsmDescriptor& d = PsmLayout::Table()[i];
        const uint16_t* lut = BlockDecode::PageLut(d.psm);
        const uint32_t n = uint32_t(d.page_w) * d.page_h;
        std::vector<uint8_t> seen(PsmLayout::ElementsPerPage(d.psm), 0);
        uint32_t bad = 0, dup = 0;
        for (uint32_t ly = 0; ly < d.page_h; ly++)
            for (uint32_t lx = 0; lx < d.page_w; lx++) {
                const uint32_t e = lut[ly * d.page_w + lx];
                if (e != PsmLayout::ElementInPage(d.psm, lx, ly)) bad++;
                if (seen[e]++) dup++;
            }
        CHECK(bad == 0, "%s: %u LUT entries differ from ElementInPage", d.name, bad);
        CHECK(dup == 0 && n == PsmLayout::ElementsPerPage(d.psm), "%s: LUT not a permutation (%u dups)", d.name, dup);
        CHECK(BlockDecode::PageLut(d.psm) == lut, "%s: LUT generated once", d.name);
    }

    // --- 2. LUT path == per-pixel reference, and Decode == DecodeScalar, over sampled
    //        shapes per PSM (palette prepared once per PSM).
    uint64_t words = 0;
    for (int i = 0; i < ps2r::gs::kPsmCount; i++) {
        const PsmDescriptor& d = PsmLayout::Table()[i];
        uint32_t palette[256];
        const uint32_t entries = BlockDecode::PaletteEntries(d.psm);
        if (entries) BlockDecode::ReadPalette(vram.data(), BlockDecode::Palette{Psm::CT32, 0x3300, false}, entries, texa, palette);
        const uint32_t pw = d.page_w, ph = d.page_h;
        const Shape shapes[] = {
            {0, 1, pw, ph, {0, 0, pw, ph}, "one page"},
            {9, 1, 16, 16, {0, 0, 16, 16}, "block-boundary base (section 8.5)"},
            {0x3300, 4, 256, 256, {0, 0, 256, 256}, "4 pages wide, multi-row"},
            {0x100, 2, 128, 128, {37, 19, 61, 77}, "sub-rect straddling pages"},
            {0x2000, 1, 3, 1023, {0, 0, 3, 1023}, "3 wide, 1023 tall (tbw 1)"},
            {0x1234, 0, 17, 13, {0, 0, 17, 13}, "tbw 0 = 1 page per row"},
            {0x280 * 32 + 5, 8, 1024, 448, {0, 0, 1024, 448}, "wider than the buffer (T8H fog shape)"},
            {511 * 32 + 3, 1, pw, 2 * ph, {0, 0, pw, 2 * ph}, "4 MB wrap"},
            {0x3c00, 1, 1, 1, {0, 0, 1, 1}, "1x1"},
            {0x150000 / 256, 10, 640, 448, {576, 384, 64, 64}, "far corner rect"},
        };
        for (const Shape& s : shapes) {
            const BlockDecode::Texture tex{d.psm, s.tbp, s.tbw, s.w, s.h};
            std::vector<uint32_t> a(size_t(s.r.w) * s.r.h), b(a.size()), c(a.size());
            BlockDecode::DecodeGeneric(vram.data(), tex, s.r, texa, palette, a.data());
            BlockDecode::DecodeScalar(vram.data(), tex, s.r, texa, palette, b.data());
            BlockDecode::Decode(vram.data(), tex, s.r, texa, palette, c.data());
            CHECK(a == b, "%s %s: LUT path != per-pixel reference", d.name, s.what);
            CHECK(b == c, "%s %s: Decode (SIMD %d) != DecodeScalar", d.name, s.what, int(BlockDecode::SimdEnabled()));
            words += a.size();
        }
        // random shapes
        for (int k = 0; k < 12; k++) {
            const uint32_t w = Rnd(1, 300), h = Rnd(1, 300);
            const BlockDecode::Texture tex{d.psm, Rnd(0, PsmLayout::kBlockCount - 1), Rnd(0, 16), w, h};
            const uint32_t rx = Rnd(0, w - 1), ry = Rnd(0, h - 1);
            const BlockDecode::Rect r{rx, ry, Rnd(1, w - rx), Rnd(1, h - ry)};
            std::vector<uint32_t> a(size_t(r.w) * r.h), b(a.size()), c(a.size());
            BlockDecode::DecodeGeneric(vram.data(), tex, r, texa, palette, a.data());
            BlockDecode::DecodeScalar(vram.data(), tex, r, texa, palette, b.data());
            BlockDecode::Decode(vram.data(), tex, r, texa, palette, c.data());
            CHECK(a == b, "%s random %d: LUT path != reference", d.name, k);
            CHECK(b == c, "%s random %d: Decode != DecodeScalar", d.name, k);
            words += a.size();
        }
        // whole-texture overload == the full rect
        {
            const BlockDecode::Texture tex{d.psm, 0x3300, 2, 100, 70};
            std::vector<uint32_t> a(7000), b(7000);
            BlockDecode::Decode(vram.data(), tex, texa, palette, a.data());
            BlockDecode::Decode(vram.data(), tex, BlockDecode::Rect{0, 0, 100, 70}, texa, palette, b.data());
            CHECK(a == b, "%s: whole-texture overload", d.name);
        }
    }
    std::printf("  identity sweep: %llu texels x 3 paths\n", (unsigned long long)words);

    // --- 3. value rules per mode (TexelValue).
    {
        uint32_t pal[256];
        for (uint32_t i = 0; i < 256; i++) pal[i] = 0x01000000u * (i & 0x7F) + i;
        CHECK(BlockDecode::TexelValue(Psm::CT32, 0xDEADBEEFu, texa, nullptr) == 0xDEADBEEFu, "CT32 raw");
        CHECK(BlockDecode::TexelValue(Psm::CT24, 0xDE123456u, texa, nullptr) == TexaExpand::ApplyTexa24(0xDE123456u, texa), "CT24 TEXA");
        CHECK(BlockDecode::TexelValue(Psm::CT24, 0xDE000000u, texa, nullptr) == 0u, "CT24 black under AEM -> alpha 0");
        CHECK(BlockDecode::TexelValue(Psm::CT16, 0x8001u, texa, nullptr) == TexaExpand::ApplyTexa16(0x8001u, texa), "CT16 TEXA");
        CHECK(PixelConvert::A(BlockDecode::TexelValue(Psm::CT16, 0x8001u, texa, nullptr)) == 0x80, "CT16 A=1 -> TA1");
        CHECK(PixelConvert::A(BlockDecode::TexelValue(Psm::CT16S, 0x0001u, texa, nullptr)) == 0x33, "CT16S A=0 -> TA0");
        CHECK(BlockDecode::TexelValue(Psm::Z32, 0x12345678u, texa, nullptr) == 0x12345678u, "Z32 word");
        CHECK(BlockDecode::TexelValue(Psm::Z24, 0xFF345678u, texa, nullptr) == 0x00345678u, "Z24 low 24");
        CHECK(BlockDecode::TexelValue(Psm::Z16, 0x8001u, texa, nullptr) == TexaExpand::Expand16To32(0x8001u), "Z16 frame form");
        CHECK(PixelConvert::A(BlockDecode::TexelValue(Psm::Z16S, 0x8001u, texa, nullptr)) == 0xFF, "Z16S A bit -> 0xFF (no TEXA)");
        CHECK(BlockDecode::TexelValue(Psm::T8, 0xA5u, texa, pal) == pal[0xA5], "T8 index");
        CHECK(BlockDecode::TexelValue(Psm::T4, 0x7u, texa, pal) == pal[7], "T4 nibble");
        CHECK(BlockDecode::TexelValue(Psm::T8H, 0xA5123456u, texa, pal) == pal[0xA5], "T8H bits 31:24");
        CHECK(BlockDecode::TexelValue(Psm::T4HL, 0xA5123456u, texa, pal) == pal[0x5], "T4HL bits 27:24");
        CHECK(BlockDecode::TexelValue(Psm::T4HH, 0xA5123456u, texa, pal) == pal[0xA], "T4HH bits 31:28");
        CHECK(BlockDecode::PaletteEntries(Psm::T8) == 256 && BlockDecode::PaletteEntries(Psm::T8H) == 256, "256-entry modes");
        CHECK(BlockDecode::PaletteEntries(Psm::T4) == 16 && BlockDecode::PaletteEntries(Psm::T4HL) == 16 && BlockDecode::PaletteEntries(Psm::T4HH) == 16, "16-entry modes");
        CHECK(BlockDecode::PaletteEntries(Psm::CT32) == 0 && BlockDecode::PaletteEntries(Psm::Z16) == 0, "no palette");
    }

    // --- 4. ReadPalette = ClutLayout reads + the 16-bit value rule.
    {
        uint32_t p32[256], p16[256], p16s[16];
        BlockDecode::ReadPalette(vram.data(), BlockDecode::Palette{Psm::CT32, 0x3c00, false}, 256, texa, p32);
        BlockDecode::ReadPalette(vram.data(), BlockDecode::Palette{Psm::CT16, 0x3c00, true}, 256, texa, p16);
        BlockDecode::ReadPalette(vram.data(), BlockDecode::Palette{Psm::CT16S, 0x3c00, false}, 16, texa, p16s);
        uint32_t bad32 = 0, bad16 = 0, bad16s = 0;
        for (uint32_t i = 0; i < 256; i++) {
            if (p32[i] != ClutLayout::ReadClut32(vram.data(), 0x3c00, false, 256, i)) bad32++;
            if (p16[i] != TexaExpand::ApplyTexa16(ClutLayout::ReadClut16(vram.data(), Psm::CT16, 0x3c00, true, 256, i), texa)) bad16++;
            if (i < 16 && p16s[i] != TexaExpand::ApplyTexa16(ClutLayout::ReadClut16(vram.data(), Psm::CT16S, 0x3c00, false, 16, i), texa)) bad16s++;
        }
        CHECK(bad32 == 0, "CT32 palette raw: %u differ", bad32);
        CHECK(bad16 == 0, "CT16 CSM2 palette through ApplyTexa16: %u differ", bad16);
        CHECK(bad16s == 0, "CT16S IDTEX4 palette: %u differ", bad16s);
    }

    // --- 5. footprint / row band: the page window covers exactly the pages a
    //        brute-force max over PixelByteAddress touches (unwrapped, base 0).
    for (int i = 0; i < ps2r::gs::kPsmCount; i++) {
        const PsmDescriptor& d = PsmLayout::Table()[i];
        const uint32_t dims[][3] = {{1, 1, 0}, {64, 32, 1}, {256, 256, 4}, {1024, 448, 8}, {300, 17, 2}, {17, 300, 3}, {130, 130, 1}};
        for (const uint32_t* t : dims) {
            const BlockDecode::Texture tex{d.psm, 0, t[2], t[0], t[1]};
            const BlockDecode::Footprint fp = BlockDecode::DecodeFootprint(tex);
            // brute force: highest byte touched + 1, rounded up to a page
            uint64_t hi = 0;
            const uint32_t sy = t[1] > 64 ? 7 : 1, sx = t[0] > 64 ? 5 : 1;
            for (uint32_t y = 0; y < t[1]; y += sy)
                for (uint32_t x = 0; x < t[0]; x += sx) {
                    const uint64_t e = PsmLayout::PixelByteAddress(d.psm, x, y, 0, t[2]);
                    if (e + 1 > hi) hi = e + 1;
                }
            // the sampled grid may skip the last row/column: walk them explicitly
            for (uint32_t y = 0; y < t[1]; y++) { const uint64_t e = PsmLayout::PixelByteAddress(d.psm, t[0] - 1, y, 0, t[2]); if (e + 1 > hi) hi = e + 1; }
            for (uint32_t x = 0; x < t[0]; x++) { const uint64_t e = PsmLayout::PixelByteAddress(d.psm, x, t[1] - 1, 0, t[2]); if (e + 1 > hi) hi = e + 1; }
            const uint64_t pages = (hi + BlockDecode::kPageBytes - 1) / BlockDecode::kPageBytes;
            CHECK(fp.start == 0 && fp.size == pages * BlockDecode::kPageBytes,
                  "%s %ux%u tbw %u: footprint %u B, brute force %llu pages", d.name, t[0], t[1], t[2], fp.size, (unsigned long long)pages);
            // row band [y0, y1) inside the footprint, first page = y0's page row
            const uint32_t y0 = t[1] / 3, y1 = t[1];
            const BlockDecode::Footprint rb = BlockDecode::DecodeRowBand(tex, y0, y1);
            CHECK(rb.start == uint64_t(y0 / d.page_h) * PsmLayout::PagesPerRow(d.psm, t[2]) * BlockDecode::kPageBytes,
                  "%s row band start", d.name);
            CHECK(rb.start + rb.size == fp.size, "%s row band to the end == footprint end (%u+%u vs %u)", d.name, rb.start, rb.size, fp.size);
        }
        // base offset and the 4 MB clamp
        const BlockDecode::Footprint fb = BlockDecode::DecodeFootprint(BlockDecode::Texture{d.psm, 0x3300, 1, 16, 16});
        CHECK(fb.start == 0x330000u && fb.size == BlockDecode::kPageBytes, "%s: base 0x3300 -> 0x330000, one page", d.name);
        const BlockDecode::Footprint big = BlockDecode::DecodeFootprint(BlockDecode::Texture{d.psm, 0, 64, 4096, 4096});
        CHECK(big.size == BlockDecode::kVramBytes, "%s: footprint clamps at 4 MB", d.name);
        const BlockDecode::Footprint z = BlockDecode::DecodeFootprint(BlockDecode::Texture{d.psm, 0, 0, 0, 0});
        CHECK(z.size == BlockDecode::kPageBytes, "%s: 0x0 texture reads one page", d.name);
        const BlockDecode::Footprint inv = BlockDecode::DecodeRowBand(BlockDecode::Texture{d.psm, 0, 1, 8, 8}, 5, 2);
        CHECK(inv.start == 0 && inv.size == BlockDecode::kPageBytes, "%s: y1 <= y0 reads one row", d.name);
    }

    std::printf("test_block_decode: %s (%d/%d checks passed)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
