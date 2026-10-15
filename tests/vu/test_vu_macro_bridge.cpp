// VuMacroBridge: the EE COP2 <-> VuUnit hand-over behind the generated code's
// VCALLMS / VCALLMSR / CMSAR1 seams. (1) unbound -> the park sink receives
// vu_micro_absent with the Phase-3 detail text; (2) bound: the SDK sin/cos
// program at 0xF0 through Vu0Macro::Execute lands VF31 in ctx.vuRegs (the
// LibVu0SinCosPin value), TPC 0x1C0, and the whole 608-byte block equals the
// unit's; (3) the retail VU0 shadow micro (fixtures, entries 0xA08 / 0xB90,
// five variants) through Vu0Macro::Execute reproduces a direct VuUnit run
// byte-for-byte and the captured PASS verdict VI02 == 0 (the copy-in/out is
// transparent); (4) ResumeIfWaiting on a non-suspended unit changes nothing;
// (5) Vu1Window::Execute runs a one-bundle E-bit program and advances the
// VU1 clock. The census counters follow each call.
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/recomp_abi/host_app/vu.h"
#include "engine/vu/macro/vu_macro_bridge.h"
#include "engine/vu/vu0/lib_vu0_sin_cos.h"
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

struct ParkUnwind {};

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

void PutBundle(uint8_t* micro, uint32_t pc, uint32_t lower, uint32_t upper) {
    std::memcpy(micro + pc, &lower, 4);
    std::memcpy(micro + pc + 4, &upper, 4);
}

void SetVF(VUregs& r, int reg, uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    r.VF[reg].I[0] = x;
    r.VF[reg].I[1] = y;
    r.VF[reg].I[2] = z;
    r.VF[reg].I[3] = w;
}

// The Vu0ShadowReplay captured COP2 file at a passing call (entry 0xA08).
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

void LoadShadow(VuUnit& vu, uint32_t variant) {
    vu.WriteMicroMem(0, g_code, sizeof(g_code));
    vu.WriteDataMem(0, g_data, sizeof(g_data));
    if (variant == 1) vu.WriteDataMem(19 * 16, reinterpret_cast<const uint8_t*>(kFailRec), sizeof(kFailRec));
}

bool SameRegs(const VUregs& a, const VUregs& b) { return std::memcmp(&a, &b, sizeof(VUregs)) == 0; }

}  // namespace

