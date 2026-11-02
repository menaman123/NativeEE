// VuFloatRules: the VU single-precision rules -- clamp table, MAC flags from
// raw bits, the lane select table, masked clamped writes, ACC re-clamp,
// Q-status bit ownership, MAX/MINI integer ordering, FDIV by zero, FTOI
// saturation/truncation, ITOF, and the round-toward-zero guard.
#include <cfenv>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine/vu/vu_chop_round_guard.h"
#include "engine/vu/vu_decoder.h"
#include "engine/vu/vu_float.h"

static int g_fail = 0;
#define CHECK(c)                                                          \
    do {                                                                  \
        if (!(c)) {                                                       \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);       \
            ++g_fail;                                                     \
        }                                                                 \
    } while (0)

static constexpr uint32_t kPosZero = 0x00000000u, kNegZero = 0x80000000u;
static constexpr uint32_t kPosDenorm = 0x00000001u, kNegDenorm = 0x807FFFFFu;
static constexpr uint32_t kPosInf = 0x7F800000u, kNegInf = 0xFF800000u;
static constexpr uint32_t kNaN = 0x7FC00000u, kNegNaN = 0xFFC00001u;
static constexpr uint32_t kPosMax = 0x7F7FFFFFu, kNegMax = 0xFF7FFFFFu;
static constexpr uint32_t kOne = 0x3F800000u, kNegOne = 0xBF800000u, kTwo = 0x40000000u;
static constexpr uint32_t kMinNormal = 0x00800000u;

static void TestBitsPunning() {
    CHECK(VuFloat::F2Bits(1.0f) == kOne);
    CHECK(VuFloat::Bits2F(kTwo) == 2.0f);
    CHECK(VuFloat::F2Bits(VuFloat::Bits2F(kNaN)) == kNaN);
    CHECK(VuFloat::F2Bits(VuFloat::Bits2F(kNegDenorm)) == kNegDenorm);
}

static void TestClamp() {
    // The register-level clamp: denormals flush, every other pattern -- the exponent-255 binade (an IEEE
    // Inf / NaN pattern) included -- is a VU value and stays.
    CHECK(VuFloat::ClampBits1(kPosZero) == kPosZero);
    CHECK(VuFloat::ClampBits1(kNegZero) == kNegZero);        // -0 survives (sign kept, no mantissa)
    CHECK(VuFloat::ClampBits1(kPosDenorm) == kPosZero);      // denormal -> +0
    CHECK(VuFloat::ClampBits1(kNegDenorm) == kNegZero);      // denormal -> -0
    CHECK(VuFloat::ClampBits1(kMinNormal) == kMinNormal);    // smallest normal unchanged
    CHECK(VuFloat::ClampBits1(kPosInf) == kPosInf);          // 2^128: kept
    CHECK(VuFloat::ClampBits1(kNegInf) == kNegInf);
    CHECK(VuFloat::ClampBits1(kNaN) == kNaN);                // -2^128 * 1.5: kept
    CHECK(VuFloat::ClampBits1(kNegNaN) == kNegNaN);
    CHECK(VuFloat::ClampBits1(kPosMax) == kPosMax);
    CHECK(VuFloat::ClampBits1(kOne) == kOne && VuFloat::ClampBits1(kNegOne) == kNegOne);
    // The IEEE-producer clamp (Q / P / I / FDIV / EFU results) keeps the +/-MAX rule.
    CHECK(VuFloat::F2Bits(VuFloat::ClampFloat(VuFloat::Bits2F(kNegInf))) == kNegMax);
    CHECK(VuFloat::ClampIeeeBits1(kNaN) == kPosMax && VuFloat::ClampIeeeBits1(kNegNaN) == kNegMax);
    CHECK(VuFloat::ClampIeeeBits1(kPosDenorm) == kPosZero && VuFloat::ClampIeeeBits1(kOne) == kOne);

    VR_reg acc;
    acc.I[0] = kPosInf; acc.I[1] = kNegDenorm; acc.I[2] = kOne; acc.I[3] = kNaN;
    VuFloat::ClampStoreACC(acc);
    CHECK(acc.I[0] == kPosInf && acc.I[1] == kNegZero && acc.I[2] == kOne && acc.I[3] == kNaN);

    VR_reg dst;
    dst.I[0] = 0x11111111u; dst.I[1] = 0x22222222u; dst.I[2] = 0x33333333u; dst.I[3] = 0x44444444u;
    const uint32_t res[4] = {kPosInf, kPosDenorm, kNegInf, kTwo};
    VuFloat::WriteMaskClamp(dst, res, 0xA);  // X and Z
    CHECK(dst.I[0] == kPosInf && dst.I[1] == 0x22222222u && dst.I[2] == kNegInf && dst.I[3] == 0x44444444u);
    VuFloat::WriteMaskClamp(dst, res, 0x5);  // Y and W
    CHECK(dst.I[1] == kPosZero && dst.I[3] == kTwo);
    VuFloat::WriteMaskClamp(dst, res, 0x0);
    CHECK(dst.I[0] == kPosInf && dst.I[3] == kTwo);
}

