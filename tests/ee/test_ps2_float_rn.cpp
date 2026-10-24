// Ps2FloatNearestForms: the arithmetic to-nearest forms of ps2_div_s /
// ps2_sqrt_s / ps2_rsqrt_s (eecode round 4, E3: no FPU mode switch per call)
// are bit-equal to the round-3 mode-switching bodies (kept here verbatim as the
// reference) over a random operand grid under all four host rounding modes,
// plus the edge patterns: zeros of both signs, subnormals (operands and
// quotients), powers of two, FLT_MAX / overflow, Inf and NaN operands, exact
// quotients, quotients one ulp either side of a float midpoint, and the
// subnormal-range ties (3 * 2^-149 / 2) the arithmetic form hands back to the
// reference. Also pins the public contract the round-3 test had: the caller's
// mode is untouched.
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine/ee/cpu/ps2_float.h"
#include "engine/platform/host/platform.h"

// The reference switches the FPU mode around its operation: this TU must not let
// the compiler move the operation across fesetround (without the pragma Clang did,
// at -O3, once the reference was inlined into the grid loop -- the test then saw
// the toward-zero quotient as the "reference").
PS2R_FENV_ACCESS_ON

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

namespace {

uint32_t Bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}
float FromBits(uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

// The round-3 bodies, verbatim (engine/ee/cpu/ps2_float.inl before E3); kept
// out of line so the mode switch and the operation stay one unit.
PS2R_NOINLINE float RefDiv(float a, float b) {
    const int m = fegetround();
    fesetround(FE_TONEAREST);
    volatile float r = ps2_float_clamp(a) / ps2_float_clamp(b);
    fesetround(m);
    return ps2_float_clamp(r);
}
PS2R_NOINLINE float RefSqrt(float v) {
    const int m = fegetround();
    fesetround(FE_TONEAREST);
    volatile float r = std::sqrt(std::fabs(ps2_float_clamp(v)));
    fesetround(m);
    return ps2_float_clamp(r);
}
PS2R_NOINLINE float RefRsqrt(float a, float b) {
    const int m = fegetround();
    fesetround(FE_TONEAREST);
    volatile float r = ps2_float_clamp(a) / std::sqrt(std::fabs(ps2_float_clamp(b)));
    fesetround(m);
    return ps2_float_clamp(r);
}

uint64_t g_seed = 0x9E3779B97F4A7C15ull;
uint32_t Next() {
    uint64_t z = (g_seed += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return static_cast<uint32_t>(z ^ (z >> 31));
}

// Operand generator: a mix of uniform bit patterns (every exponent, Inf/NaN
// included), "game" magnitudes (2^-20 .. 2^20), and the edge list.
const uint32_t kEdges[] = {
    0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x00400000u, 0x007FFFFFu, 0x00800000u, 0x00800001u,
    0x3F800000u, 0xBF800000u, 0x40000000u, 0x40400000u, 0x3EAAAAABu, 0x7F7FFFFFu, 0xFF7FFFFFu, 0x7F7FFFFEu,
    0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFC00000u, 0x7F800001u, 0x00000003u, 0x3F000000u, 0x4B800000u,
    0x33800000u, 0x7E800000u, 0x01000000u, 0x0A800000u, 0x5A800000u,
};
constexpr size_t kEdgeCount = sizeof(kEdges) / sizeof(kEdges[0]);

uint32_t Operand(uint32_t i) {
    switch (i % 4) {
        case 0: return Next();
        case 1: {   // game magnitudes
            const uint32_t exp = 107u + Next() % 41u;   // 2^-20 .. 2^20
            return (Next() & 0x807FFFFFu) | (exp << 23);
        }
        case 2: return kEdges[Next() % kEdgeCount];
        default: {   // near a power of two (quotients near exact / near midpoints)
            const uint32_t exp = 1u + Next() % 253u;
            return (Next() & 0x80000000u) | (exp << 23) | (Next() % 3u);
        }
    }
}

const int kModes[] = {FE_TOWARDZERO, FE_TONEAREST, FE_UPWARD, FE_DOWNWARD};

void Grid(int mode, uint32_t n, uint64_t* diffs, uint64_t* cases) {
    fesetround(mode);
    for (uint32_t i = 0; i < n; ++i) {
        const float a = FromBits(Operand(i)), b = FromBits(Operand(i + 7));
        const uint32_t d1 = Bits(ps2_div_s(a, b)), d0 = Bits(RefDiv(a, b));
        const uint32_t s1 = Bits(ps2_sqrt_s(a)), s0 = Bits(RefSqrt(a));
        const uint32_t r1 = Bits(ps2_rsqrt_s(a, b)), r0 = Bits(RefRsqrt(a, b));
        *cases += 3;
        if (d1 != d0) { if (*diffs < 8) std::printf("div  mode=%d a=%08x b=%08x new=%08x ref=%08x\n", mode, Bits(a), Bits(b), d1, d0); ++*diffs; }
        if (s1 != s0) { if (*diffs < 8) std::printf("sqrt mode=%d a=%08x new=%08x ref=%08x\n", mode, Bits(a), s1, s0); ++*diffs; }
        if (r1 != r0) { if (*diffs < 8) std::printf("rsqrt mode=%d a=%08x b=%08x new=%08x ref=%08x\n", mode, Bits(a), Bits(b), r1, r0); ++*diffs; }
        CHECK(fegetround() == mode);
    }
}

// Every edge against every edge (the exact / tie / non-finite table).
void EdgeTable(int mode, uint64_t* diffs, uint64_t* cases) {
    fesetround(mode);
    for (size_t i = 0; i < kEdgeCount; ++i) {
        for (size_t j = 0; j < kEdgeCount; ++j) {
            const float a = FromBits(kEdges[i]), b = FromBits(kEdges[j]);
            *cases += 2;
            if (Bits(ps2_div_s(a, b)) != Bits(RefDiv(a, b))) { ++*diffs; std::printf("edge div mode=%d %08x/%08x\n", mode, kEdges[i], kEdges[j]); }
            if (Bits(ps2_rsqrt_s(a, b)) != Bits(RefRsqrt(a, b))) { ++*diffs; std::printf("edge rsqrt mode=%d %08x/%08x\n", mode, kEdges[i], kEdges[j]); }
        }
        *cases += 1;
        if (Bits(ps2_sqrt_s(FromBits(kEdges[i]))) != Bits(RefSqrt(FromBits(kEdges[i])))) { ++*diffs; std::printf("edge sqrt mode=%d %08x\n", mode, kEdges[i]); }
    }
}

// Quotients that land exactly on, or one double-ulp around, a float midpoint
// under the arithmetic: a = M * b with M a 25-bit odd significand is NOT a
// float, so build the nearest floats instead: a = RN(M*b) and its neighbours.
void MidpointNeighbours(int mode, uint64_t* diffs, uint64_t* cases) {
    fesetround(mode);
    for (uint32_t k = 0; k < 20000; ++k) {
        const uint32_t mbits = (Next() & 0x00FFFFFFu) | 0x01000000u | 1u;   // 25-bit odd
        const float b = FromBits((Next() & 0x007FFFFFu) | ((100u + Next() % 60u) << 23));
        const double M = static_cast<double>(mbits) * std::ldexp(1.0, -24 + static_cast<int>(Next() % 20u) - 10);
        const float a0 = static_cast<float>(M * static_cast<double>(b));
        const uint32_t base = Bits(a0);
        for (int d = -1; d <= 1; ++d) {
            const float a = FromBits(base + static_cast<uint32_t>(d));
            *cases += 1;
            if (Bits(ps2_div_s(a, b)) != Bits(RefDiv(a, b))) { ++*diffs; if (*diffs < 8) std::printf("mid div mode=%d %08x/%08x\n", mode, Bits(a), Bits(b)); }
        }
    }
}

// Subnormal-range quotients, ties included (3*2^-149 / 2 = 3*2^-150, exactly
// between two subnormals), and sqrt of subnormals.
void Subnormals(int mode, uint64_t* diffs, uint64_t* cases) {
    fesetround(mode);
    for (uint32_t k = 0; k < 20000; ++k) {
        const float a = FromBits((Next() & 0x80000000u) | (Next() % 0x01000000u));       // |a| < 2^-125
        const float b = FromBits((Next() & 0x80000000u) | ((120u + Next() % 20u) << 23) | (Next() % 4u));   // 2^-7 .. 2^12
        *cases += 2;
        if (Bits(ps2_div_s(a, b)) != Bits(RefDiv(a, b))) { ++*diffs; if (*diffs < 8) std::printf("sub div mode=%d %08x/%08x\n", mode, Bits(a), Bits(b)); }
        if (Bits(ps2_sqrt_s(a)) != Bits(RefSqrt(a))) { ++*diffs; if (*diffs < 8) std::printf("sub sqrt mode=%d %08x\n", mode, Bits(a)); }
    }
    const float tie_a = FromBits(0x00000003u), tie_b = 2.0f;
    *cases += 1;
    if (Bits(ps2_div_s(tie_a, tie_b)) != Bits(RefDiv(tie_a, tie_b))) { ++*diffs; std::printf("tie 3*2^-149/2 mode=%d\n", mode); }
}

}  // namespace

int main() {
    // The known values of the round-3 test, on the guest thread's mode.
    fesetround(FE_TOWARDZERO);
    CHECK(Bits(ps2_div_s(1.0f, 5.0f)) == 0x3E4CCCCDu);
    CHECK(Bits(ps2_div_s(2.0f, 3.0f)) == 0x3F2AAAABu);
    CHECK(Bits(ps2_div_s(1.0f, 0.0f)) == 0x7F7FFFFFu);
    CHECK(Bits(ps2_div_s(-1.0f, 0.0f)) == 0xFF7FFFFFu);
    CHECK(Bits(ps2_sqrt_s(4.0f)) == 0x40000000u);
    CHECK(Bits(ps2_sqrt_s(-4.0f)) == 0x40000000u);
    CHECK(Bits(ps2_sqrt_s(2.0f)) == 0x3FB504F3u);
    CHECK(Bits(ps2_div_s(-0.0f, 5.0f)) == 0x80000000u);
    CHECK(Bits(ps2_div_s(0.0f, -5.0f)) == 0x80000000u);
    CHECK(fegetround() == FE_TOWARDZERO);

    uint64_t diffs = 0, cases = 0;
    for (int mode : kModes) {
        Grid(mode, 300000, &diffs, &cases);
        EdgeTable(mode, &diffs, &cases);
        MidpointNeighbours(mode, &diffs, &cases);
        Subnormals(mode, &diffs, &cases);
    }
    fesetround(FE_TONEAREST);
    std::printf("Ps2FloatNearestForms: %llu comparisons, %llu differences\n", (unsigned long long)cases, (unsigned long long)diffs);
    CHECK(diffs == 0);
    if (g_fail) {
        std::printf("FAILED (%d)\n", g_fail);
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
