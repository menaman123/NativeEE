// Vu0VisMicro: a retail VU0 visibility micro (entry 0x1C0, CMSAR0 0x38) over
// its captured 4 KB images and the full COP2 register file captured at the
// VCALLMSR (PCSX2 live). Leg 1 injects an E bit at pc 0x210 and pins the
// mid-program registers -- VI2 depends on the FMAC read stall re-timing an
// FTOI0 -> MTIR pair. Leg 2 runs the whole program and pins the output rows.
// Retail ground truth: the strongest correctness gate for the stall model.
// The interp-vs-cached leg returns with the cached tier.
#include <cstdio>
#include <cstring>
#include <string>

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

// Full captured COP2 register file at the VCALLMSR (raw IEEE bits).
const uint32_t kInVF[32][4] = {
    {0x00000000u, 0x00000000u, 0x00000000u, 0x3f800000u}, {0xc1ea05adu, 0xbfb47ae1u, 0xc2b1c99fu, 0x3f800000u},
    {0xffff8000u, 0x00000000u, 0x00000000u, 0x00000000u}, {0x00000000u, 0x00000000u, 0xffff8000u, 0x00000000u},
    {0xbe790995u, 0xbe790995u, 0xbf272cb5u, 0x3e3fad31u}, {0x421d862du, 0x421d862du, 0x421d862du, 0x421d862du},
    {0xbf61bd56u, 0xbecc1f06u, 0xbf08abd1u, 0xbf314601u}, {0x3d79e33eu, 0x436ee190u, 0x3d79e33eu, 0x40a840cfu},
    {0x4193d6b2u, 0xc1b5ff1au, 0x3f192cfeu, 0xc096f2abu}, {0xbf2e524fu, 0x3f2e524fu, 0x00000000u, 0x00000000u},
    {0x3f2f64f2u, 0x3f2f64f2u, 0x00000000u, 0x00000000u}, {0xc2679088u, 0x4319a309u, 0x42812ac4u, 0x4221be76u},
    {0xc6046fa5u, 0xc683887eu, 0x43906119u, 0xbe846fa5u}, {0xc6d9f3d1u, 0xc6a2fd3cu, 0x443f4bd7u, 0xbf2f64f3u},
    {0x4a9d3c0fu, 0x4aab0d77u, 0x49b3efceu, 0x433673dbu}, {0x0000b5fau, 0x41f229c0u, 0x454ef02fu, 0x00000000u},
    {0x435c71beu, 0x4315bf99u, 0x431cd2bau, 0x4345b8deu}, {0x3f2e524du, 0x3f2e524du, 0x00000000u, 0x00000000u},
    {0xbe9268cbu, 0x3f72c26au, 0x3dbdb192u, 0x3edcff16u}, {0xbe0006dbu, 0xbe0006dbu, 0xbf7dcec3u, 0x3f4b2368u},
    {0x3e9268cbu, 0x3f72c26au, 0x3dbdb191u, 0x3edcff15u}, {0x3f2f64f0u, 0x3f2f64f0u, 0x00000000u, 0x00000000u},
    {0x3e0006dbu, 0x3e0006dbu, 0x3f7dcec3u, 0x3f4b2368u}, {0xbd8225c0u, 0x00000000u, 0xbc5fcbb6u, 0x00000000u},
    {0x00000000u, 0x40c76d6du, 0x00000000u, 0x00000000u}, {0x3d1efd7eu, 0x00000000u, 0xbe38eb8fu, 0x00000000u},
    {0xbf2e524fu, 0xbe846fa5u, 0x3f2f64f2u, 0xbdcccccdu}, {0x3f3592e3u, 0xb05cb900u, 0x3f347692u, 0x00000000u},
    {0xc1e8a696u, 0xbfb47ae1u, 0xc2b3aa73u, 0x3f800000u}, {0x000000bcu, 0x000000eeu, 0x000000f6u, 0x00000005u},
    {0xfffffc0du, 0x00000000u, 0xfffffc0du, 0x00000000u}, {0x000003f3u, 0x00000000u, 0xfffffc0du, 0x000000f0u},
};
const uint16_t kInVI[16] = {0x0000, 0x0010, 0x0050, 0x0000, 0x0080, 0x00f0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

const uint32_t kMidVF29[4] = {0x3f000000u, 0x000000eeu, 0x3f000000u, 0x3f800000u};
const uint32_t kMidVF30[4] = {0x3f000000u, 0x00000000u, 0x3f000000u, 0x00000000u};
const uint32_t kMidVF31[4] = {0xbf000000u, 0x00000000u, 0x3f000000u, 0x000000f0u};
const uint32_t kMidVF1W = 0x3fd271cau;
const uint32_t kMidVF28W = 0x3ee76847u;
const uint16_t kMidVI1 = 0x0011;
const uint16_t kMidVI2 = 0x0001;
const uint32_t kEndVF[4][4] = {
    {0xc1ea05adu, 0xbfb47ae1u, 0xc2b1c99fu, 0x3f800000u},
    {0x000000beu, 0x000000d3u, 0x000000e7u, 0x0000008du},
    {0xffffe724u, 0x00000000u, 0xffffe724u, 0x00000000u},
    {0x000018dcu, 0x00000000u, 0xffffe724u, 0x000000f0u},
};

void SetupCapturedRegs(VUregs& r) {
    std::memset(&r, 0, sizeof(r));
    r.Reset();
    for (int reg = 0; reg < 32; ++reg)
        for (int k = 0; k < 4; ++k) r.VF[reg].I[k] = kInVF[reg][k];
    for (int i = 0; i < 16; ++i) r.VI[i] = kInVI[i];
    const uint32_t qbits = 0x3eb2d861u;
    std::memcpy(&r.Q, &qbits, 4);
    r.status_flag = 0x2;
    r.mac_flag = 0x50;
    r.clip_flag = 0;
    r.CMSAR0 = 0x38;
}

uint8_t g_code[4096];
uint8_t g_data[4096];

void InstallImages(VuUnit& vu, uint32_t ebit_at) {
    uint8_t code[4096];
    std::memcpy(code, g_code, sizeof(code));
    if (ebit_at) {  // cut the program: E bit (upper word bit 30) on the bundle at ebit_at
        uint32_t u;
        std::memcpy(&u, code + ebit_at + 4, 4);
        u |= 1u << 30;
        std::memcpy(code + ebit_at + 4, &u, 4);
    }
    vu.WriteMicroMem(0, code, sizeof(code));
    vu.WriteDataMem(0, g_data, sizeof(g_data));
}

void CheckLane(const VUregs& r, int reg, int k, uint32_t want, const char* leg) {
    if (r.VF[reg].I[k] != want) std::printf("  %s vf%d.%c = %08x retail %08x\n", leg, reg, "xyzw"[k], r.VF[reg].I[k], want);
    CHECK(r.VF[reg].I[k] == want);
}

}  // namespace