// The exponent-255 binade through the FMAC kernels: Sly 2's shadow-volume programs MFIR a 16-bit tag into a
// vertex's w (0xFFFF9000 = -2^128 * 1.9453125), carry it through MULA by 1.0 and MADD + 0.0 and read the low
// 16 bits back with ILW. Hardware keeps the pattern bit for bit; an IEEE kernel would make it a NaN.
static void TestExp255() {
    const uint32_t kTag = 0xFFFF9000u, kTagPos = 0x7FFF9000u;
    CHECK(VuFloat::LaneToDouble(kPosInf) == 340282366920938463463374607431768211456.0);  // 2^128
    CHECK(VuFloat::DoubleToLane(VuFloat::LaneToDouble(kTag)) == kTag);
    CHECK(VuFloat::DoubleToLane(VuFloat::LaneToDouble(kTagPos)) == kTagPos);
    CHECK(VuFloat::DoubleToLane(VuFloat::LaneToDouble(kNegMax)) == kNegMax);
    CHECK(VuFloat::DoubleToLane(1.0) == kOne && VuFloat::DoubleToLane(-1.0) == kNegOne);
    CHECK(VuFloat::DoubleToLane(0.0) == kPosZero && VuFloat::DoubleToLane(-0.0) == kNegZero);
    CHECK(VuFloat::DoubleToLane(VuFloat::LaneToDouble(kPosDenorm)) == kPosZero);
    CHECK(VuFloat::DoubleToLane(VuFloat::LaneToDouble(kPosInf) * 2.0) == kPosMax);     // past the binade: overflow value
    CHECK(VuFloat::DoubleToLane(-VuFloat::LaneToDouble(kPosInf) * 4.0) == kNegMax);
    CHECK(VuFloat::DoubleToLane(1.0 / 3.0) == 0x3EAAAAAAu);                             // truncation, not rounding

    VR_reg one, zero, tag;
    for (int k = 0; k < 4; ++k) { one.I[k] = kOne; zero.I[k] = kPosZero; tag.I[k] = kOne; }
    tag.I[3] = kTag;
    const VR_reg acc = VuFloat::Mul4(tag, one);              // MULA.xyzw vf, vf07(w = 1.0)
    CHECK(acc.I[3] == kTag && acc.I[0] == kOne);
    const VR_reg out = VuFloat::Fma4(zero, one, acc);        // MADD.xyzw vf25, vf08(w = 0.0), vf00.w
    CHECK(out.I[3] == kTag && out.I[0] == kOne);
    CHECK((out.I[3] & 0xF000u) == 0x9000u);                   // the clipper's ILW.w test
    // A genuine overflow still lands on the overflow value; a tag times 0 is 0; a tag plus itself overflows.
    VR_reg big; for (int k = 0; k < 4; ++k) big.I[k] = kPosMax;
    CHECK(VuFloat::Mul4(big, big).I[0] == kPosMax);
    CHECK(VuFloat::Mul4(tag, zero).I[3] == kNegZero || VuFloat::Mul4(tag, zero).I[3] == kPosZero);
    CHECK(VuFloat::Add4(tag, tag).I[3] == kNegMax);
    CHECK(VuFloat::Sub4(tag, tag).I[3] == kPosZero);
    CHECK(VuFloat::Fms4(one, one, tag).I[3] == kTag);         // -2^128*1.945 - 1: the 1 is below the lsb
    CHECK(VuFloat::Mul4(tag, one).I[3] == kTag);
}

