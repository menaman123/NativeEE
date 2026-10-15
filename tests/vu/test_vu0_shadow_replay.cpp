// Vu0ShadowReplay: a retail VU0 shadow-volume micro (entries 0xA08 / 0xB90)
// over its captured 4 KB code + data images, run by the interpreter tier on
// five register/data variants, fresh and as a 12-step persistent sequence.
// Pinned: the captured PASS vector's FMAND verdict VI02 == 0 (PCSX2), the
// verdict varies across the variants, every run ends at an E bit within the
// cycle cap, and the persistent sequence reproduces the fresh verdicts. The
// interp-vs-cached legs return with the cached tier. Prints the GT dump in
// the reference's format for diffing against the OLD test binary.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/vu/tiers/vu_interpreter_tier.h"
#include "engine/vu/vu_unit.h"
#include "tests/lib/private_fixtures.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

namespace {

// The retail VU0 captures are private fixtures (cmake/Ps2rPrivateFixtures.cmake):
// $NATIVEEE_PRIVATE_FIXTURES/tests/vu/fixtures/<name>; absent -> the test SKIPs (77).
std::string FixturePath(const char* name) {
    return PrivateFixtures::Path(std::string("tests/vu/fixtures/") + name);
}

bool ReadFixture(const char* name, uint8_t* dst, size_t size) {
    FILE* f = std::fopen(FixturePath(name).c_str(), "rb");
    if (!f) return false;
    const size_t n = std::fread(dst, 1, size, f);
    std::fclose(f);
    return n == size;
}

void SetVF(VUregs& r, int reg, uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    r.VF[reg].I[0] = x;
    r.VF[reg].I[1] = y;
    r.VF[reg].I[2] = z;
    r.VF[reg].I[3] = w;
}

// Captured COP2 register file at a passing call (entry 0xA08, VI01 = 0).
void SetupCapturedRegs(VUregs& r) {
    std::memset(&r, 0, sizeof(r));
    r.Reset();
    SetVF(r, 2, 0x46f5f742u, 0x46c99d17u, 0xc46d164cu, 0x3f5934c2u);
    SetVF(r, 3, 0x3f800000u, 0xb448dc07u, 0x3448dc07u, 0x00000000u);
    SetVF(r, 4, 0x3448dc07u, 0x3f800000u, 0x291da3e6u, 0x00000000u);
    SetVF(r, 5, 0xb448dc07u, 0x00000000u, 0x3f800000u, 0x00000000u);
    SetVF(r, 6, 0xc083d611u, 0x3f916a91u, 0xc1a532d3u, 0x3f800000u);
    SetVF(r, 7, 0x40065aa1u, 0x00000000u, 0x00000000u, 0x3f800000u);
    r.VI[1] = 0;
    r.VI[5] = 0xF0;
}

// Live-captured FAIL vector (entry 0xA08, vi01 = 19) + the record the micro
// loads from data qw19..qw37.
const uint32_t kFailVF[6][4] = {
    {0x41f41a36u, 0x41c53475u, 0x4204cd82u, 0x3f800000u},
    {0x3f800000u, 0x00000000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x3f800000u, 0x00000000u, 0x00000000u},
    {0x00000000u, 0x00000000u, 0x3f800000u, 0x00000000u},
    {0x430b5ecau, 0x42841df3u, 0x42813030u, 0x3f800000u},
    {0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u},
};
const uint32_t kFailRec[19][4] = {
    {0x3f737f79u, 0x3e951fd5u, 0x3dcc9d52u, 0x3ed8494bu}, {0x3df53857u, 0x3df53857u, 0x3f7d69ccu, 0x3f4ce340u},
    {0x3e91ac47u, 0x3f72f99eu, 0x3dce1242u, 0x3ed9d385u}, {0xbf737f79u, 0x3e951fd5u, 0xbdcc9d52u, 0xbed8494bu},
    {0xbdf53857u, 0xbdf53857u, 0xbf7d69ccu, 0x3f4ce340u}, {0xbe91ac47u, 0x3f72f99eu, 0x3dce1242u, 0x3ed9d385u},
    {0xbfdb8e68u, 0x421d076au, 0x4119c935u, 0x41a0d5eeu}, {0x3f2ec107u, 0x3f2ec107u, 0x00000000u, 0x00000000u},
    {0x3e7daa63u, 0x3e7daa63u, 0x00000000u, 0x00000000u}, {0x3f2fff8du, 0x3f2fff8du, 0x00000000u, 0x00000000u},
    {0xbf2ec107u, 0x3f2ec107u, 0x00000000u, 0x00000000u}, {0xbe7daa63u, 0x3e7daa63u, 0x00000000u, 0x00000000u},
    {0x3f2fff8du, 0xbf2fff8du, 0x00000000u, 0x00000000u}, {0x421af08fu, 0x44b6a547u, 0x00000000u, 0x00000000u},
    {0x412e52a9u, 0x4083cca8u, 0xc230bdf2u, 0x3f800000u}, {0xc683e9e5u, 0xc6a2f3c0u, 0x443ee000u, 0xbf2ec10au},
    {0xc5fdaa67u, 0xc680ecdbu, 0x438a9000u, 0xbe7daa67u}, {0x46da892cu, 0x46a41cc3u, 0xc4404000u, 0x3f2fff90u},
    {0x49b16a1fu, 0x49955724u, 0x49c78300u, 0x421b56f8u},
};

void ApplyVariant(VUregs& r, uint32_t variant) {
    switch (variant) {
        case 0: break;
        case 1:
            for (int reg = 2; reg <= 7; ++reg)
                SetVF(r, reg, kFailVF[reg - 2][0], kFailVF[reg - 2][1], kFailVF[reg - 2][2], kFailVF[reg - 2][3]);
            r.VI[1] = 19;
            break;
        case 2:
            for (int reg = 2; reg <= 7; ++reg)
                SetVF(r, reg, kFailVF[reg - 2][0], kFailVF[reg - 2][1], kFailVF[reg - 2][2], kFailVF[reg - 2][3]);
            break;
        case 3:
            r.VF[6].F[0] += 5000.0f;
            r.VF[6].F[2] -= 5000.0f;
            break;
        default:
            r.VF[7].F[0] = 0.25f;
            r.VF[6].F[1] -= 500.0f;
            break;
    }
}

uint8_t g_code[4096];
uint8_t g_data[4096];

void LoadImages(VuUnit& vu) {
    vu.WriteMicroMem(0, g_code, sizeof(g_code));
    vu.WriteDataMem(0, g_data, sizeof(g_data));
}

void WriteVariantData(VuUnit& vu, uint32_t variant) {
    if (variant != 1) return;
    vu.WriteDataMem(19 * 16, reinterpret_cast<const uint8_t*>(kFailRec), sizeof(kFailRec));
}

constexpr uint64_t kCycleCap = 200000;

// Fresh unit per run; returns the unit's register file after the run.
VUregs RunFresh(uint32_t entry, uint32_t variant, uint64_t* cycles, uint64_t* bundles) {
    VuUnit vu(0);
    LoadImages(vu);
    SetupCapturedRegs(vu.regs());
    ApplyVariant(vu.regs(), variant);
    WriteVariantData(vu, variant);
    VuInterpreterTier tier(kCycleCap);
    tier.Run(vu, entry);
    if (cycles) *cycles = vu.cycle();
    if (bundles) *bundles = tier.bundles();
    return vu.regs();
}

void RunEntryAllVariantsFresh(uint32_t entry, const char* name, uint16_t verdicts[5]) {
    bool varies = false;
    for (uint32_t v = 0; v < 5; ++v) {
        uint64_t cycles = 0, bundles = 0;
        const VUregs r = RunFresh(entry, v, &cycles, &bundles);
        verdicts[v] = r.VI[2];
        std::printf("  %s v%u: vi02=%04x TPC=%04x cycles=%llu bundles=%llu\n", name, v, r.VI[2], r.TPC,
                    static_cast<unsigned long long>(cycles), static_cast<unsigned long long>(bundles));
        CHECK(bundles < kCycleCap);   // ended at an E bit, not the cap
        if (v > 0 && verdicts[v] != verdicts[0]) varies = true;
    }
    CHECK(varies);
}

}  // namespace

