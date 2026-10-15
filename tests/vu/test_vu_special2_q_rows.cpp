// VuSpecial2QRows: the special-2 upper table's Q column (func 0x3C) in rows
// 7/8/9 -- MULAq (ACC = fs * Q), ADDAq (ACC = fs + Q), SUBAq (ACC = fs - Q) --
// executed through the interpreter tier on one-bundle E-bit programs with
// hand-computed operands (exact in binary), the dest mask honoured (lanes
// outside dest keep the incoming ACC), and the ABS / MADDAq / MSUBAq column
// (func 0x3D) still decoding as before. Before the fix the three Q-column rows
// ran the 0x3D column's bodies (ABS wrote nothing with ft = 0; MADDAq / MSUBAq
// folded the incoming ACC in), so every check below was red.
#include <cstdio>
#include <cstring>

#include "engine/vu/tiers/vu_interpreter_tier.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_unit.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

namespace {

constexpr uint32_t kLowerNop = 0x8000033Cu;
constexpr uint32_t kUpperNop = 0x000002FFu;
constexpr uint32_t kEBit = 0x40000000u;

// Upper word: dest[24:21] ft[20:16] fs[15:11] fd/op[10:6] func[5:0].
uint32_t Upper(uint32_t func, uint32_t op, uint32_t fs, uint32_t ft, uint32_t dest) {
    return (dest << 21) | (ft << 16) | (fs << 11) | (op << 6) | func;
}

void PutBundle(VuUnit& vu, uint32_t pc, uint32_t lower, uint32_t upper) {
    uint8_t b[8];
    std::memcpy(b, &lower, 4);
    std::memcpy(b + 4, &upper, 4);
    vu.WriteMicroMem(pc, b, 8);
}

// One bundle with the E bit, then the delay-slot NOP bundle.
void RunOne(VuUnit& vu, uint32_t upper) {
    PutBundle(vu, 0, kLowerNop, upper | kEBit);
    PutBundle(vu, 8, kLowerNop, kUpperNop);
    VuInterpreterTier tier;
    tier.Run(vu, 0);
}

void SetVf(VuUnit& vu, int n, float x, float y, float z, float w) {
    vu.regs().VF[n].x = x;
    vu.regs().VF[n].y = y;
    vu.regs().VF[n].z = z;
    vu.regs().VF[n].w = w;
}

void SetAcc(VuUnit& vu, float x, float y, float z, float w) {
    vu.regs().ACC.x = x;
    vu.regs().ACC.y = y;
    vu.regs().ACC.z = z;
    vu.regs().ACC.w = w;
}

bool AccIs(const VuUnit& vu, float x, float y, float z, float w) {
    const VR_reg& a = vu.regs().ACC;
    return VuFloat::F2Bits(a.x) == VuFloat::F2Bits(x) && VuFloat::F2Bits(a.y) == VuFloat::F2Bits(y) &&
           VuFloat::F2Bits(a.z) == VuFloat::F2Bits(z) && VuFloat::F2Bits(a.w) == VuFloat::F2Bits(w);
}

void PrintAcc(const char* what, const VuUnit& vu) {
    const VR_reg& a = vu.regs().ACC;
    std::printf("  %-28s ACC = %g %g %g %g\n", what, a.x, a.y, a.z, a.w);
}

}  // namespace