static void TestLaneMask() {
    for (int m = 0; m < 16; ++m) {
        for (int k = 0; k < 4; ++k) {
            const bool sel = (m & (8 >> k)) != 0;
            CHECK(VuFloat::kLaneMask[m][k] == (sel ? ~0u : 0u));
        }
    }
    CHECK((reinterpret_cast<uintptr_t>(&VuFloat::kLaneMask[0][0]) & 15u) == 0);
}

static void TestMacFlags() {
    // field: x=3 y=2 z=1 w=0; nibbles Z 0-3, S 4-7, U 8-11, O 12-15
    CHECK(VuFloat::MacFlags1(kOne, 3) == 0);
    CHECK(VuFloat::MacFlags1(kNegOne, 3) == vu_mac::SX);
    CHECK(VuFloat::MacFlags1(kPosZero, 0) == vu_mac::ZW);
    CHECK(VuFloat::MacFlags1(kNegZero, 0) == (vu_mac::ZW | vu_mac::SW));          // -0: Z and S
    CHECK(VuFloat::MacFlags1(kPosDenorm, 2) == (vu_mac::ZY | vu_mac::UY));         // denormal: Z and U
    CHECK(VuFloat::MacFlags1(kNegDenorm, 1) == (vu_mac::ZZ | vu_mac::UZ | vu_mac::SZ));
    CHECK(VuFloat::MacFlags1(kPosInf, 3) == vu_mac::OX);                            // Inf: O
    CHECK(VuFloat::MacFlags1(kNegInf, 3) == (vu_mac::OX | vu_mac::SX));
    CHECK(VuFloat::MacFlags1(kNaN, 0) == vu_mac::OW);                               // NaN pattern: O
    CHECK(VuFloat::MacFlags1(kMinNormal, 3) == 0);
    CHECK(VuFloat::MacFlags1(kPosMax, 3) == 0);
    const uint32_t bits[4] = {kNegOne, kPosZero, kPosInf, kNegDenorm};
    CHECK(VuFloat::PackMacBits(bits, 0xF) == (vu_mac::SX | vu_mac::ZY | vu_mac::OZ | vu_mac::ZW | vu_mac::UW | vu_mac::SW));
    CHECK(VuFloat::PackMacBits(bits, 0x8) == vu_mac::SX);
    CHECK(VuFloat::PackMacBits(bits, 0x1) == (vu_mac::ZW | vu_mac::UW | vu_mac::SW));
    CHECK(VuFloat::PackMacBits(bits, 0x0) == 0);
    CHECK(vu_mac::ZW == 1 && vu_mac::ZX == 8 && vu_mac::SW == 0x10 && vu_mac::UX == 0x800 && vu_mac::OX == 0x8000);
#if defined(PS2R_SIMD) && PS2R_SIMD && defined(__aarch64__)
    for (int m = 0; m < 16; ++m) {
        const uint32_t sets[3][4] = {{kNegOne, kPosZero, kPosInf, kNegDenorm}, {kNaN, kNegZero, kOne, kNegInf},
                                     {kPosDenorm, kNegNaN, kMinNormal, kPosMax}};
        for (const auto& s : sets) {
            CHECK(VuFloat::MacFlags4(vld1q_u32(s), static_cast<uint8_t>(m)) == VuFloat::PackMacBits(s, static_cast<uint8_t>(m)));
            uint32_t out[4];
            vst1q_u32(out, VuFloat::ClampBits4(vld1q_u32(s)));
            for (int k = 0; k < 4; ++k) CHECK(out[k] == VuFloat::ClampBits1(s[k]));
        }
    }
#endif
#if PS2R_VU_SSE
    // The SSE2 twins against the scalar per-lane rules (MacFlags1 / ClampBits1 are scalar on every build): the
    // edge patterns in every lane position, then 1M pseudo-random lanes biased toward the exponent ends.
    const uint32_t edges[] = {kOne, kNegOne, kPosZero, kNegZero, kPosDenorm, kNegDenorm, kPosInf, kNegInf, kNaN, kNegNaN,
                              kMinNormal, kPosMax, 0x00000001u, 0x807FFFFFu, 0x7F800001u, 0xFF7FFFFFu};
    auto check4 = [](const uint32_t s[4]) {
        uint32_t out[4];
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out), VuFloat::ClampBits4S(_mm_loadu_si128(reinterpret_cast<const __m128i*>(s))));
        VR_reg v;
        for (int k = 0; k < 4; ++k) v.I[k] = s[k];
        const VR_reg cv = VuFloat::ClampVec(v);
        bool any255 = false;
        for (int k = 0; k < 4; ++k) {
            CHECK(out[k] == VuFloat::ClampBits1(s[k]));
            CHECK(cv.I[k] == VuFloat::ClampBits1(s[k]));
            any255 = any255 || (s[k] & VuFloat::kExpMask) == VuFloat::kExpMask;
        }
        CHECK(VuFloat::AnyExp255(v) == any255);
        for (int m = 0; m < 16; ++m) {
            uint16_t ref = 0;
            for (int k = 0; k < 4; ++k)
                if (m & (8 >> k)) ref = static_cast<uint16_t>(ref | VuFloat::MacFlags1(s[k], 3 - k));
            CHECK(VuFloat::PackMacBits(s, static_cast<uint8_t>(m)) == ref);
            VR_reg dst;
            for (int k = 0; k < 4; ++k) dst.I[k] = 0x12345678u + static_cast<uint32_t>(k);
            VuFloat::WriteMaskClamp(dst, s, static_cast<uint8_t>(m));
            for (int k = 0; k < 4; ++k)
                CHECK(dst.I[k] == ((m & (8 >> k)) ? VuFloat::ClampBits1(s[k]) : 0x12345678u + static_cast<uint32_t>(k)));
        }
    };
    for (uint32_t e : edges)
        for (int pos = 0; pos < 4; ++pos) {
            uint32_t s[4] = {kOne, kNegZero, kPosDenorm, kPosInf};
            s[pos] = e;
            check4(s);
        }
    uint64_t x = 0x9E3779B97F4A7C15ull;
    for (int i = 0; i < 250000; ++i) {
        uint32_t s[4];
        for (int k = 0; k < 4; ++k) {
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            uint32_t b = static_cast<uint32_t>(x >> 16);
            const uint32_t pick = static_cast<uint32_t>(x) & 3u;
            if (pick == 0) b &= 0x807FFFFFu;          // exponent 0: zeros and denormals
            else if (pick == 1) b |= 0x7F800000u;     // exponent 255
            s[k] = b;
        }
        check4(s);
    }
