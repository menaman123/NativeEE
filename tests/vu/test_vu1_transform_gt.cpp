// Vu1TransformGroundTruth: a retail transform chain (MULAw / MADDAy / MADDAz
// / MADDx, then DIV / WAITQ / MULq / FTOI0) whose bit-exact outputs
// discriminate the rounding and fusion mode: the VU rounds toward zero and
// the MADD family is a single-rounding fused multiply-add; an unfused or
// round-to-nearest implementation lands off on every lane.
#include <cstdio>
#include <cstring>

#include "engine/vu/tiers/vu_interpreter_tier.h"
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

constexpr uint32_t U_NOP = 0x000002FFu;
constexpr uint32_t L_NOP = 0x8000033Cu;
constexpr uint32_t U_EBIT = 0x40000000u;

void SetVF(VUregs& r, int reg, uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    r.VF[reg].I[0] = x;
    r.VF[reg].I[1] = y;
    r.VF[reg].I[2] = z;
    r.VF[reg].I[3] = w;
}

void PutBundle(VuUnit& vu, int i, uint32_t lower, uint32_t upper) {
    uint8_t b[8];
    std::memcpy(b, &lower, 4);
    std::memcpy(b + 4, &upper, 4);
    vu.WriteMicroMem(static_cast<uint32_t>(i) * 8u, b, 8);
}

void SeedInputs(VUregs& r) {
    r.Reset();
    SetVF(r, 1, 0x45d4dbd0u, 0x4664410du, 0xc3eb1b81u, 0x3ed75d7cu);
    SetVF(r, 2, 0x44601194u, 0xc5dd1d30u, 0xc2521707u, 0x3d407333u);
    SetVF(r, 3, 0xc700be6au, 0xc6e87006u, 0x447d2fb3u, 0xbf67f06au);
    SetVF(r, 4, 0x488a4b80u, 0x4873ad40u, 0x49cbcea7u, 0x40e81f40u);
    SetVF(r, 11, 0x3e96b5d2u, 0x3f8c92b2u, 0xbf335710u, 0x00000000u);
}

bool CheckVF(const VUregs& r, int reg, const uint32_t (&want)[4], int lanes, const char* tag) {
    bool ok = true;
    for (int k = 0; k < lanes; ++k) {
        if (r.VF[reg].I[k] != want[k]) {
            std::printf("  %s vf%d.%c = %08x want %08x\n", tag, reg, "xyzw"[k], r.VF[reg].I[k], want[k]);
            ok = false;
        }
    }
    return ok;
}

void VariantA() {
    VuUnit vu(1);
    SeedInputs(vu.regs());
    PutBundle(vu, 0, L_NOP, 0x01e021bfu);  // MULAw  ACC, vf4, vf0w
    PutBundle(vu, 1, L_NOP, 0x01eb10bdu);  // MADDAy ACC, vf2, vf11y
    PutBundle(vu, 2, L_NOP, 0x01eb18beu);  // MADDAz ACC, vf3, vf11z
    PutBundle(vu, 3, L_NOP, 0x01eb0a48u);  // MADDx  vf9, vf1, vf11x
    PutBundle(vu, 4, L_NOP, U_NOP | U_EBIT);
    PutBundle(vu, 5, L_NOP, U_NOP);
    VuInterpreterTier tier;
    tier.Run(vu, 0);
    static const uint32_t want[4] = {0x48970746u, 0x48825220u, 0x49cbb259u, 0x410105e0u};
    CHECK(CheckVF(vu.regs(), 9, want, 4, "A"));
    CHECK(vu.regs().TPC == 0x30u);
    CHECK(tier.bundles() == 6);
}

void VariantB() {
    VuUnit vu(1);
    SeedInputs(vu.regs());
    PutBundle(vu, 0, L_NOP, 0x01e021bfu);
    PutBundle(vu, 1, L_NOP, 0x01eb10bdu);
    PutBundle(vu, 2, L_NOP, 0x01eb18beu);
    PutBundle(vu, 3, L_NOP, 0x01eb0a48u);
    PutBundle(vu, 4, 0x81e903bcu, U_NOP);  // DIV Q, vf0w, vf9w
    PutBundle(vu, 5, 0x800003bfu, U_NOP);  // WAITQ
    PutBundle(vu, 6, L_NOP, 0x01c04a5cu);  // MULq.xyz  vf9, vf9, Q
    PutBundle(vu, 7, L_NOP, 0x01c7497cu);  // FTOI0.xyz vf7, vf9
    PutBundle(vu, 8, L_NOP, U_NOP | U_EBIT);
    PutBundle(vu, 9, L_NOP, U_NOP);
    VuInterpreterTier tier;
    tier.Run(vu, 0);
    uint32_t q;
    std::memcpy(&q, &vu.regs().Q, 4);
    if (q != 0x3dfdf867u) std::printf("  B Q = %08x want 3dfdf867\n", q);
    CHECK(q == 0x3dfdf867u);
    static const uint32_t want9[4] = {0x4715d4bbu, 0x4701499du, 0x484a14e8u, 0};
    static const uint32_t want7[4] = {38356u, 33097u, 206931u, 0};
    CHECK(CheckVF(vu.regs(), 9, want9, 3, "B"));
    CHECK(CheckVF(vu.regs(), 7, want7, 3, "B"));
    CHECK(vu.regs().TPC == 0x50u);
}

}  // namespace

int main() {
    VariantA();
    VariantB();
    if (g_fail) {
        std::printf("Vu1TransformGroundTruth: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("Vu1TransformGroundTruth: ALL PASS\n");
    return 0;
}