int main() {
    // fs = {2, 4, -6, 8}, Q = 0.5, incoming ACC = {100, 200, 300, 400}.
    {   // MULAq xyzw: ACC = fs * Q = {1, 2, -3, 4}
        VuUnit vu(1);
        SetVf(vu, 1, 2.0f, 4.0f, -6.0f, 8.0f);
        vu.regs().Q = 0.5f;
        SetAcc(vu, 100.0f, 200.0f, 300.0f, 400.0f);
        RunOne(vu, Upper(0x3C, 0x07, 1, 0, 0xF));
        PrintAcc("MULAq.xyzw vf1 Q=0.5", vu);
        CHECK(AccIs(vu, 1.0f, 2.0f, -3.0f, 4.0f));
        CHECK(vu.regs().VF[0].x == 0.0f && vu.regs().VF[0].w == 1.0f);   // ft = vf0 untouched (not ABS)
    }
    {   // ADDAq xyzw: ACC = fs + Q = {2.5, 4.5, -5.5, 8.5}
        VuUnit vu(1);
        SetVf(vu, 1, 2.0f, 4.0f, -6.0f, 8.0f);
        vu.regs().Q = 0.5f;
        SetAcc(vu, 100.0f, 200.0f, 300.0f, 400.0f);
        RunOne(vu, Upper(0x3C, 0x08, 1, 0, 0xF));
        PrintAcc("ADDAq.xyzw vf1 Q=0.5", vu);
        CHECK(AccIs(vu, 2.5f, 4.5f, -5.5f, 8.5f));
    }
    {   // SUBAq xyzw: ACC = fs - Q = {1.5, 3.5, -6.5, 7.5}
        VuUnit vu(1);
        SetVf(vu, 1, 2.0f, 4.0f, -6.0f, 8.0f);
        vu.regs().Q = 0.5f;
        SetAcc(vu, 100.0f, 200.0f, 300.0f, 400.0f);
        RunOne(vu, Upper(0x3C, 0x09, 1, 0, 0xF));
        PrintAcc("SUBAq.xyzw vf1 Q=0.5", vu);
        CHECK(AccIs(vu, 1.5f, 3.5f, -6.5f, 7.5f));
    }
    {   // Dest mask: MULAq.xz keeps ACC.y / ACC.w
        VuUnit vu(1);
        SetVf(vu, 1, 2.0f, 4.0f, -6.0f, 8.0f);
        vu.regs().Q = 0.5f;
        SetAcc(vu, 100.0f, 200.0f, 300.0f, 400.0f);
        RunOne(vu, Upper(0x3C, 0x07, 1, 0, 0xA));
        PrintAcc("MULAq.xz vf1 Q=0.5", vu);
        CHECK(AccIs(vu, 1.0f, 200.0f, -3.0f, 400.0f));
    }
    {   // The 0x3D column is unchanged: MADDAq folds the incoming ACC in; MSUBAq subtracts; ABS writes ft.
        VuUnit vu(1);
        SetVf(vu, 1, 2.0f, 4.0f, -6.0f, 8.0f);
        vu.regs().Q = 0.5f;
        SetAcc(vu, 100.0f, 200.0f, 300.0f, 400.0f);
        RunOne(vu, Upper(0x3D, 0x08, 1, 0, 0xF));
        PrintAcc("MADDAq.xyzw vf1 Q=0.5", vu);
        CHECK(AccIs(vu, 101.0f, 202.0f, 297.0f, 404.0f));
        SetAcc(vu, 100.0f, 200.0f, 300.0f, 400.0f);
        RunOne(vu, Upper(0x3D, 0x09, 1, 0, 0xF));
        PrintAcc("MSUBAq.xyzw vf1 Q=0.5", vu);
        CHECK(AccIs(vu, 99.0f, 198.0f, 303.0f, 396.0f));
        SetAcc(vu, 100.0f, 200.0f, 300.0f, 400.0f);
        RunOne(vu, Upper(0x3D, 0x07, 1, 2, 0xF));   // ABS vf2, vf1
        CHECK(AccIs(vu, 100.0f, 200.0f, 300.0f, 400.0f));
        CHECK(vu.regs().VF[2].x == 2.0f && vu.regs().VF[2].z == 6.0f && vu.regs().VF[2].w == 8.0f);
    }
    if (g_fail) {
        std::printf("VuSpecial2QRows: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("VuSpecial2QRows: all checks passed\n");
    return 0;
}
