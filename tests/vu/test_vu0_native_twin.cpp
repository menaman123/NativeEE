// Vu0NativeTwin: the `--vu0-native` lane kernels (engine/ee/recomp_abi/host_app/
// vu0_native.h) against the scalar emission they replace, bit for bit:
//  (1) Add / Sub / Mul vs the VR_reg operators of recomp_abi cpu_state.h over a
//      random grid seeded with the special patterns (+-0, denormals, +-Inf,
//      NaN, +-MAX, exponent edges) under all four rounding modes (the guest
//      thread's FE_TOWARDZERO included);
//  (2) Blend<mask> vs the per-lane masked write; Bc<lane> / Splat;
//  (3) LaneFlags<mask> + Commit vs VuMacroCommitFlags for a single op, every
//      mask, with random prior STATUS bits (D/I and the sticky nibble kept);
//  (4) the FOLD: a random run of k ops committed once (Commit(last, OR)) equals
//      k successive VuMacroCommitFlags calls -- MAC and STATUS;
//  (5) the vector arm vs the scalar arm (vu0n::scalar) on the same grid.
// Args: [iterations] (default 200000).
#include <cfenv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine/ee/recomp_abi/host_app/cpu_state.h"
#include "engine/ee/recomp_abi/host_app/vu.h"
#include "engine/ee/recomp_abi/host_app/vu0_native.h"

static int g_fail = 0;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            if (g_fail < 20) std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                               \
        }                                                           \
    } while (0)

namespace {

uint64_t g_rng = 0x9E3779B97F4A7C15ull;
uint32_t Rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return static_cast<uint32_t>(g_rng >> 11);
}

const uint32_t kSpecial[] = {
    0x00000000u, 0x80000000u, 0x00000001u, 0x80000001u, 0x007FFFFFu, 0x807FFFFFu,  // zeros, denormals
    0x00800000u, 0x80800000u, 0x7F7FFFFFu, 0xFF7FFFFFu,                            // min normal, +-MAX
    0x7F800000u, 0xFF800000u, 0x7FC00000u, 0xFFC00000u, 0x7F800001u, 0xFFFFFFFFu,  // +-Inf, NaNs
    0x3F800000u, 0xBF800000u, 0x3F000000u, 0x40000000u, 0x4B800000u, 0x33800000u,  // 1, -1, .5, 2, 2^24, 2^-24
    0x7F000000u, 0x7E800000u, 0x00FFFFFFu, 0x01000000u,
};

uint32_t RandomBits() {
    const uint32_t r = Rnd();
    switch (r & 7u) {
        case 0: return kSpecial[(r >> 3) % (sizeof(kSpecial) / sizeof(kSpecial[0]))];
        case 1: return (Rnd() & 0x807FFFFFu) | 0x3F800000u;     // around 1
        case 2: return (Rnd() & 0x807FFFFFu) | 0x7F000000u;     // near overflow
        case 3: return (Rnd() & 0x80FFFFFFu);                   // denormal / tiny
        default: return Rnd();
    }
}

VR_reg RandomVec() {
    VR_reg v{};
    for (int i = 0; i < 4; ++i) v.I[i] = RandomBits();
    return v;
}

bool Same(const VR_reg& a, const VR_reg& b) { return std::memcmp(a.I, b.I, 16) == 0; }

void Print(const char* tag, const VR_reg& v) {
    std::printf("  %s = %08x %08x %08x %08x\n", tag, v.I[0], v.I[1], v.I[2], v.I[3]);
}

// The scalar emission's masked write (emit_vu_masked_write without the flag).
VR_reg MaskedWriteRef(const VR_reg& dst, const VR_reg& res, unsigned mask) {
    VR_reg r = dst;
    if (mask & 8u) r.x = res.x;
    if (mask & 4u) r.y = res.y;
    if (mask & 2u) r.z = res.z;
    if (mask & 1u) r.w = res.w;
    return r;
}

template <unsigned M>
void CheckMask(const VR_reg& dst, const VR_reg& res, VUregs& prior_ref, VUregs& prior_nat) {
    // masked write
    const VR_reg ref = MaskedWriteRef(dst, res, M);
    const VR_reg nat = vu0n::Blend<M>(dst, res);
    const VR_reg sc = vu0n::scalar::Blend<M>(dst, res);
    CHECK(Same(ref, nat));
    CHECK(Same(ref, sc));
    // one-op flag commit
    VuMacroCommitFlags(prior_ref, res, static_cast<uint8_t>(M));
    const VR_reg fl = vu0n::LaneFlags<M>(res);
    const VR_reg fls = vu0n::scalar::LaneFlags<M>(res);
    CHECK(Same(fl, fls));
    vu0n::Commit(prior_nat, fl, fl);
    CHECK(prior_ref.mac_flag == prior_nat.mac_flag);
    CHECK(prior_ref.status_flag == prior_nat.status_flag);
    if (prior_ref.mac_flag != prior_nat.mac_flag || prior_ref.status_flag != prior_nat.status_flag) {
        Print("res", res);
        std::printf("  mask %u ref mac %04x status %04x | nat mac %04x status %04x\n", M, prior_ref.mac_flag,
                    prior_ref.status_flag, prior_nat.mac_flag, prior_nat.status_flag);
    }
}