#endif
}

static void TestQStatus() {
    VUregs r;
    r.Reset();
    r.status_flag = 0x0FFF;                     // everything set
    VuFloat::CommitQStatus(r, 0x0000);          // D/I (bits 4-5) replaced; the sticky DS/IS are only ever ORed in
    CHECK(r.status_flag == 0x0FCF);
    r.status_flag = 0x000F;
    VuFloat::CommitQStatus(r, vu_status::D | vu_status::DS);
    CHECK(r.status_flag == (0x000F | vu_status::D | vu_status::DS));
    VuFloat::CommitQStatus(r, vu_status::I | vu_status::IS);
    CHECK(r.status_flag == (0x000F | vu_status::I | vu_status::IS | vu_status::DS));  // D replaced by I; DS stays sticky
    r.status_flag = 0x03C0;                     // the MAC sticky bits are never touched
    VuFloat::CommitQStatus(r, 0x0C30);
    CHECK(r.status_flag == 0x0FF0);
}

static void TestMinMax() {
    CHECK(VuFloat::FpMax(kOne, kTwo) == kTwo && VuFloat::FpMin(kOne, kTwo) == kOne);
    CHECK(VuFloat::FpMax(kNegOne, kOne) == kOne && VuFloat::FpMin(kNegOne, kOne) == kNegOne);
    CHECK(VuFloat::FpMax(kNegOne, kNegMax) == kNegOne && VuFloat::FpMin(kNegOne, kNegMax) == kNegMax);  // both negative: reversed int order
    CHECK(VuFloat::FpMax(kNegZero, kPosZero) == kPosZero && VuFloat::FpMin(kNegZero, kPosZero) == kNegZero);
    CHECK(VuFloat::FpMax(kPosZero, kNegZero) == kPosZero && VuFloat::FpMin(kPosZero, kNegZero) == kNegZero);
    CHECK(VuFloat::FpMax(kNaN, kOne) == kNaN);        // raw bits: NaN pattern is the larger int (no clamp here)
    CHECK(VuFloat::FpMax(kPosInf, kPosMax) == kPosInf);
    CHECK(VuFloat::FpMin(kNegInf, kNegOne) == kNegInf);
    VR_reg dest, fs;
    dest.I[0] = dest.I[1] = dest.I[2] = dest.I[3] = 0x55555555u;
    fs.I[0] = kOne; fs.I[1] = kNegOne; fs.I[2] = kNegZero; fs.I[3] = kTwo;
    const uint32_t ft[4] = {kTwo, kTwo, kPosZero, kOne};
    VuFloat::ApplyMinMaxRaw(dest, fs, ft, 0xE, false, &VuFloat::FpMax);
    CHECK(dest.I[0] == kTwo && dest.I[1] == kTwo && dest.I[2] == kPosZero && dest.I[3] == 0x55555555u);
    VuFloat::ApplyMinMaxRaw(dest, fs, ft, 0xF, true, &VuFloat::FpMin);     // VF0 protected: no write at all
    CHECK(dest.I[0] == kTwo && dest.I[3] == 0x55555555u);
    VuFloat::ApplyMinMaxRaw(dest, fs, ft, 0x1, false, &VuFloat::FpMin);
    CHECK(dest.I[3] == kOne);
}