int main() {
    Vu0Macro macro;
    Vu1Window window;

    // (1) Unbound: both seams park with the Phase-3 reason and detail.
    {
        VuMacroBridge::Unbind();
        GuestPark::SetSink([](const ParkRecord&) { throw ParkUnwind{}; });
        CpuContext ctx{};
        ctx.cpuRegs.pc = 0x0018b918u;
        bool unwound = false;
        try {
            macro.Execute(ctx, 0xF0);
        } catch (const ParkUnwind&) {
            unwound = true;
        }
        CHECK(unwound);
        CHECK(std::string(GuestPark::Last().reason) == "vu_micro_absent");
        CHECK(GuestPark::Last().pc == 0x0018b918u);
        CHECK(std::string(GuestPark::Last().detail) == "vcallms micro pc 0xf0 (VU micro memory + interpreter absent)");
        unwound = false;
        try {
            window.Execute(ctx, 0x790);
        } catch (const ParkUnwind&) {
            unwound = true;
        }
        CHECK(unwound);
        CHECK(std::string(GuestPark::Last().detail) == "cmsar1 start micro pc 0x790 (VU micro memory + interpreter absent)");
        // ResumeIfWaiting unbound: nothing waiting, nothing touched, no park.
        CpuContext before = ctx;
        macro.ResumeIfWaiting(ctx);
        CHECK(std::memcmp(&before, &ctx, sizeof(CpuContext)) == 0);
        GuestPark::SetSink(nullptr);
        CHECK(!VuMacroBridge::Bound());
    }

    // (2) Bound: the SDK sin/cos program at 0xF0 through the seam.
    {
        VuUnit vu0(0), vu1(1);
        VuMacroBridge::Bind(&vu0, &vu1);
        VuMacroBridge::ResetStats();
        CHECK(VuMacroBridge::Bound());
        uint8_t* micro = vu0.state().micro;
        for (int i = 0; i < 3; ++i)
            PutBundle(micro, LibVu0SinCos::kEntryPc + 8u * static_cast<uint32_t>(i), LibVu0SinCos::kFingerprint[i][0],
                      LibVu0SinCos::kFingerprint[i][1]);
        vu0.BumpMicroEpoch();
        CpuContext ctx{};
        ctx.vuRegs.Reset();
        ctx.vuRegs.VF[20].F[0] = 0.5f;
        ctx.vuRegs.VF[20].F[1] = 0.0f;
        ctx.vuRegs.VF[20].F[2] = -2.0f;
        ctx.vuRegs.VF[20].F[3] = 0.0f;
        macro.Execute(ctx, 0xF0);
        CHECK(ctx.vuRegs.VF[31].I[0] == 0x3ef57743u);
        CHECK(ctx.vuRegs.VF[31].I[1] == 0x3f60a940u);
        CHECK(ctx.vuRegs.VF[31].I[2] == 0xbf68c7b7u);
        CHECK(ctx.vuRegs.VF[31].I[3] == 0xbed51132u);
        CHECK(ctx.vuRegs.TPC == LibVu0SinCos::kExitTpc);
        CHECK(vu0.regs().TPC == 0x1C0u);
        CHECK(SameRegs(ctx.vuRegs, vu0.regs()));  // the whole 608 bytes = the copy-back
        CHECK(VuMacroBridge::Stats().vu0_calls == 1);
        std::printf("  sincos via Vu0Macro: %08x %08x %08x %08x TPC=%x\n", ctx.vuRegs.VF[31].I[0], ctx.vuRegs.VF[31].I[1],
                    ctx.vuRegs.VF[31].I[2], ctx.vuRegs.VF[31].I[3], ctx.vuRegs.TPC);

        // (4) ResumeIfWaiting on a non-suspended unit: byte-identical ctx, no count.
        CHECK(!vu0.state().suspended);
        CpuContext before = ctx;
        macro.ResumeIfWaiting(ctx);
        CHECK(std::memcmp(&before, &ctx, sizeof(CpuContext)) == 0);
        CHECK(VuMacroBridge::Stats().vu0_park_resumes == 0);
        VuMacroBridge::Unbind();
    }

    // (3) The retail shadow micro through the seam == a direct unit run. The capture is a
    // private fixture: absent, this section is skipped and the run reports SKIP (77) unless an
    // earlier section already failed.
    const bool shadow_fixture = ReadFixture("vu0_shadow_code.bin", g_code, sizeof(g_code)) &&
                                ReadFixture("vu0_shadow_data.bin", g_data, sizeof(g_data));
    if (!shadow_fixture) {
        PrivateFixtures::Skip(FixturePath("vu0_shadow_code.bin"));
    } else {
        static const uint32_t kEntries[2] = {0xA08, 0xB90};
        uint16_t verdicts[2][5];
        for (int e = 0; e < 2; ++e) {
            for (uint32_t v = 0; v < 5; ++v) {
                // Reference: the unit driven directly.
                VuUnit ref(0);
                LoadShadow(ref, v);
                SetupCapturedRegs(ref.regs());
                ApplyVariant(ref.regs(), v);
                ref.Execute(kEntries[e]);
                // The seam: the same inputs in ctx.vuRegs, a fresh bound unit.
                VuUnit vu0(0), vu1(1);
                LoadShadow(vu0, v);
                VuMacroBridge::Bind(&vu0, &vu1);
                CpuContext ctx{};
                SetupCapturedRegs(ctx.vuRegs);
                ApplyVariant(ctx.vuRegs, v);
                macro.Execute(ctx, kEntries[e]);
                CHECK(SameRegs(ctx.vuRegs, ref.regs()));
                CHECK(SameRegs(ctx.vuRegs, vu0.regs()));
                CHECK(vu0.cycle() == ref.cycle());
                CHECK(std::memcmp(vu0.state().data, ref.state().data, 4096) == 0);
                verdicts[e][v] = ctx.vuRegs.VI[2];
                std::printf("  shadow e0x%X v%u: vi02=%04x TPC=%04x cycles=%llu\n", kEntries[e], v, ctx.vuRegs.VI[2],
                            ctx.vuRegs.TPC, static_cast<unsigned long long>(vu0.cycle()));
                VuMacroBridge::Unbind();
            }
            bool varies = false;
            for (uint32_t v = 1; v < 5; ++v)
                if (verdicts[e][v] != verdicts[e][0]) varies = true;
            CHECK(varies);
        }
        CHECK(verdicts[0][0] == 0);  // the captured PASS vector (PCSX2 ground truth)
    }

    // (5) Vu1Window::Execute: one bundle with the E bit advances the VU1 clock.
    {
        VuUnit vu0(0), vu1(1);
        VuMacroBridge::Bind(&vu0, &vu1);
        VuMacroBridge::ResetStats();
        // upper: NOP with the E bit (bit 30); lower: NOP.
        uint8_t bundle[8];
        const uint32_t lower = 0x8000033Cu;               // NOP (lower)
        const uint32_t upper = 0x400002FFu;               // NOP (upper) | E
        std::memcpy(bundle, &lower, 4);
        std::memcpy(bundle + 4, &upper, 4);
        vu1.WriteMicroMem(0x100, bundle, 8);
        const uint32_t upper_nop = 0x000002FFu;
        std::memcpy(bundle + 4, &upper_nop, 4);
        vu1.WriteMicroMem(0x108, bundle, 8);              // the bundle after E still executes
        const uint64_t c0 = vu1.cycle();
        CpuContext ctx{};
        ctx.vuRegs.CMSAR1 = 0x100 / 8;
        window.Execute(ctx, (ctx.vuRegs.CMSAR1 & 0x7FFFu) * 8u);
        CHECK(vu1.cycle() > c0);
        CHECK(vu1.regs().TPC == 0x110u);
        CHECK(VuMacroBridge::Stats().vu1_kicks == 1);
        std::printf("  vu1 window: cycle %llu -> %llu TPC=%x\n", static_cast<unsigned long long>(c0),
                    static_cast<unsigned long long>(vu1.cycle()), vu1.regs().TPC);
        VuMacroBridge::Unbind();
    }

    if (g_fail) {
        std::printf("VuMacroBridge: %d failure(s)\n", g_fail);
        return 1;
    }
    if (!shadow_fixture) {
        std::printf("VuMacroBridge: SKIP (sections 1, 2, 4, 5 passed; the retail shadow micro needs the private fixture)\n");
        return PrivateFixtures::kSkip;
    }
    std::printf("VuMacroBridge: ALL PASS\n");
    return 0;
}