void RunGrid(int iterations) {
    for (int it = 0; it < iterations; ++it) {
        const VR_reg a = RandomVec(), b = RandomVec(), acc = RandomVec();
        // (1) + (5): the operators
        const VR_reg add_ref = a + b, sub_ref = a - b, mul_ref = a * b;
        const VR_reg madd_ref = acc + (a * b), msub_ref = acc - (a * b);
        CHECK(Same(add_ref, vu0n::Add(a, b)));
        CHECK(Same(sub_ref, vu0n::Sub(a, b)));
        CHECK(Same(mul_ref, vu0n::Mul(a, b)));
        CHECK(Same(madd_ref, vu0n::Add(acc, vu0n::Mul(a, b))));
        CHECK(Same(msub_ref, vu0n::Sub(acc, vu0n::Mul(a, b))));
        CHECK(Same(add_ref, vu0n::scalar::Add(a, b)));
        CHECK(Same(sub_ref, vu0n::scalar::Sub(a, b)));
        CHECK(Same(mul_ref, vu0n::scalar::Mul(a, b)));
        if (!Same(mul_ref, vu0n::Mul(a, b)) || !Same(add_ref, vu0n::Add(a, b))) {
            Print("a", a);
            Print("b", b);
            Print("mul_ref", mul_ref);
            Print("mul_nat", vu0n::Mul(a, b));
        }
        // broadcasts
        VR_reg bx{}, by{}, bz{}, bw{};
        bx.x = bx.y = bx.z = bx.w = b.x;
        by.x = by.y = by.z = by.w = b.y;
        bz.x = bz.y = bz.z = bz.w = b.z;
        bw.x = bw.y = bw.z = bw.w = b.w;
        CHECK(Same(bx, vu0n::Bc<0>(b)));
        CHECK(Same(by, vu0n::Bc<1>(b)));
        CHECK(Same(bz, vu0n::Bc<2>(b)));
        CHECK(Same(bw, vu0n::Bc<3>(b)));
        CHECK(Same(bx, vu0n::Splat(b.x)));
        CHECK(Same(a + bx, vu0n::Add(a, vu0n::Bc<0>(b))));
        // (2) + (3): every mask, random prior STATUS (D/I/sticky kept)
        VUregs pr{}, pn{};
        pr.status_flag = static_cast<uint16_t>(Rnd() & 0xFFFu);
        pr.mac_flag = static_cast<uint16_t>(Rnd());
        pn = pr;
        const VR_reg res = (it & 1) ? mul_ref : madd_ref;
        switch (Rnd() & 15u) {
            case 0: CheckMask<0>(a, res, pr, pn); break;
            case 1: CheckMask<1>(a, res, pr, pn); break;
            case 2: CheckMask<2>(a, res, pr, pn); break;
            case 3: CheckMask<3>(a, res, pr, pn); break;
            case 4: CheckMask<4>(a, res, pr, pn); break;
            case 5: CheckMask<5>(a, res, pr, pn); break;
            case 6: CheckMask<6>(a, res, pr, pn); break;
            case 7: CheckMask<7>(a, res, pr, pn); break;
            case 8: CheckMask<8>(a, res, pr, pn); break;
            case 9: CheckMask<9>(a, res, pr, pn); break;
            case 10: CheckMask<10>(a, res, pr, pn); break;
            case 11: CheckMask<11>(a, res, pr, pn); break;
            case 12: CheckMask<12>(a, res, pr, pn); break;
            case 13: CheckMask<13>(a, res, pr, pn); break;
            case 14: CheckMask<14>(a, res, pr, pn); break;
            default: CheckMask<15>(a, res, pr, pn); break;
        }
        // (4) the fold: k ops, masks 0xF / 0xE / 0x8 / 0x1 in a random order
        {
            VUregs fr{}, fn{};
            fr.status_flag = static_cast<uint16_t>(Rnd() & 0xFFFu);
            fn = fr;
            const int k = 1 + static_cast<int>(Rnd() % 6u);
            VR_reg last{}, acc_or{};
            for (int j = 0; j < k; ++j) {
                const VR_reg r = RandomVec();
                const unsigned m = (Rnd() & 1u) ? 0xFu : (Rnd() & 15u);
                VuMacroCommitFlags(fr, r, static_cast<uint8_t>(m));
                VR_reg fl{};
                switch (m) {   // the emitter instantiates the mask statically; here a switch over the 16
#define L(M) case M: fl = vu0n::LaneFlags<M>(r); break;
                    L(0) L(1) L(2) L(3) L(4) L(5) L(6) L(7) L(8) L(9) L(10) L(11) L(12) L(13) L(14) L(15)
#undef L
                    default: break;
                }
                last = fl;
                acc_or = (j == 0) ? fl : vu0n::Or(acc_or, fl);
            }
            vu0n::Commit(fn, last, acc_or);
            CHECK(fr.mac_flag == fn.mac_flag);
            CHECK(fr.status_flag == fn.status_flag);
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const int iterations = argc > 1 ? std::atoi(argv[1]) : 200000;
    const int modes[] = {FE_TOWARDZERO, FE_TONEAREST, FE_UPWARD, FE_DOWNWARD};
    for (int m : modes) {
        fesetround(m);
        g_rng = 0x9E3779B97F4A7C15ull ^ static_cast<uint64_t>(m + 1);
        RunGrid(iterations);
        std::printf("Vu0NativeTwin: mode %d: %d iterations, failures so far %d\n", m, iterations, g_fail);
    }
    fesetround(FE_TONEAREST);
    std::printf("Vu0NativeTwin: arm %s, %s\n", PS2R_VU0N_NEON ? "neon" : (PS2R_VU0N_SSE2 ? "sse2" : "scalar"),
                g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
