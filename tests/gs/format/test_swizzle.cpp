// tests/gs/format/test_swizzle.cpp -- Swizzle property tests (PHASE2_PLAN section 4
// item 5): the block-run fast path is byte-identical to the per-pixel PsmLayout
// reference for every PSM, masked writes included, across the 4 MB wrap; and
// LinearToVram followed by VramToLinear is the identity on the rect
// (bijection: every rect pixel lands on its own element and comes back).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gs/format/psm_layout.h"
#include "engine/gs/format/swizzle.h"

using ps2r::gs::Psm;
using ps2r::gs::PsmDescriptor;
using ps2r::gs::PsmLayout;
using ps2r::gs::Swizzle;

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

uint32_t g_seed = 0x9E3779B9u;
uint32_t Rnd() { g_seed = g_seed * 1664525u + 1013904223u; return g_seed >> 8; }

void FillRandom(std::vector<uint8_t>& v) { for (auto& b : v) b = uint8_t(Rnd()); }

// The rects: transfer-window shapes (1-row segments at odd offsets, the per-qword
// F3 role), page-sized and multi-page uploads, sub-block and block-straddling
// windows, and rects whose buffer runs past the 4 MB.
struct Case { uint32_t bp, bw, x, y, w, h; };
const Case kCases[] = {
    {0, 1, 0, 0, 64, 32},        {0, 1, 0, 0, 128, 128},     {0, 4, 3, 5, 61, 27},
    {9, 2, 0, 0, 100, 70},       {0x3300, 1, 0, 0, 16, 16},  {0x3300, 1, 5, 0, 7, 1},
    {0x3c00, 1, 0, 0, 16, 16},   {0, 8, 0, 0, 512, 64},      {0, 10, 33, 17, 300, 40},
    {511 * 32, 1, 0, 0, 64, 64}, {511 * 32 + 3, 4, 0, 0, 256, 33}, {0, 1, 1, 1, 1, 1},
    {1, 1, 63, 31, 1, 1},        {0, 3, 130, 66, 45, 3},     {12345, 5, 7, 9, 200, 37},
};

const uint32_t kMasks[] = {Swizzle::kAllBits, 0x00FFFFFFu, 0xFFFF00FFu, 0x0000FF00u, 0xF0F0F0F0u};

void FastEqualsGeneric(const PsmDescriptor& d) {
    std::vector<uint8_t> vram_a(Swizzle::kVramBytes), vram_b(Swizzle::kVramBytes);
    for (const Case& c : kCases) {
        const Swizzle::Buffer buf{d.psm, c.bp, c.bw};
        const Swizzle::Rect r{c.x, c.y, c.w, c.h};
        const size_t pitch = Swizzle::MinPitch(d.psm, c.w) + 3;  // a little slack, like a real buffer
        std::vector<uint8_t> src(pitch * c.h);
        FillRandom(src);
        FillRandom(vram_a);
        vram_b = vram_a;
        for (uint32_t mask : kMasks) {
            std::vector<uint8_t> va = vram_a, vb = vram_b;
            Swizzle::LinearToVram(va.data(), buf, r, src.data(), pitch, mask);
            Swizzle::LinearToVramGeneric(vb.data(), buf, r, src.data(), pitch, mask);
            CHECK(va == vb, "%s LinearToVram fast != generic (bp=%u bw=%u rect %u,%u %ux%u mask=%08x)",
                  d.name, c.bp, c.bw, c.x, c.y, c.w, c.h, mask);
            std::vector<uint8_t> la(pitch * c.h, 0xA5), lb(pitch * c.h, 0xA5);
            Swizzle::VramToLinear(va.data(), buf, r, la.data(), pitch);
            Swizzle::VramToLinearGeneric(va.data(), buf, r, lb.data(), pitch);
            CHECK(la == lb, "%s VramToLinear fast != generic (bp=%u bw=%u rect %u,%u %ux%u)",
                  d.name, c.bp, c.bw, c.x, c.y, c.w, c.h);
        }
    }
}

