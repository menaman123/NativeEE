// tests/gs/format/test_pixel_convert.cpp -- PixelConvert properties: pack/unpack
// round-trips for every PSM (colour: identity on the representable set and the
// stored element is exactly the pixel bits; Z: identity on the Z width; index:
// identity and read-modify-write preserves the co-paged bits); element widths and
// the IMAGE-stream width read from the descriptor table; the alpha-domain scale
// vectors are the original's (0-128 -> 0-255, >= 128 -> 255; inverse 255 -> 128).
#include <cstdint>
#include <cstdio>

#include "engine/gs/format/pixel_convert.h"
#include "engine/gs/format/psm_layout.h"
#include "engine/gs/format/texa_expand.h"

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

uint32_t Lcg(uint32_t& s) { s = s * 1664525u + 1013904223u; return s; }

}  // namespace

int main() {
    // --- widths and classes, every PSM, against the descriptor table.
    for (int i = 0; i < ps2r::gs::kPsmCount; i++) {
        const PsmDescriptor& d = PsmLayout::Table()[i];
        CHECK(PixelConvert::StorageBitsPerPixel(d.psm) == d.storage_bits, "%s storage bits", d.name);
        if (d.storage_bits >= 8) CHECK(PixelConvert::BytesPerPixel(d.psm) == d.storage_bits / 8u, "%s bytes per pixel", d.name);
        const uint32_t stream = PixelConvert::StreamBitsPerPixel(d.psm);
        CHECK(stream == ((d.psm == Psm::CT24 || d.psm == Psm::Z24) ? 24u : d.storage_bits), "%s stream bits = %u", d.name, stream);
        CHECK(PixelConvert::IsZ(d.psm) == d.is_z, "%s IsZ", d.name);
        CHECK(PixelConvert::IsIndexed(d.psm) == d.is_indexed, "%s IsIndexed", d.name);
        const bool color = d.psm == Psm::CT32 || d.psm == Psm::CT24 || d.psm == Psm::CT16 || d.psm == Psm::CT16S;
        CHECK(PixelConvert::IsColor(d.psm) == color, "%s IsColor", d.name);
        CHECK((int)color + (int)d.is_z + (int)d.is_indexed == 1, "%s is exactly one of colour / Z / indexed", d.name);
        // The pixel mask has exactly the pixel's bit count and lies inside the element.
        const uint32_t mask = PixelConvert::ElementPixelMask(d.psm);
        const uint32_t bits = d.is_indexed ? d.index_bits : (d.psm == Psm::CT24 || d.psm == Psm::Z24) ? 24u : d.storage_bits;
        CHECK(__builtin_popcount(mask) == int(bits), "%s pixel mask 0x%08x has %d bits, want %u", d.name, mask, __builtin_popcount(mask), bits);
        if (d.storage_bits < 32) CHECK(mask < (1u << d.storage_bits), "%s mask exceeds the element", d.name);
    }
    CHECK(PixelConvert::BytesPerPixel(Psm::CT32) == 4 && PixelConvert::BytesPerPixel(Psm::CT24) == 4 &&
          PixelConvert::BytesPerPixel(Psm::CT16) == 2 && PixelConvert::BytesPerPixel(Psm::T8) == 1 &&
          PixelConvert::BytesPerPixel(Psm::Z24) == 4 && PixelConvert::BytesPerPixel(Psm::Z16S) == 2, "byte widths");
    CHECK(PixelConvert::StorageBitsPerPixel(Psm::T4) == 4, "T4 is a nibble");
    CHECK(PixelConvert::ElementPixelMask(Psm::CT24) == 0x00FFFFFFu && PixelConvert::ElementPixelMask(Psm::Z24) == 0x00FFFFFFu, "24-bit masks");
    CHECK(PixelConvert::ElementPixelMask(Psm::T8H) == 0xFF000000u && PixelConvert::ElementPixelMask(Psm::T4HL) == 0x0F000000u &&
          PixelConvert::ElementPixelMask(Psm::T4HH) == 0xF0000000u, "index-in-word masks (section 2.6.3)");
    CHECK(PixelConvert::ElementPixelMask(Psm::T8) == 0xFF && PixelConvert::ElementPixelMask(Psm::T4) == 0xF, "T8/T4 masks");
    CHECK(PixelConvert::ElementPixelMask(Psm::CT16S) == 0xFFFF && PixelConvert::ElementPixelMask(Psm::Z16) == 0xFFFF, "16-bit masks");

    // --- colour: pack(unpack(e)) == e on the pixel bits, unpack(pack(w)) == w on the
    // representable words; the element never carries bits outside the pixel mask.
    {
        uint32_t s = 7;
        for (int n = 0; n < 200000; n++) {
            const uint32_t e = Lcg(s);
            const uint32_t w32 = PixelConvert::UnpackColor(Psm::CT32, e);
            if (w32 != e || PixelConvert::PackColor(Psm::CT32, w32) != e) { CHECK(false, "CT32 identity at 0x%08x", e); break; }
            const uint32_t w24 = PixelConvert::UnpackColor(Psm::CT24, e);
            if (w24 != (e & 0x00FFFFFFu) || PixelConvert::PackColor(Psm::CT24, e) != (e & 0x00FFFFFFu)) { CHECK(false, "CT24 at 0x%08x", e); break; }
            // CT16: the 5-bit lanes of any word survive pack -> unpack -> pack.
            const uint32_t p16 = PixelConvert::PackColor(Psm::CT16, e);
            if (p16 > 0xFFFF || PixelConvert::PackColor(Psm::CT16, PixelConvert::UnpackColor(Psm::CT16, p16)) != p16) { CHECK(false, "CT16 pack/unpack at 0x%08x", e); break; }
            if (PixelConvert::PackColor(Psm::CT16S, e) != p16 || PixelConvert::UnpackColor(Psm::CT16S, p16) != PixelConvert::UnpackColor(Psm::CT16, p16)) { CHECK(false, "CT16S == CT16 values"); break; }
        }
        g_checks++;
        for (uint32_t h = 0; h < 65536; h++) {
            const uint32_t w = PixelConvert::UnpackColor(Psm::CT16, h);
            if (w != TexaExpand::Expand16To32(uint16_t(h)) || PixelConvert::PackColor(Psm::CT16, w) != h) { CHECK(false, "CT16 halfword 0x%04x", h); break; }
        }
        g_checks++;
        CHECK(PixelConvert::UnpackColor(Psm::CT24, 0xAB123456u) == 0x00123456u, "CT24 unpack drops the shared byte, alpha 0");
        CHECK(PixelConvert::PackColor(Psm::CT24, 0x80123456u) == 0x00123456u, "CT24 pack writes only the pixel bits");
        CHECK(PixelConvert::PackColor(Psm::CT16, 0x80FFFFFFu) == 0xFFFF, "opaque white -> 0xFFFF");
        CHECK(PixelConvert::PackColor(Psm::CT16, 0x7F000000u) == 0x0000, "alpha 0x7F -> A bit clear");
        CHECK(PixelConvert::UnpackColor(Psm::CT16, 0x8000) == 0xFF000000u, "A bit -> 0xFF (Expand16To32 convention)");
        CHECK(PixelConvert::R(0x04030201u) == 1 && PixelConvert::G(0x04030201u) == 2 && PixelConvert::B(0x04030201u) == 3 &&
              PixelConvert::A(0x04030201u) == 4 && PixelConvert::Rgba(1, 2, 3, 4) == 0x04030201u, "RGBA32 field layout (R low)");
    }

    // --- Z: identity on the Z width, upper byte of Z24 never written, Depth is its own type.
    {
        uint32_t s = 99;
        for (int n = 0; n < 200000; n++) {
            const uint32_t e = Lcg(s);
            if (PixelConvert::PackZ(Psm::Z32, PixelConvert::UnpackZ(Psm::Z32, e)) != e) { CHECK(false, "Z32 identity"); break; }
            if (PixelConvert::UnpackZ(Psm::Z24, e).value != (e & 0x00FFFFFFu) || PixelConvert::PackZ(Psm::Z24, PixelConvert::Depth{e}) != (e & 0x00FFFFFFu)) { CHECK(false, "Z24 at 0x%08x", e); break; }
            if (PixelConvert::UnpackZ(Psm::Z16, e).value != (e & 0xFFFFu) || PixelConvert::PackZ(Psm::Z16S, PixelConvert::Depth{e}) != (e & 0xFFFFu)) { CHECK(false, "Z16/Z16S at 0x%08x", e); break; }
            if (PixelConvert::DepthFromColorWord(e).value != e) { CHECK(false, "colour word as Z32"); break; }
        }
        g_checks++;
        CHECK(PixelConvert::PackZ(Psm::Z24, PixelConvert::Depth{0xFFFFFFFFu}) == 0x00FFFFFFu, "Z24 saturated value keeps 24 bits");
        CHECK(PixelConvert::PackZ(Psm::Z16, PixelConvert::Depth{0x00010000u}) == 0, "Z16 masks (no saturation here: Phase 6 decides at the store)");
    }

    // --- index: unpack(pack(e, i)) == i, and pack leaves every non-index bit of e alone.
    {
        const Psm idx[] = {Psm::T8, Psm::T4, Psm::T8H, Psm::T4HL, Psm::T4HH};
        uint32_t s = 5;
        for (Psm psm : idx) {
            const PsmDescriptor& d = PsmLayout::Describe(psm);
            const uint32_t mask = PixelConvert::ElementPixelMask(psm), imax = 1u << d.index_bits;
            bool ok = true;
            for (int n = 0; n < 20000 && ok; n++) {
                const uint32_t e = Lcg(s) & (d.storage_bits == 32 ? 0xFFFFFFFFu : ((1u << d.storage_bits) - 1u));
                for (uint32_t i = 0; i < imax; i += (imax == 256 ? 17 : 1)) {
                    const uint32_t p = PixelConvert::PackIndex(psm, e, i);
                    if (PixelConvert::UnpackIndex(psm, p) != i || (p & ~mask) != (e & ~mask)) { ok = false; break; }
                }
                if (PixelConvert::UnpackIndex(psm, e) != ((e & mask) >> d.index_shift)) ok = false;
            }
            CHECK(ok, "%s index pack/unpack", d.name);
        }
        CHECK(PixelConvert::UnpackIndex(Psm::T8H, 0xAB123456u) == 0xAB, "T8H index = byte 3");
        CHECK(PixelConvert::UnpackIndex(Psm::T4HL, 0xAB123456u) == 0xB, "T4HL index = bits 27:24");
        CHECK(PixelConvert::UnpackIndex(Psm::T4HH, 0xAB123456u) == 0xA, "T4HH index = bits 31:28");
        CHECK(PixelConvert::PackIndex(Psm::T8H, 0x00123456u, 0x7F) == 0x7F123456u, "T8H pack keeps the CT24 pixel");
        CHECK(PixelConvert::PackIndex(Psm::T4HL, 0xAB123456u, 0x1) == 0xA1123456u, "T4HL pack keeps T4HH's nibble");
        CHECK(PixelConvert::PackIndex(Psm::T4, 0x0, 0x1F) == 0xF, "index wider than the field is masked");
    }

    // --- alpha domain: the original's vectors (ScaleAlphaPS2ToGPU: a >= 128 ? 255 : a * 2;
    // EncodeRTToVRAM inverse: a >= 255 ? 128 : a / 2) for every byte, and the round trips.
    for (uint32_t a = 0; a < 256; a++) {
        const uint8_t gpu = PixelConvert::ScaleAlphaPs2ToGpu(uint8_t(a));
        CHECK(gpu == (a >= 128 ? 255u : a * 2u), "ScaleAlphaPs2ToGpu(%u) = %u", a, gpu);
        const uint8_t ps2 = PixelConvert::ScaleAlphaGpuToPs2(uint8_t(a));
        CHECK(ps2 == (a >= 255 ? 128u : a / 2u), "ScaleAlphaGpuToPs2(%u) = %u", a, ps2);
        if (a <= 128) CHECK(PixelConvert::ScaleAlphaGpuToPs2(gpu) == a, "PS2 -> GPU -> PS2 identity on 0..128 at %u", a);
        else CHECK(PixelConvert::ScaleAlphaGpuToPs2(gpu) == 128, "PS2 alpha > 1.0 (%u) saturates to 0x80 through the GPU domain", a);
    }
    CHECK(PixelConvert::ScaleAlphaPs2ToGpu(0x00) == 0 && PixelConvert::ScaleAlphaPs2ToGpu(0x40) == 0x80 &&
          PixelConvert::ScaleAlphaPs2ToGpu(0x7F) == 0xFE && PixelConvert::ScaleAlphaPs2ToGpu(0x80) == 0xFF &&
          PixelConvert::ScaleAlphaPs2ToGpu(0xFF) == 0xFF, "spot vectors 0/0x40/0x7F/0x80/0xFF");
    CHECK(PixelConvert::ScaleAlphaGpuToPs2(0xFF) == 0x80 && PixelConvert::ScaleAlphaGpuToPs2(0xFE) == 0x7F &&
          PixelConvert::ScaleAlphaGpuToPs2(0x01) == 0x00, "inverse spot vectors");

    std::printf("test_pixel_convert: %s (%d/%d checks passed)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