static void TestDiv() {
    VuChopRoundGuard guard;
    CHECK(VuFloat::VuDiv(1.0f, 2.0f) == 0.5f);
    CHECK(VuFloat::F2Bits(VuFloat::VuDiv(1.0f, 0.0f)) == kPosMax);
    CHECK(VuFloat::F2Bits(VuFloat::VuDiv(-1.0f, 0.0f)) == kNegMax);
    CHECK(VuFloat::F2Bits(VuFloat::VuDiv(1.0f, -0.0f)) == kNegMax);
    CHECK(VuFloat::F2Bits(VuFloat::VuDiv(-1.0f, -0.0f)) == kPosMax);
    CHECK(VuFloat::F2Bits(VuFloat::VuDiv(0.0f, 0.0f)) == kPosMax);           // 0/0: +MAX (signs equal)
    CHECK(VuFloat::F2Bits(VuFloat::VuDiv(-0.0f, 0.0f)) == kNegMax);
    CHECK(VuFloat::F2Bits(VuFloat::VuDiv(1.0f, VuFloat::Bits2F(kPosDenorm))) == kPosMax);  // denormal divisor flushes to 0
    CHECK(VuFloat::F2Bits(VuFloat::VuDiv(VuFloat::Bits2F(kPosMax), 0.5f)) == kPosMax);     // overflow clamps
    CHECK(VuFloat::F2Bits(VuFloat::VuDiv(VuFloat::Bits2F(kNaN), 1.0f)) == kPosMax);        // NaN numerator clamps first
    // Truncation: 1/3 rounds toward zero, i.e. below the nearest-rounded quotient
    const uint32_t third = VuFloat::F2Bits(VuFloat::VuDiv(1.0f, 3.0f));
    CHECK(third == 0x3EAAAAAAu);
}