// Write a random image, read it back: every pixel returns (masked to the
// mode's pixel bits), and the number of VRAM bytes that changed is exactly the
// rect's footprint (no pixel shares an element with another -> bijection on the
// rect; VRAM was pre-filled with the complement so every write is visible).
void RoundTrip(const PsmDescriptor& d) {
    const uint32_t bpp = Swizzle::LinearBitsPerPixel(d.psm);
    std::vector<uint8_t> vram(Swizzle::kVramBytes);
    for (const Case& c : kCases) {
        // A rect past the buffer width aliases the next page row on real hardware
        // too (section 8.2); only in-width rects are bijections.
        const uint32_t width_px = c.bw * 64u > d.page_w ? c.bw * 64u : d.page_w;
        if (c.x + c.w > width_px) continue;
        const Swizzle::Buffer buf{d.psm, c.bp, c.bw};
        const Swizzle::Rect r{c.x, c.y, c.w, c.h};
        const size_t pitch = Swizzle::MinPitch(d.psm, c.w);
        std::vector<uint8_t> src(pitch * c.h), back(pitch * c.h, 0);
        FillRandom(src);
        // Distinct pixel values where the width allows: make the image a counter so
        // any two pixels landing on one element would be detectable.
        for (uint32_t j = 0; j < c.h; j++)
            for (uint32_t i = 0; i < c.w; i++) {
                const uint32_t v = (j * c.w + i) * 2654435761u;
                uint8_t* row = src.data() + j * pitch;
                if (bpp == 32) std::memcpy(row + i * 4, &v, 4);
                else if (bpp == 16) { row[i * 2] = uint8_t(v); row[i * 2 + 1] = uint8_t(v >> 8); }
                else if (bpp == 8) row[i] = uint8_t(v);
                else row[i / 2] = uint8_t((row[i / 2] & ~(0xFu << ((i & 1) * 4))) | ((v & 0xFu) << ((i & 1) * 4)));
            }
        std::memset(vram.data(), 0, vram.size());
        Swizzle::LinearToVram(vram.data(), buf, r, src.data(), pitch);
        Swizzle::VramToLinear(vram.data(), buf, r, back.data(), pitch);
        // Expected read-back: the source masked to the pixel bits (and, for odd
        // 4-bit widths, without the padding nibble the walker never touches).
        std::vector<uint8_t> want = src;
        if (d.psm == Psm::CT24 || d.psm == Psm::Z24)
            for (uint32_t j = 0; j < c.h; j++)
                for (uint32_t i = 0; i < c.w; i++) want[j * pitch + i * 4 + 3] = 0;
        if (bpp == 4 && (c.w & 1u))
            for (uint32_t j = 0; j < c.h; j++) want[j * pitch + c.w / 2] &= 0x0Fu;
        CHECK(back == want, "%s round trip differs (bp=%u bw=%u rect %u,%u %ux%u)",
              d.name, c.bp, c.bw, c.x, c.y, c.w, c.h);
        // Footprint: count changed bits == pixels * pixel bits (bijection).
        uint64_t bits = 0;
        for (uint8_t b : vram) bits += __builtin_popcount(b);
        uint64_t want_bits = 0;
        for (uint32_t j = 0; j < c.h; j++)
            for (uint32_t i = 0; i < c.w; i++) {
                const uint32_t v = (j * c.w + i) * 2654435761u;
                const uint32_t pixel_bits = bpp == 32 ? Swizzle::ElementWriteMask(d.psm) : (1u << bpp) - 1u;
                want_bits += __builtin_popcount(v & pixel_bits & (bpp == 32 ? 0xFFFFFFFFu : (1u << bpp) - 1u));
            }
        CHECK(bits == want_bits, "%s footprint bits %llu != %llu (pixels overlap or leak)",
              d.name, (unsigned long long)bits, (unsigned long long)want_bits);
    }
}

