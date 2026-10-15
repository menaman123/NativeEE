// tests/oracle/og_page_invariance.cpp -- port of the frozen original's
// tests/test_page_invariance.cpp (457 lines, a GL render harness) reduced to the
// VRAM-level contract it rests on, now against NEW Swizzle / ClutLayout:
//
//   THE RULE: identical texture bytes + identical register state decode
//   identically regardless of the VRAM page the tile lives at and of what the
//   neighbouring pages contain (GS_SKILL section 8.3: "the same block and column
//   number in the same page occupy the same physical memory regardless of format").
//
// Construction (the original's live cavern wall-tile shape, same bases): page A =
// 0x330000, page B = 0x3c0000 bytes (blocks 0x3300 / 0x3c00); both receive THE
// SAME 8 KB image -- a 16x16 PSMT8 tile at bp (bw 1), an authored 8x8 L1 at
// bp + 2 blocks (0x200 bytes), a 256-entry PSMCT32 CSM1 CLUT at bp + 8 blocks
// (0x800) -- and different noise in the NEXT page on each side. The original's
// environment pins (PS2_GL_FAST etc.) are dropped: nothing here is gated.
//
// Checks: (1) the two 8 KB page images are byte-identical and (2) the neighbour
// pages differ (construction sanity, as in the original); (3) the tile, (4) the
// L1 and (5) the CLUT decode identically at A and B and equal the authored
// values; (6) authoring page A into a zeroed VRAM touches no byte outside page A
// (co-page containment); (7) Z rows never alias colour rows: for every pixel of
// a page the Z-mode element differs from the colour-mode element of the same
// class (Z32/CT32, Z24/CT24, Z16/CT16, Z16S/CT16S) -- the reason the original's
// SyncZToVRAM insists on the Z tables (opengl_backend.cpp:18375-18395).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gs/format/clut_layout.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/format/swizzle.h"

using ps2r::gs::ClutLayout;
using ps2r::gs::Psm;
using ps2r::gs::PsmLayout;
using ps2r::gs::Swizzle;

namespace {

int g_checks = 0, g_failures = 0;
#define CHECK(cond, msg)                                                   \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) { g_failures++; std::printf("[PAGEINV] FAIL: %s (line %d)\n", msg, __LINE__); } \
    } while (0)

constexpr uint32_t kPageA = 0x330000u / 256u;  // blocks
constexpr uint32_t kPageB = 0x3c0000u / 256u;
constexpr uint32_t kBw = 1;                      // 64 texels
constexpr int kTileW = 16, kTileH = 16;

uint8_t TileIndex(int x, int y) { return uint8_t((x * 7 + y * 13 + ((x ^ y) & 5) * 29) & 0xFF); }
uint8_t L1Index(int x, int y) { return uint8_t((x * 11 + y * 3 + 91) & 0xFF); }
uint32_t ClutEntry(int i) {
    return uint32_t(40 + (i & 63)) | (uint32_t(35 + (i >> 2)) << 8) | (uint32_t(30 + (i & 31)) << 16) | (0x80u << 24);
}

void WriteImageAt(std::vector<uint8_t>& vram, uint32_t bp) {
    uint8_t tile[kTileW * kTileH];
    for (int y = 0; y < kTileH; y++)
        for (int x = 0; x < kTileW; x++) tile[y * kTileW + x] = TileIndex(x, y);
    Swizzle::LinearToVram(vram.data(), {Psm::T8, bp, kBw}, {0, 0, kTileW, kTileH}, tile, kTileW);
    uint8_t l1[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) l1[y * 8 + x] = L1Index(x, y);
    Swizzle::LinearToVram(vram.data(), {Psm::T8, bp + 2, kBw}, {0, 0, 8, 8}, l1, 8);
    // The CLUT as the CSM1 16x16 CT32 rectangle: entry i at Csm1Position(i).
    uint8_t clut_img[16 * 16 * 4] = {};
    for (int i = 0; i < 256; i++) {
        const ClutLayout::Position p = ClutLayout::Csm1Position(uint32_t(i), ClutLayout::kEntries8);
        const uint32_t v = ClutEntry(i);
        std::memcpy(clut_img + (p.y * 16 + p.x) * 4, &v, 4);
    }
    Swizzle::LinearToVram(vram.data(), {Psm::CT32, bp + 8, kBw}, {0, 0, 16, 16}, clut_img, 64);
}

void WriteNeighbourNoise(std::vector<uint8_t>& vram, uint32_t bp, uint32_t seed) {
    uint32_t s = seed;
    const size_t page = size_t(bp) * 256u;
    for (uint32_t i = 0; i < 0x2000; i++) {
        s = s * 1664525u + 1013904223u;
        vram[(page + 0x2000 + i) % vram.size()] = uint8_t(s >> 24);
    }
}

std::vector<uint8_t> DecodeTile(const std::vector<uint8_t>& vram, uint32_t bp, int w, int h) {
    std::vector<uint8_t> out(size_t(w) * h);
    Swizzle::VramToLinear(vram.data(), {Psm::T8, bp, kBw}, {0, 0, uint32_t(w), uint32_t(h)}, out.data(), size_t(w));
    return out;
}