int main() {
    if (!ReadFixture("vu0_shadow_code.bin", g_code, sizeof(g_code)) ||
        !ReadFixture("vu0_shadow_data.bin", g_data, sizeof(g_data))) {
        return PrivateFixtures::Skip(FixturePath("vu0_shadow_code.bin"));
    }
    static const uint8_t kHead[8] = {0xff, 0x0f, 0x00, 0x5a, 0xff, 0x02, 0x00, 0x20};
    CHECK(std::memcmp(g_code, kHead, 8) == 0);

    uint16_t verdicts_a[5], verdicts_b[5];
    RunEntryAllVariantsFresh(0xA08, "shadowA", verdicts_a);
    RunEntryAllVariantsFresh(0xB90, "shadowB", verdicts_b);

    // Persistent 12-step sequence: one unit, registers refreshed per step;
    // the verdicts must match the fresh runs (no carried state leaks).
    {
        VuUnit vu(0);
        LoadImages(vu);
        SetupCapturedRegs(vu.regs());
        VuInterpreterTier tier(kCycleCap);
        static const struct { uint32_t entry, variant; } seq[] = {
            {0xB90, 0}, {0xA08, 0}, {0xB90, 1}, {0xA08, 1}, {0xA08, 2}, {0xB90, 2},
            {0xA08, 3}, {0xB90, 3}, {0xA08, 4}, {0xB90, 4}, {0xA08, 0}, {0xB90, 0},
        };
        int step = 0;
        for (const auto& s : seq) {
            VUregs tmp;
            SetupCapturedRegs(tmp);
            ApplyVariant(tmp, s.variant);
            for (int reg = 2; reg <= 7; ++reg) vu.regs().VF[reg] = tmp.VF[reg];
            vu.regs().VI[1] = tmp.VI[1];
            WriteVariantData(vu, s.variant);
            const uint64_t b0 = tier.bundles();
            tier.Run(vu, s.entry);
            CHECK(tier.bundles() - b0 < kCycleCap);
            const uint16_t want = (s.entry == 0xA08) ? verdicts_a[s.variant] : verdicts_b[s.variant];
            if (vu.regs().VI[2] != want)
                std::printf("  seq step %d e0x%X v%u: vi02=%04x fresh=%04x\n", step, s.entry, s.variant, vu.regs().VI[2], want);
            CHECK(vu.regs().VI[2] == want);
            ++step;
        }
    }

    // The captured PASS vector: VI02 == 0 (PCSX2 ground truth).
    {
        const VUregs r = RunFresh(0xA08, 0, nullptr, nullptr);
        CHECK(r.VI[2] == 0);
        std::printf("GT dump (interp, captured vector, entry 0xA08):\n");
        for (int reg = 26; reg <= 30; ++reg)
            std::printf("  vf%02d = %08x %08x %08x %08x\n", reg, r.VF[reg].I[0], r.VF[reg].I[1], r.VF[reg].I[2], r.VF[reg].I[3]);
        uint32_t q;
        std::memcpy(&q, &r.Q, 4);
        std::printf("  Q=%08x mac=%04x status=%04x vi02=%04x\n", q, r.mac_flag, r.status_flag, r.VI[2]);
    }

    if (g_fail) {
        std::printf("Vu0ShadowReplay: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("Vu0ShadowReplay: ALL PASS\n");
    return 0;
}