// Every pixel of two full pages maps to a distinct element and the elements
// are exactly the pages' range (PixelAddress bijection restated at rect level).
void PageBijection(const PsmDescriptor& d) {
    const uint32_t per_page = PsmLayout::ElementsPerPage(d.psm);
    std::vector<uint8_t> seen(per_page * 2, 0);
    for (uint32_t y = 0; y < d.page_h; y++)
        for (uint32_t x = 0; x < 2u * d.page_w; x++) {
            const uint32_t e = PsmLayout::PixelAddress(d.psm, x, y, 0, (2u * d.page_w) / 64u);
            CHECK(e < per_page * 2, "%s element %u outside two pages", d.name, e);
            if (e < per_page * 2) seen[e]++;
        }
    uint32_t dup = 0, missing = 0;
    for (uint8_t s : seen) { if (s > 1) dup++; if (s == 0) missing++; }
    CHECK(dup == 0 && missing == 0, "%s two-page bijection: %u duplicates, %u missing", d.name, dup, missing);
}

}  // namespace

int main() {
    for (int i = 0; i < ps2r::gs::kPsmCount; i++) {
        const PsmDescriptor& d = PsmLayout::Table()[i];
        FastEqualsGeneric(d);
        RoundTrip(d);
        PageBijection(d);
    }
    // Masks: the mode's intrinsic pixel bits.
    CHECK(Swizzle::ElementWriteMask(Psm::CT24) == 0x00FFFFFFu, "CT24 mask");
    CHECK(Swizzle::ElementWriteMask(Psm::Z24) == 0x00FFFFFFu, "Z24 mask");
    CHECK(Swizzle::ElementWriteMask(Psm::T8H) == 0xFF000000u, "T8H mask");
    CHECK(Swizzle::ElementWriteMask(Psm::T4HL) == 0x0F000000u, "T4HL mask");
    CHECK(Swizzle::ElementWriteMask(Psm::T4HH) == 0xF0000000u, "T4HH mask");
    CHECK(Swizzle::ElementWriteMask(Psm::CT16S) == 0xFFFFu, "CT16S mask");
    CHECK(Swizzle::ElementWriteMask(Psm::T4) == 0xFu, "T4 mask");
    CHECK(Swizzle::LinearBitsPerPixel(Psm::T8H) == 8 && Swizzle::LinearBitsPerPixel(Psm::T4HH) == 4 &&
          Swizzle::LinearBitsPerPixel(Psm::CT24) == 32 && Swizzle::LinearBitsPerPixel(Psm::Z16S) == 16,
          "linear bits per pixel");
    CHECK(Swizzle::MinPitch(Psm::T4, 33) == 17 && Swizzle::MinPitch(Psm::CT32, 3) == 12, "min pitch");
    // T8H over a CT24 buffer: the index write leaves the colour bytes; the colour
    // write leaves the index (the section 2.6.3 co-page use).
    {
        std::vector<uint8_t> vram(Swizzle::kVramBytes, 0);
        const uint32_t rgb = 0x00ABCDEFu;
        std::vector<uint8_t> col(4 * 8);
        for (int i = 0; i < 8; i++) std::memcpy(col.data() + i * 4, &rgb, 4);
        std::vector<uint8_t> idx(8);
        for (int i = 0; i < 8; i++) idx[i] = uint8_t(0x10 + i);
        Swizzle::LinearToVram(vram.data(), {Psm::CT24, 0, 1}, {0, 0, 8, 1}, col.data(), 32);
        Swizzle::LinearToVram(vram.data(), {Psm::T8H, 0, 1}, {0, 0, 8, 1}, idx.data(), 8);
        std::vector<uint8_t> col2(32), idx2(8);
        Swizzle::VramToLinear(vram.data(), {Psm::CT24, 0, 1}, {0, 0, 8, 1}, col2.data(), 32);
        Swizzle::VramToLinear(vram.data(), {Psm::T8H, 0, 1}, {0, 0, 8, 1}, idx2.data(), 8);
        CHECK(col2 == col && idx2 == idx, "T8H over CT24 co-page: colour and index coexist");
        std::vector<uint8_t> raw(32);
        Swizzle::VramToLinear(vram.data(), {Psm::CT32, 0, 1}, {0, 0, 8, 1}, raw.data(), 32);
        uint32_t w0; std::memcpy(&w0, raw.data(), 4);
        CHECK(w0 == 0x10ABCDEFu, "CT32 view of the shared word = 0x10ABCDEF, got %08x", w0);
    }
    std::printf("test_swizzle: %s (%d/%d checks passed)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