std::vector<uint32_t> DecodeClut(const std::vector<uint8_t>& vram, uint32_t cbp) {
    std::vector<uint32_t> out(256);
    ClutLayout::ReadClut(vram.data(), Psm::CT32, cbp, false, 256, out.data());
    return out;
}

}  // namespace

int main() {
    std::vector<uint8_t> vram(PsmLayout::kVramBytes, 0);
    WriteImageAt(vram, kPageA);
    WriteImageAt(vram, kPageB);
    WriteNeighbourNoise(vram, kPageA, 0xA55AA55Au);
    WriteNeighbourNoise(vram, kPageB, 0x5AA55AA5u);

    // (1) / (2) construction sanity, exactly the original's two checks.
    CHECK(std::memcmp(vram.data() + kPageA * 256u, vram.data() + kPageB * 256u, 0x2000) == 0,
          "tile page images byte-identical (construction sanity)");
    CHECK(std::memcmp(vram.data() + kPageA * 256u + 0x2000, vram.data() + kPageB * 256u + 0x2000, 0x2000) != 0,
          "neighbour pages differ (construction sanity)");

    // (3) tile decodes identically at A and B, and is the authored image.
    const std::vector<uint8_t> ta = DecodeTile(vram, kPageA, kTileW, kTileH), tb = DecodeTile(vram, kPageB, kTileW, kTileH);
    CHECK(ta == tb, "tile: page A and page B decode byte-identical");
    bool authored = true;
    for (int y = 0; y < kTileH; y++)
        for (int x = 0; x < kTileW; x++) authored &= ta[y * kTileW + x] == TileIndex(x, y);
    CHECK(authored, "tile decodes to the authored indices");

    // (4) authored L1.
    const std::vector<uint8_t> la = DecodeTile(vram, kPageA + 2, 8, 8), lb = DecodeTile(vram, kPageB + 2, 8, 8);
    CHECK(la == lb, "L1: page A and page B decode byte-identical");
    authored = true;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) authored &= la[y * 8 + x] == L1Index(x, y);
    CHECK(authored, "L1 decodes to the authored indices");

    // (5) CLUT.
    const std::vector<uint32_t> ca = DecodeClut(vram, kPageA + 8), cb = DecodeClut(vram, kPageB + 8);
    CHECK(ca == cb, "CLUT: page A and page B read byte-identical");
    authored = true;
    for (int i = 0; i < 256; i++) authored &= ca[i] == ClutEntry(i);
    CHECK(authored, "CLUT reads back the authored entries through the CSM1 rectangle");

    // (6) containment: authoring page A alone touches no byte outside page A.
    {
        std::vector<uint8_t> only_a(PsmLayout::kVramBytes, 0);
        WriteImageAt(only_a, kPageA);
        bool outside_clean = true;
        for (size_t a = 0; a < only_a.size(); a++) {
            if (a >= kPageA * 256u && a < kPageA * 256u + 0x2000) continue;
            outside_clean &= only_a[a] == 0;
        }
        CHECK(outside_clean, "writes through Swizzle at page A change no byte outside page A");
        bool inside_touched = false;
        for (size_t a = kPageA * 256u; a < kPageA * 256u + 0x2000; a++) inside_touched |= only_a[a] != 0;
        CHECK(inside_touched, "page A received the image");
        CHECK(std::memcmp(only_a.data() + kPageA * 256u, vram.data() + kPageA * 256u, 0x2000) == 0,
              "page A image independent of page B / neighbour content");
    }

    // (7) Z rows never alias colour rows within a shared page.
    struct Pair { Psm z, c; } pairs[] = {{Psm::Z32, Psm::CT32}, {Psm::Z24, Psm::CT24}, {Psm::Z16, Psm::CT16}, {Psm::Z16S, Psm::CT16S}};
    for (const Pair& p : pairs) {
        const auto& d = PsmLayout::Describe(p.c);
        bool never = true;
        for (uint32_t y = 0; y < d.page_h; y++)
            for (uint32_t x = 0; x < d.page_w; x++)
                never &= PsmLayout::PixelAddress(p.z, x, y, kPageA, kBw) != PsmLayout::PixelAddress(p.c, x, y, kPageA, kBw);
        CHECK(never, "Z-mode element never equals the colour-mode element at the same pixel");
    }
    // ... yet both cover the same page: writing a Z16 buffer into the CT16 page's
    // memory scatters over exactly the page (the co-page corruption the original
    // guards against is a fact of the layout, not of the walker).
    {
        std::vector<uint8_t> z(PsmLayout::kVramBytes, 0), zimg(64 * 64 * 2, 0xEE);
        Swizzle::LinearToVram(z.data(), {Psm::Z16, kPageA, kBw}, {0, 0, 64, 64}, zimg.data(), 128);
        bool page_full = true;
        for (size_t a = kPageA * 256u; a < kPageA * 256u + 0x2000; a++) page_full &= z[a] == 0xEE;
        CHECK(page_full, "a full Z16 page covers exactly the 8 KB");
    }

    std::printf("[PAGEINV] RESULT: %s (%d/%d checks passed)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
