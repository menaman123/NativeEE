// tests/gs/format/test_mip_chain.cpp -- MipChain properties: level sizes halve to
// a floor of 1 (GS_SKILL section 3.4.11), the MIPTBP field positions (TBPn 14 bits
// at 20*slot, TBWn 6 bits at 20*slot+14, 0 read as 1), the manual's 512x512
// PSMCT16 example table, MXL/MTBA field decode, mxl clamping, and the pack /
// unpack round trip of both registers.
#include <cstdint>
#include <cstdio>

#include "engine/gs/format/mip_chain.h"

using ps2r::gs::MipChain;
using ps2r::gs::Psm;

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

uint64_t Pair(uint32_t tbp, uint32_t tbw, uint32_t slot) {
    return (uint64_t(tbp) | (uint64_t(tbw) << 14)) << (20 * slot);
}

}  // namespace

int main() {
    // --- halving: every base size 2^0..2^10, every level 0..6
    for (uint32_t log2 = 0; log2 <= 10; log2++) {
        const uint32_t base = 1u << log2;
        for (uint32_t l = 0; l <= 6; l++) {
            const uint32_t s = MipChain::LevelSize(base, l);
            CHECK(s == (l >= log2 ? 1u : base >> l), "LevelSize(%u, %u) = %u", base, l, s);
            if (l) CHECK(s == (MipChain::LevelSize(base, l - 1) + 1) / 2 || s == 1, "level %u is half of level %u", l, l - 1);
        }
    }
    CHECK(MipChain::LevelSize(0, 0) == 1, "size 0 reads as 1");

    // --- field positions
    const uint64_t m1 = Pair(0x1234, 5, 0) | Pair(0x2345, 0, 1) | Pair(0x3FFF, 63, 2);
    CHECK(MipChain::PairTbp(m1, 0) == 0x1234 && MipChain::PairTbw(m1, 0) == 5, "slot 0");
    CHECK(MipChain::PairTbp(m1, 1) == 0x2345 && MipChain::PairTbw(m1, 1) == 1, "slot 1, TBW 0 read as 1");
    CHECK(MipChain::PairTbp(m1, 2) == 0x3FFF && MipChain::PairTbw(m1, 2) == 63, "slot 2, full-width fields");
    CHECK(MipChain::PairTbp(~0ull, 0) == 0x3FFF && MipChain::PairTbw(~0ull, 2) == 63, "masks");

    // --- TEX1 fields
    CHECK(MipChain::Mxl(0) == 0 && MipChain::Mxl(6u << 2) == 6 && MipChain::Mxl(7u << 2) == 7, "MXL bits 4:2");
    CHECK(!MipChain::Mtba(0) && MipChain::Mtba(1u << 9), "MTBA bit 9");
    CHECK(MipChain::Mxl(0xFFFFFFFFFFFFFFE3ull) == 0, "MXL ignores other bits");

    // --- the manual's example (section 3.4.11): 512x512 PSMCT16, level 0 at 7200 bw 8
    {
        const uint64_t mip1 = Pair(9248, 4, 0) | Pair(9760, 2, 1) | Pair(9888, 1, 2);
        const uint64_t mip2 = Pair(9920, 1, 0) | Pair(9928, 1, 1) | Pair(9930, 1, 2);
        const MipChain::Chain c = MipChain::Build(Psm::CT16, 7200, 8, 9, 9, uint64_t(6) << 2, mip1, mip2);
        CHECK(c.psm == Psm::CT16 && c.mxl == 6 && !c.mtba, "chain header");
        const uint32_t tbp[7] = {7200, 9248, 9760, 9888, 9920, 9928, 9930};
        const uint32_t tbw[7] = {8, 4, 2, 1, 1, 1, 1};
        const uint32_t dim[7] = {512, 256, 128, 64, 32, 16, 8};
        for (uint32_t l = 0; l <= 6; l++) {
            CHECK(c.level[l].tbp == tbp[l], "level %u tbp %u", l, c.level[l].tbp);
            CHECK(c.level[l].tbw == tbw[l], "level %u tbw %u", l, c.level[l].tbw);
            CHECK(c.level[l].w == dim[l] && c.level[l].h == dim[l], "level %u %ux%u", l, c.level[l].w, c.level[l].h);
        }
        CHECK(MipChain::LevelTbp(mip1, mip2, 3) == 9888 && MipChain::LevelTbp(mip1, mip2, 4) == 9920, "level 3 from MIPTBP1, 4 from MIPTBP2");
        CHECK(MipChain::LevelTbw(mip1, mip2, 1) == 4 && MipChain::LevelTbw(mip1, mip2, 6) == 1, "level tbw");
        CHECK(MipChain::PackMiptbp1(c) == mip1 && MipChain::PackMiptbp2(c) == mip2, "pack round trip");
    }

    // --- mxl clamp, level-0 tbw floor, non-square, MTBA reported
    {
        const MipChain::Chain c = MipChain::Build(Psm::T8, 0x100, 0, 8, 5, (7u << 2) | (1u << 9), 0, 0);
        CHECK(c.mxl == 6, "MXL 7 clamps to 6");
        CHECK(c.mtba, "MTBA reported");
        CHECK(c.level[0].tbw == 1, "level-0 tbw 0 -> 1");
        CHECK(c.level[0].w == 256 && c.level[0].h == 32, "256x32 base");
        CHECK(c.level[5].w == 8 && c.level[5].h == 1 && c.level[6].w == 4 && c.level[6].h == 1, "height floors at 1 while width halves");
        CHECK(c.level[1].tbp == 0 && c.level[1].tbw == 1, "zero registers: tbp 0, tbw 1");
    }
    {
        const MipChain::Chain c = MipChain::Build(Psm::CT32, 0x40, 2, 7, 7, 2u << 2, Pair(0x50, 1, 0) | Pair(0x54, 1, 1), 0);
        CHECK(c.mxl == 2 && c.level[2].tbp == 0x54 && c.level[2].w == 32, "MXL 2 chain");
        CHECK(MipChain::PackMiptbp1(c) == (Pair(0x50, 1, 0) | Pair(0x54, 1, 1)), "pack stops at mxl");
        CHECK(MipChain::PackMiptbp2(c) == 0, "no levels 4-6");
    }
    // --- random pack/unpack round trip
    {
        uint64_t s = 0x1234567ull;
        int bad = 0;
        for (int k = 0; k < 2000; k++) {
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            const uint64_t a = s & 0x0FFFFFFFFFFFFFFFull;   // bits 60-63 are outside the fields
            s = s * 6364136223846793005ull + 1442695040888963407ull;
            const uint64_t b = s & 0x0FFFFFFFFFFFFFFFull;
            const MipChain::Chain c = MipChain::Build(Psm::CT16, 0, 1, 9, 9, 6u << 2, a, b);
            // TBW 0 reads as 1, so the round trip is exact only when every stored TBW is non-zero.
            uint64_t a1 = a, b1 = b;
            for (uint32_t slot = 0; slot < 3; slot++) {
                if (((a >> (20 * slot + 14)) & 0x3F) == 0) a1 |= 1ull << (20 * slot + 14);
                if (((b >> (20 * slot + 14)) & 0x3F) == 0) b1 |= 1ull << (20 * slot + 14);
            }
            if (MipChain::PackMiptbp1(c) != a1 || MipChain::PackMiptbp2(c) != b1) bad++;
        }
        CHECK(bad == 0, "%d of 2000 random register pairs fail the round trip", bad);
    }

    std::printf("test_mip_chain: %s (%d/%d checks passed)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
