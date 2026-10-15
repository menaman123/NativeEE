// tests/gs/format/test_texa_expand.cpp -- TexaExpand properties: 5-bit replicate
// widening is the exact inverse of truncation (every 5-bit lane, every channel);
// Expand16To32 / Truncate32To16 round-trip on the representable set and pin the
// A-bit convention; the TEXA truth table is GS_SKILL section 3.4.6 cell by cell
// (RGBA16 and RGB24, AEM 0/1, A bit 0/1, black and non-black); the register
// field decode; and the manual's shift formula is NOT what is written (the
// adjudicated deviation, EQUIVALENCE_PLAN 7.1 P2-EXP16-REPL, is visible here).
#include <cstdint>
#include <cstdio>

#include "engine/gs/format/texa_expand.h"

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

}  // namespace

int main() {
    // --- 5-bit lanes: replicate(truncate(x)) == x for every representable x, and
    // truncate(replicate(v)) == v for every 5-bit v (the widening is injective and
    // lands inside the truncation's fibre).
    for (uint32_t v = 0; v < 32; v++) {
        const uint8_t wide = TexaExpand::Replicate5To8(uint8_t(v));
        CHECK(wide == ((v << 3) | (v >> 2)), "Replicate5To8(%u) = %u", v, wide);
        CHECK(TexaExpand::Truncate8To5(wide) == v, "truncate(replicate(%u)) = %u", v, TexaExpand::Truncate8To5(wide));
        CHECK(TexaExpand::Replicate5To8(TexaExpand::Truncate8To5(wide)) == wide, "replicate(truncate(%u))", wide);
    }
    CHECK(TexaExpand::Replicate5To8(0) == 0, "0 -> 0");
    CHECK(TexaExpand::Replicate5To8(31) == 255, "31 -> 255 (replicate, not the manual's 248)");
    CHECK(TexaExpand::Replicate5To8(16) == 132, "16 -> 132");
    CHECK(TexaExpand::Replicate5To8(0xFF) == 255, "high bits of the input lane are ignored");
    // The manual's shift (section 3.4.6, out = in5 << 3) differs from what is written for
    // every v with a set bit in positions 3-4 -- the adjudicated deviation, counted here.
    {
        int differing = 0;
        for (uint32_t v = 0; v < 32; v++) if (TexaExpand::Replicate5To8(uint8_t(v)) != (v << 3)) differing++;
        CHECK(differing == 28, "replicate vs manual shift differ on %d of 32 lanes (expected 28: every v >= 4)", differing);
    }
    // Every 8-bit value truncates to the lane the manual's storage rule keeps (>> 3).
    for (uint32_t x = 0; x < 256; x++) CHECK(TexaExpand::Truncate8To5(uint8_t(x)) == (x >> 3), "Truncate8To5(%u)", x);

    // --- Expand16To32: all 65536 halfwords; channel placement, A-bit convention,
    // round trip through Truncate32To16 is the identity on the 16-bit side.
    for (uint32_t p = 0; p < 65536; p++) {
        const uint32_t w = TexaExpand::Expand16To32(uint16_t(p));
        const uint32_t r5 = p & 0x1F, g5 = (p >> 5) & 0x1F, b5 = (p >> 10) & 0x1F, a1 = p >> 15;
        const uint32_t want = TexaExpand::Replicate5To8(uint8_t(r5)) | (TexaExpand::Replicate5To8(uint8_t(g5)) << 8)
                            | (TexaExpand::Replicate5To8(uint8_t(b5)) << 16) | ((a1 ? 0xFFu : 0u) << 24);
        if (w != want) { CHECK(false, "Expand16To32(0x%04x) = 0x%08x want 0x%08x", p, w, want); break; }
        if (TexaExpand::Truncate32To16(w) != p) { CHECK(false, "Truncate32To16(Expand16To32(0x%04x)) = 0x%04x", p, TexaExpand::Truncate32To16(w)); break; }
    }
    g_checks++;  // the loop above counts as one property when it does not fail
    CHECK(TexaExpand::Expand16To32(0x0000) == 0x00000000u, "black, A=0");
    CHECK(TexaExpand::Expand16To32(0x8000) == 0xFF000000u, "black, A=1 -> alpha 0xFF (the A bit as a byte)");
    CHECK(TexaExpand::Expand16To32(0x7FFF) == 0x00FFFFFFu, "white, A=0");
    CHECK(TexaExpand::Expand16To32(0xFFFF) == 0xFFFFFFFFu, "white, A=1");
    CHECK(TexaExpand::Expand16To32(0x001F) == 0x000000FFu, "R lane -> low byte");
    CHECK(TexaExpand::Expand16To32(0x03E0) == 0x0000FF00u, "G lane -> byte 1");
    CHECK(TexaExpand::Expand16To32(0x7C00) == 0x00FF0000u, "B lane -> byte 2");
    // Truncate32To16: A bit = alpha >= 0x80 (PS2 domain; 0x80 = 1.0 sets it, 0x7F does not).
    CHECK(TexaExpand::Truncate32To16(0x80000000u) == 0x8000, "alpha 0x80 -> A bit");
    CHECK(TexaExpand::Truncate32To16(0x7F000000u) == 0x0000, "alpha 0x7F -> no A bit");
    CHECK(TexaExpand::Truncate32To16(0xFF000000u) == 0x8000, "alpha 0xFF -> A bit");
    CHECK(TexaExpand::Truncate32To16(0x00F8F8F8u) == 0x7FFF, "0xF8 per channel -> 31 per lane");
    CHECK(TexaExpand::Truncate32To16(0x00070707u) == 0x0000, "low 3 bits are dropped");
    // Truncation is idempotent through the pair (a 32-bit value quantized once stays put).
    for (uint32_t x = 0; x < 256; x++) {
        const uint32_t word = x | (x << 8) | (x << 16) | ((x & 1 ? 0x80u : 0x00u) << 24);
        const uint32_t once = TexaExpand::Expand16To32(TexaExpand::Truncate32To16(word));
        CHECK(TexaExpand::Expand16To32(TexaExpand::Truncate32To16(once)) == once, "quantize idempotent at %u", x);
    }

    // --- TEXA register decode: TA0 7:0, AEM 15, TA1 39:32 (other bits ignored).
    {
        const TexaExpand::Texa t = TexaExpand::DecodeTexaRegister(0x000000A5'0000807Bull);
        CHECK(t.ta0 == 0x7B, "TA0 = 0x%02x", t.ta0);
        CHECK(t.aem, "AEM set");
        CHECK(t.ta1 == 0xA5, "TA1 = 0x%02x", t.ta1);
        const TexaExpand::Texa u = TexaExpand::DecodeTexaRegister(0xFFFFFF00'FFFF7F00ull);
        CHECK(u.ta0 == 0x00 && !u.aem && u.ta1 == 0x00, "unused bits ignored, AEM clear");
    }

    // --- Section 3.4.6 truth table, RGBA16 (A bit x AEM x black) and RGB24 (AEM x black).
    {
        const TexaExpand::Texa aem0{0x11, 0x22, false}, aem1{0x11, 0x22, true};
        // A bit = 0
        CHECK(TexaExpand::Alpha16(false, false, aem0) == 0x11, "A=0 AEM=0 colour -> TA0");
        CHECK(TexaExpand::Alpha16(false, true,  aem0) == 0x11, "A=0 AEM=0 black  -> TA0");
        CHECK(TexaExpand::Alpha16(false, false, aem1) == 0x11, "A=0 AEM=1 colour -> TA0");
        CHECK(TexaExpand::Alpha16(false, true,  aem1) == 0x00, "A=0 AEM=1 black  -> 0");
        // A bit = 1: TA1 regardless of AEM and colour
        CHECK(TexaExpand::Alpha16(true, false, aem0) == 0x22, "A=1 AEM=0 colour -> TA1");
        CHECK(TexaExpand::Alpha16(true, true,  aem0) == 0x22, "A=1 AEM=0 black  -> TA1");
        CHECK(TexaExpand::Alpha16(true, false, aem1) == 0x22, "A=1 AEM=1 colour -> TA1");
        CHECK(TexaExpand::Alpha16(true, true,  aem1) == 0x22, "A=1 AEM=1 black  -> TA1 (AEM does not apply)");
        // RGB24
        CHECK(TexaExpand::Alpha24(false, aem0) == 0x11, "RGB24 AEM=0 colour -> TA0");
        CHECK(TexaExpand::Alpha24(true,  aem0) == 0x11, "RGB24 AEM=0 black  -> TA0");
        CHECK(TexaExpand::Alpha24(false, aem1) == 0x11, "RGB24 AEM=1 colour -> TA0");
        CHECK(TexaExpand::Alpha24(true,  aem1) == 0x00, "RGB24 AEM=1 black  -> 0");

        // ApplyTexa16 composes the widening with the table; RGB is Expand16To32's.
        const TexaExpand::Texa common{0x00, 0x80, false};   // the common setup: A=0 transparent, A=1 opaque
        CHECK(TexaExpand::ApplyTexa16(0x0000, common) == 0x00000000u, "black A=0 -> alpha TA0=0");
        CHECK(TexaExpand::ApplyTexa16(0x8000, common) == 0x80000000u, "black A=1 -> alpha TA1=0x80");
        CHECK(TexaExpand::ApplyTexa16(0x7FFF, common) == 0x00FFFFFFu, "white A=0 -> TA0");
        CHECK(TexaExpand::ApplyTexa16(0xFFFF, common) == 0x80FFFFFFu, "white A=1 -> TA1");
        CHECK(TexaExpand::ApplyTexa16(0x0001, aem1) == 0x11000008u, "R=1 (not black) A=0 AEM=1 -> TA0, R widened to 8");
        CHECK(TexaExpand::ApplyTexa16(0x0000, aem1) == 0x00000000u, "black A=0 AEM=1 -> 0");
        CHECK(TexaExpand::ApplyTexa16(0x8000, aem1) == 0x22000000u, "black A=1 AEM=1 -> TA1");
        // "black" for RGBA16 means all fifteen colour bits clear, not the widened bytes.
        for (uint32_t p = 1; p < 0x8000; p++)
            if (TexaExpand::ApplyTexa16(uint16_t(p), aem1) >> 24 != 0x11) { CHECK(false, "non-black 0x%04x lost TA0 under AEM", p); break; }
        g_checks++;
        // ApplyTexa24: RGB kept, bits 31:24 of the input ignored, alpha from the table.
        CHECK(TexaExpand::ApplyTexa24(0x00123456u, common) == 0x00123456u, "RGB24 -> TA0 (0)");
        CHECK(TexaExpand::ApplyTexa24(0xAB123456u, aem0) == 0x11123456u, "upper input byte ignored, alpha TA0");
        CHECK(TexaExpand::ApplyTexa24(0xAB000000u, aem1) == 0x00000000u, "black under AEM -> 0 even with junk in bits 31:24");
        CHECK(TexaExpand::ApplyTexa24(0x00000001u, aem1) == 0x11000001u, "R=1 is not black");
        CHECK(TexaExpand::ApplyTexa24(0x00000000u, aem0) == 0x11000000u, "black, AEM=0 -> TA0");
        // Alpha bytes above 0x80 pass through (TEXA is an 8-bit field; > 1.0 is legal).
        const TexaExpand::Texa big{0xFF, 0xC0, false};
        CHECK(TexaExpand::ApplyTexa24(0x00000000u, big) == 0xFF000000u, "TA0 = 0xFF passes");
        CHECK(TexaExpand::ApplyTexa16(0x8000, big) == 0xC0000000u, "TA1 = 0xC0 passes");
    }

    std::printf("test_texa_expand: %s (%d/%d checks passed)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