int main() {
    if (!ReadFixture("vu0_vis_code.bin", g_code, sizeof(g_code)) || !ReadFixture("vu0_vis_data.bin", g_data, sizeof(g_data))) {
        return PrivateFixtures::Skip(FixturePath("vu0_vis_code.bin"));
    }
    {  // Leg 1: mid-program cut at 0x210
        VuUnit vu(0);
        InstallImages(vu, 0x210);
        SetupCapturedRegs(vu.regs());
        VuInterpreterTier tier;
        tier.Run(vu, 0x1C0);
        const VUregs& r = vu.regs();
        if (r.VI[1] != kMidVI1 || r.VI[2] != kMidVI2) std::printf("  mid vi1=%04x vi2=%04x retail %04x %04x\n", r.VI[1], r.VI[2], kMidVI1, kMidVI2);
        CHECK(r.VI[1] == kMidVI1);
        CHECK(r.VI[2] == kMidVI2);  // MTIR must read the stalled FTOI0 result
        CheckLane(r, 1, 3, kMidVF1W, "mid");
        CheckLane(r, 28, 3, kMidVF28W, "mid");
        for (int k = 0; k < 4; ++k) {
            CheckLane(r, 29, k, kMidVF29[k], "mid");
            CheckLane(r, 30, k, kMidVF30[k], "mid");
            CheckLane(r, 31, k, kMidVF31[k], "mid");
        }
        CHECK(r.TPC == 0x220u);  // E bit at 0x210 + delay slot -> TPC = 0x220
        std::printf("  leg1: bundles=%llu cycles=%llu\n", static_cast<unsigned long long>(tier.bundles()),
                    static_cast<unsigned long long>(vu.cycle()));
    }
    {  // Leg 2: the full program
        VuUnit vu(0);
        InstallImages(vu, 0);
        SetupCapturedRegs(vu.regs());
        VuInterpreterTier tier;
        tier.Run(vu, 0x1C0);
        const VUregs& r = vu.regs();
        for (int i = 0; i < 4; ++i)
            for (int k = 0; k < 4; ++k) CheckLane(r, 28 + i, k, kEndVF[i][k], "end");
        CHECK((r.VF[31].I[3] & 0xFFFFu) == 0x00F0u);
        CHECK(tier.bundles() < 1000000);
        std::printf("  leg2: bundles=%llu cycles=%llu TPC=%04x\n", static_cast<unsigned long long>(tier.bundles()),
                    static_cast<unsigned long long>(vu.cycle()), r.TPC);
    }
    if (g_fail) {
        std::printf("Vu0VisMicro: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("Vu0VisMicro: ALL PASS\n");
    return 0;
}