static void TestFtoiItof() {
    VuChopRoundGuard guard;
    CHECK(VuFloat::FtoiSat(1.5f) == 1u);
    CHECK(VuFloat::FtoiSat(-1.5f) == static_cast<uint32_t>(-1));
    CHECK(VuFloat::FtoiSat(0.0f) == 0u && VuFloat::FtoiSat(-0.0f) == 0u);
    CHECK(VuFloat::FtoiSat(2147483520.0f) == 0x7FFFFF80u);                   // largest float below 2^31
    CHECK(VuFloat::FtoiSat(2147483648.0f) == 0x7FFFFFFFu);                   // 2^31 saturates
    CHECK(VuFloat::FtoiSat(-2147483648.0f) == 0x80000000u);                  // -2^31: the edge saturates by sign
    CHECK(VuFloat::FtoiSat(VuFloat::Bits2F(kPosInf)) == 0x7FFFFFFFu);
    CHECK(VuFloat::FtoiSat(VuFloat::Bits2F(kNegInf)) == 0x80000000u);
    CHECK(VuFloat::FtoiSat(VuFloat::Bits2F(kNaN)) == 0x7FFFFFFFu);
    CHECK(VuFloat::FtoiSat(VuFloat::Bits2F(kNegNaN)) == 0x80000000u);
    CHECK(VuFloat::FtoiSat(VuFloat::Bits2F(0x4EFFFFFFu)) == 0x7FFFFF80u);    // just below the exponent edge
    CHECK(VuFloat::FtoiSat(VuFloat::Bits2F(0xCF000000u)) == 0x80000000u);
    CHECK(VuFloat::Ftoi0(kOne) == 1u && VuFloat::Ftoi0(kNegOne) == 0xFFFFFFFFu);
    CHECK(VuFloat::Ftoi0(kPosDenorm) == 0u && VuFloat::Ftoi0(kPosInf) == 0x7FFFFFFFu);
    CHECK(VuFloat::Ftoi4(kOne) == 16u && VuFloat::Ftoi12(kOne) == 4096u && VuFloat::Ftoi15(kOne) == 32768u);
    CHECK(VuFloat::Ftoi4(VuFloat::F2Bits(-2.75f)) == static_cast<uint32_t>(-44));
    CHECK(VuFloat::Ftoi12(VuFloat::F2Bits(0.5f)) == 2048u);
    CHECK(VuFloat::Ftoi15(VuFloat::F2Bits(65536.0f)) == 0x7FFFFFFFu);       // 2^16 * 2^15 = 2^31 saturates
    CHECK(VuFloat::Ftoi15(VuFloat::F2Bits(-65536.0f)) == 0x80000000u);
    CHECK(VuFloat::Ftoi4(kPosInf) == 0x7FFFFFFFu && VuFloat::Ftoi4(kNegNaN) == 0x80000000u);
    CHECK(VuFloat::Itof0(1u) == kOne && VuFloat::Itof0(0xFFFFFFFFu) == kNegOne && VuFloat::Itof0(0) == kPosZero);
    CHECK(VuFloat::Itof0(0x80000000u) == 0xCF000000u);
    CHECK(VuFloat::Itof0(16777217u) == 0x4B800000u);                         // 2^24+1 truncates toward zero under the guard
}

static void TestChopGuard() {
    std::fesetround(FE_TONEAREST);
    {
        VuChopRoundGuard g;
        CHECK(std::fegetround() == FE_TOWARDZERO);
        volatile float a = 1.0f, b = 3.0f;
        const float q = a / b;
        CHECK(VuFloat::F2Bits(q) == 0x3EAAAAAAu);
        {
            VuChopRoundGuard inner;  // nested guard: still toward zero, restores nothing on exit
            CHECK(std::fegetround() == FE_TOWARDZERO);
        }
        CHECK(std::fegetround() == FE_TOWARDZERO);
    }
    CHECK(std::fegetround() == FE_TONEAREST);
    {
        volatile float a = 1.0f, b = 3.0f;
        const float q = a / b;
        CHECK(VuFloat::F2Bits(q) == 0x3EAAAAABu);  // nearest again
    }
}

int main() {
    TestBitsPunning();
    TestClamp();
    TestExp255();
    TestLaneMask();
    TestMacFlags();
    TestQStatus();
    TestMinMax();
    TestDiv();
    TestFtoiItof();
    TestChopGuard();
    if (g_fail) {
        std::printf("VuFloatRules: %d FAILED\n", g_fail);
        return 1;
    }
#if defined(PS2R_SIMD) && PS2R_SIMD && defined(__aarch64__)
    std::printf("VuFloatRules: PASS (scalar + NEON ClampBits4/MacFlags4 twins)\n");
#elif PS2R_VU_SSE
    std::printf("VuFloatRules: PASS (scalar + SSE2 ClampBits4S/MacFlags4S twins)\n");
#else
    std::printf("VuFloatRules: PASS (scalar)\n");
#endif
    return 0;
}
