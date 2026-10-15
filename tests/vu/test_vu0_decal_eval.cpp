// Vu0DecalEval: a retail VU0 decal-evaluation micro (entry 0x1C0, CMSAR0
// 0x38) over its captured images; the output rows VF28/VF30/VF31 and Q are
// pinned to PCSX2 retail ground truth. Interp leg only; the cached leg
// returns with the cached tier.
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

void SetVF(VUregs& r, int reg, uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    r.VF[reg].I[0] = x;
    r.VF[reg].I[1] = y;
    r.VF[reg].I[2] = z;
    r.VF[reg].I[3] = w;
}

const uint32_t kRetailVF28[4] = {0xbf8a4082u, 0x3db8593bu, 0xc0519137u, 0x3f800000u};
const uint32_t kRetailVF30[4] = {0xfffff98bu, 0x00000000u, 0x00000000u, 0x00000000u};
const uint32_t kRetailVF31[4] = {0x00000675u, 0x00000000u, 0x00000000u, 0x000000f0u};
const uint32_t kRetailQ = 0x3eceb044u;

}  // namespace

int main() {
    uint8_t code[4096], data[4096];
    if (!ReadFixture("vu0_decal_code.bin", code, sizeof(code)) || !ReadFixture("vu0_decal_data.bin", data, sizeof(data))) {
        return PrivateFixtures::Skip(FixturePath("vu0_decal_code.bin"));
    }
    VuUnit vu(0);
    vu.WriteMicroMem(0, code, sizeof(code));
    vu.WriteDataMem(0, data, sizeof(data));
    VUregs& r = vu.regs();
    std::memset(&r, 0, sizeof(r));
    r.Reset();
    SetVF(r, 1, 0xbf8a4082u, 0x3db8593bu, 0xc0519137u, 0x40288889u);
    SetVF(r, 2, 0xffff8000u, 0x00000000u, 0x00000000u, 0x00000000u);
    SetVF(r, 3, 0x00000000u, 0x00000000u, 0xffff8000u, 0x00000000u);
    r.VI[1] = 0x18;
    r.CMSAR0 = 0x38;

    VuInterpreterTier tier;
    tier.Run(vu, 0x1C0);
    for (int k = 0; k < 4; ++k) {
        if (r.VF[28].I[k] != kRetailVF28[k]) std::printf("  vf28.%c = %08x retail %08x\n", "xyzw"[k], r.VF[28].I[k], kRetailVF28[k]);
        if (r.VF[30].I[k] != kRetailVF30[k]) std::printf("  vf30.%c = %08x retail %08x\n", "xyzw"[k], r.VF[30].I[k], kRetailVF30[k]);
        if (r.VF[31].I[k] != kRetailVF31[k]) std::printf("  vf31.%c = %08x retail %08x\n", "xyzw"[k], r.VF[31].I[k], kRetailVF31[k]);
        CHECK(r.VF[28].I[k] == kRetailVF28[k]);
        CHECK(r.VF[30].I[k] == kRetailVF30[k]);
        CHECK(r.VF[31].I[k] == kRetailVF31[k]);
    }
    uint32_t q;
    std::memcpy(&q, &r.Q, 4);
    if (q != kRetailQ) std::printf("  Q = %08x retail %08x\n", q, kRetailQ);
    CHECK(q == kRetailQ);
    CHECK(tier.bundles() < 1000000);
    std::printf("  bundles=%llu cycles=%llu TPC=%04x\n", static_cast<unsigned long long>(tier.bundles()),
                static_cast<unsigned long long>(vu.cycle()), r.TPC);
    if (g_fail) {
        std::printf("Vu0DecalEval: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("Vu0DecalEval: ALL PASS\n");
    return 0;
}
