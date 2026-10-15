// VuInterpreterTier -- the upper (FMAC) word and the FDIV-pipe bodies.
//
// PS2 rules: every upper instruction reads clamped copies of VF[fs]/VF[ft]
// (NaN/Inf -> +/-MAX, denormal -> +/-0) and re-clamps ACC in place; the
// MAC/STATUS flags are judged on the RAW result before the written lanes are
// clamped; a write to VF00 is dropped but its flags still issue; MAX/MINI
// compare raw bits as integers and set no flags; the MADD/MSUB family is a
// single-rounding fused multiply-add (std::fmaf); FTOI/ITOF/ABS convert the
// raw source lanes; the FDIV pipe's D/I status bits accompany the quotient.
#include "engine/vu/tiers/vu_interpreter_tier.h"

#include <cmath>

#include "engine/vu/vu_decoder.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_unit.h"

namespace {

using vu_status::D;
using vu_status::DS;
using vu_status::I;
using vu_status::IS;

inline float Fma(float a, float b, float c) { return std::fmaf(a, b, c); }

// result.k = op(src1.k, scalar) for the four lanes.
template <typename Op>
inline void Lanes1(VR_reg& result, const VR_reg& src1, float s, Op op) {
    for (int k = 0; k < 4; ++k) result.F[k] = op(src1.F[k], s);
}
template <typename Op>
inline void Lanes2(VR_reg& result, const VR_reg& src1, const VR_reg& src2, Op op) {
    for (int k = 0; k < 4; ++k) result.F[k] = op(src1.F[k], src2.F[k]);
}

inline float OpAdd(float a, float b) { return a + b; }
inline float OpSub(float a, float b) { return a - b; }
inline float OpMul(float a, float b) { return a * b; }

}  // namespace

void VuInterpreterTier::RunUpper(VuUnit& u, uint32_t upper) {
    VUregs& regs = u.regs();
    FlagPipeline& pipes = u.flags();
    const uint32_t func = upper & 0x3Fu;
    const uint32_t fd = (upper >> 6) & 0x1Fu;
    const uint32_t fs = (upper >> 11) & 0x1Fu;
    const uint32_t ft = (upper >> 16) & 0x1Fu;
    const uint8_t dest = static_cast<uint8_t>((upper >> 21) & 0xFu);
    const uint32_t bc = func & 3u;

    const VR_reg src1 = Clamp4(regs.VF[fs]);
    const VR_reg src2 = Clamp4(regs.VF[ft]);
    VuFloat::ClampStoreACC(regs.ACC);  // architecturally visible for every upper, NOP included
    const bool protect_fd = (fd == 0);
    VR_reg result = {};

    if (func >= 0x3C) {
        RunSpecial2Upper(u, upper);
        return;
    }

    const VR_reg& acc = regs.ACC;
    auto write_fd = [&]() {
        if (!protect_fd) VuFloat::WriteMaskClamp(regs.VF[fd], result.I, dest);
        pipes.UpdateMACFlags(regs, result, dest);
    };
    auto minmax_bc = [&](uint32_t bits, VuFloat::MinMaxFn fn) {
        const uint32_t b4[4] = {bits, bits, bits, bits};
        VuFloat::ApplyMinMaxRaw(regs.VF[fd], regs.VF[fs], b4, dest, protect_fd, fn);
    };
    auto madd_s = [&](float s) {
        for (int k = 0; k < 4; ++k) result.F[k] = Fma(src1.F[k], s, acc.F[k]);
    };
    auto msub_s = [&](float s) {
        for (int k = 0; k < 4; ++k) result.F[k] = Fma(-src1.F[k], s, acc.F[k]);
    };

    const float bc_val = src2.F[bc];
    switch (func) {
        case 0x00: case 0x01: case 0x02: case 0x03:  // ADDbc
            Lanes1(result, src1, bc_val, OpAdd); write_fd(); break;
        case 0x04: case 0x05: case 0x06: case 0x07:  // SUBbc
            Lanes1(result, src1, bc_val, OpSub); write_fd(); break;
        case 0x08: case 0x09: case 0x0A: case 0x0B:  // MADDbc
            madd_s(bc_val); write_fd(); break;
        case 0x0C: case 0x0D: case 0x0E: case 0x0F:  // MSUBbc
            msub_s(bc_val); write_fd(); break;
        case 0x10: case 0x11: case 0x12: case 0x13:  // MAXbc
            minmax_bc(regs.VF[ft].I[bc], VuFloat::FpMax); break;
        case 0x14: case 0x15: case 0x16: case 0x17:  // MINIbc
            minmax_bc(regs.VF[ft].I[bc], VuFloat::FpMin); break;
        case 0x18: case 0x19: case 0x1A: case 0x1B:  // MULbc
            Lanes1(result, src1, bc_val, OpMul); write_fd(); break;
        case 0x1C: Lanes1(result, src1, pipes.VisibleQ(regs), OpMul); write_fd(); break;                    // MULq
        case 0x1D: minmax_bc(regs.I, VuFloat::FpMax); break;                                                // MAXi
        case 0x1E: Lanes1(result, src1, VuFloat::ClampFloat(VuFloat::Bits2F(regs.I)), OpMul); write_fd(); break;  // MULi
        case 0x1F: minmax_bc(regs.I, VuFloat::FpMin); break;                                                // MINIi
        case 0x20: Lanes1(result, src1, pipes.VisibleQ(regs), OpAdd); write_fd(); break;                    // ADDq
        case 0x21: madd_s(pipes.VisibleQ(regs)); write_fd(); break;                                         // MADDq
        case 0x22: Lanes1(result, src1, VuFloat::ClampFloat(VuFloat::Bits2F(regs.I)), OpAdd); write_fd(); break;  // ADDi
        case 0x23: madd_s(VuFloat::ClampFloat(VuFloat::Bits2F(regs.I))); write_fd(); break;                 // MADDi
        case 0x24: Lanes1(result, src1, pipes.VisibleQ(regs), OpSub); write_fd(); break;                    // SUBq
        case 0x25: msub_s(pipes.VisibleQ(regs)); write_fd(); break;                                         // MSUBq
        case 0x26: Lanes1(result, src1, VuFloat::ClampFloat(VuFloat::Bits2F(regs.I)), OpSub); write_fd(); break;  // SUBi
        case 0x27: msub_s(VuFloat::ClampFloat(VuFloat::Bits2F(regs.I))); write_fd(); break;                 // MSUBi
        case 0x28: Lanes2(result, src1, src2, OpAdd); write_fd(); break;                                    // ADD
        case 0x29:                                                                                          // MADD
            for (int k = 0; k < 4; ++k) result.F[k] = Fma(src1.F[k], src2.F[k], acc.F[k]);
            write_fd();
            break;
        case 0x2A: Lanes2(result, src1, src2, OpMul); write_fd(); break;                                    // MUL
        case 0x2B: VuFloat::ApplyMinMaxRaw(regs.VF[fd], regs.VF[fs], regs.VF[ft].I, dest, protect_fd, VuFloat::FpMax); break;  // MAX
        case 0x2C: Lanes2(result, src1, src2, OpSub); write_fd(); break;                                    // SUB
        case 0x2D:                                                                                          // MSUB
            for (int k = 0; k < 4; ++k) result.F[k] = Fma(-src1.F[k], src2.F[k], acc.F[k]);
            write_fd();
            break;
        case 0x2E:                                                                                          // OPMSUB
            result.x = Fma(-src1.y, src2.z, acc.x);
            result.y = Fma(-src1.z, src2.x, acc.y);
            result.z = Fma(-src1.x, src2.y, acc.z);
            result.w = acc.w;
            write_fd();
            break;
        case 0x2F: VuFloat::ApplyMinMaxRaw(regs.VF[fd], regs.VF[fs], regs.VF[ft].I, dest, protect_fd, VuFloat::FpMin); break;  // MINI
        default:  // 0x30-0x3B: unused encodings, no effect
            break;
    }
}

void VuInterpreterTier::RunSpecial2Upper(VuUnit& u, uint32_t upper) {
    VUregs& regs = u.regs();
    FlagPipeline& pipes = u.flags();
    const uint32_t func = upper & 0x3Fu;
    const uint32_t bc = func & 3u;
    const uint32_t op_code = (upper >> 6) & 0x1Fu;
    const uint32_t fs = (upper >> 11) & 0x1Fu;
    const uint32_t ft = (upper >> 16) & 0x1Fu;
    const uint8_t dest = static_cast<uint8_t>((upper >> 21) & 0xFu);
    const uint32_t fsf = (upper >> 21) & 3u;
    const uint32_t ftf = (upper >> 23) & 3u;
    const bool protect_ft = (ft == 0);

    const VR_reg src1 = regs.VF[fs];  // raw copies (ITOF/FTOI/ABS/CLIP judge the raw bits)
    const VR_reg src2 = regs.VF[ft];
    const VR_reg acc_in = regs.ACC;   // already clamped by RunUpper's prologue
    const VR_reg s1 = Clamp4(src1);
    const VR_reg s2 = Clamp4(src2);
    const VR_reg acc_c = Clamp4(acc_in);
    const float bc_val = s2.F[bc];
    const float i_val = VuFloat::ClampFloat(VuFloat::Bits2F(regs.I));

    // ACC-family tail: lanes outside dest keep the incoming ACC, flags judge
    // the raw ACC, then the dest lanes are clamped in place.
    auto acc_tail = [&]() {
        for (int k = 0; k < 4; ++k) {
            if (!(dest & (8u >> k))) regs.ACC.I[k] = acc_in.I[k];
        }
        pipes.UpdateMACFlags(regs, regs.ACC, dest);
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) regs.ACC.I[k] = VuFloat::ClampBits1(regs.ACC.I[k]);
        }
    };
    auto acc_s = [&](float s, float (*op)(float, float)) {
        for (int k = 0; k < 4; ++k) regs.ACC.F[k] = op(s1.F[k], s);
        acc_tail();
    };
    auto acc_v = [&](float (*op)(float, float)) {
        for (int k = 0; k < 4; ++k) regs.ACC.F[k] = op(s1.F[k], s2.F[k]);
        acc_tail();
    };
    auto acc_madd_s = [&](float s) {
        for (int k = 0; k < 4; ++k) regs.ACC.F[k] = Fma(s1.F[k], s, acc_c.F[k]);
        acc_tail();
    };
    auto acc_msub_s = [&](float s) {
        for (int k = 0; k < 4; ++k) regs.ACC.F[k] = Fma(-s1.F[k], s, acc_c.F[k]);
        acc_tail();
    };
    auto write_ft_vec = [&](const VR_reg& v) {
        if (protect_ft) return;
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) regs.VF[ft].I[k] = v.I[k];
        }
    };

    static const float kScale[4] = {1.0f, 16.0f, 4096.0f, 32768.0f};

    switch (op_code) {
        case 0x00: acc_s(bc_val, OpAdd); break;   // ADDAbc
        case 0x01: acc_s(bc_val, OpSub); break;   // SUBAbc
        case 0x02: acc_madd_s(bc_val); break;     // MADDAbc
        case 0x03: acc_msub_s(bc_val); break;     // MSUBAbc
        case 0x04: {                              // ITOF0/4/12/15: raw integer lanes -> float / scale
            VR_reg v = {};
            for (int k = 0; k < 4; ++k) v.F[k] = static_cast<float>(static_cast<int32_t>(src1.I[k])) / kScale[bc];
            write_ft_vec(v);
            break;
        }
        case 0x05: {                              // FTOI0/4/12/15: raw lanes * scale, saturating truncation
            VR_reg v = {};
            for (int k = 0; k < 4; ++k) v.I[k] = VuFloat::FtoiSat(src1.F[k] * kScale[bc]);
            write_ft_vec(v);
            break;
        }
        case 0x06: acc_s(bc_val, OpMul); break;   // MULAbc
        // Rows 7/8/9 of the special-2 table (op in the fd field, the func's low
        // two bits select the column): func 0x3C is the Q column, 0x3D the
        // ABS / MADDAq / MSUBAq column, 0x3E the I column, 0x3F CLIP / MADDAi /
        // MSUBAi -- the VU user's manual table that the reference decodes.
        case 0x07:
            if (bc == 0) acc_s(pipes.VisibleQ(regs), OpMul);   // MULAq: ACC = fs * Q
            else if (bc == 1) {                   // ABS: raw source, sign cleared, no flags
                VR_reg v = {};
                for (int k = 0; k < 4; ++k) v.F[k] = std::fabs(src1.F[k]);
                write_ft_vec(v);
            } else if (bc == 2) {                 // MULAi
                acc_s(i_val, OpMul);
            } else {                              // CLIP
                RunClip(u, src1, src2);
            }
            break;
        case 0x08:
            if (bc == 0) acc_s(pipes.VisibleQ(regs), OpAdd);   // ADDAq: ACC = fs + Q
            else if (bc == 1) acc_madd_s(pipes.VisibleQ(regs)); // MADDAq
            else if (bc == 2) acc_s(i_val, OpAdd);          // ADDAi
            else acc_madd_s(i_val);                         // MADDAi
            break;
        case 0x09:
            if (bc == 0) acc_s(pipes.VisibleQ(regs), OpSub);   // SUBAq: ACC = fs - Q
            else if (bc == 1) acc_msub_s(pipes.VisibleQ(regs)); // MSUBAq
            else if (bc == 2) acc_s(i_val, OpSub);          // SUBAi
            else acc_msub_s(i_val);                         // MSUBAi
            break;
        case 0x0A:
            if (bc == 0) acc_v(OpAdd);                      // ADDA
            else if (bc == 1) {                             // MADDA
                for (int k = 0; k < 4; ++k) regs.ACC.F[k] = Fma(s1.F[k], s2.F[k], acc_c.F[k]);
                acc_tail();
            } else if (bc == 2) acc_v(OpMul);               // MULA
            else acc_tail();                                // unrecognised: the tail still issues the current ACC's flags
            break;
        case 0x0B:
            if (bc == 0) acc_v(OpSub);                      // SUBA
            else if (bc == 1) {                             // MSUBA
                for (int k = 0; k < 4; ++k) regs.ACC.F[k] = Fma(-s1.F[k], s2.F[k], acc_c.F[k]);
                acc_tail();
            } else if (bc == 2) {                           // OPMULA (w untouched)
                regs.ACC.x = s1.y * s2.z;
                regs.ACC.y = s1.z * s2.x;
                regs.ACC.z = s1.x * s2.y;
                acc_tail();
            }
            break;                                          // bc 3: NOP
        // Encodings no VU assembler emits (op_code >= 0x0C) that the reference
        // executes as legacy lower bodies; kept so garbage images compare equal.
        case 0x18: write_ft_vec(src2); break;               // MOVE-as-upper
        case 0x19: {                                        // MR32-as-upper
            VR_reg v = {};
            v.x = src2.y; v.y = src2.z; v.z = src2.w; v.w = src2.x;
            write_ft_vec(v);
            break;
        }
        case 0x1C: DoDiv(u, fs, fsf, ft, ftf); break;
        case 0x1D: DoSqrt(u, ft, ftf); break;
        case 0x1E: DoRsqrt(u, fs, fsf, ft, ftf); break;
        case 0x1F: pipes.FlushQPipe(regs); break;           // WAITQ-as-upper
        default:
            break;
    }
}

void VuInterpreterTier::RunClip(VuUnit& u, const VR_reg& fs_raw, const VR_reg& ft_raw) {
    // Integer-compare clip judgement: |w| as an int (a zero/denormal w compares
    // as the largest denormal), each of +x/-x/+y/-y/+z/-z tested "outside" as a
    // signed compare with the sign bit flipped for the negative side.
    const int32_t wbits = static_cast<int32_t>(ft_raw.I[3]);
    const int32_t value = (wbits & 0x7F800000) ? (wbits & 0x7FFFFFFF) : 0x007FFFFF;
    uint32_t nf = 0;
    for (int k = 0; k < 3; ++k) {
        const uint32_t b = fs_raw.I[k];
        if (static_cast<int32_t>(b) > value) nf |= 1u << (2 * k);
        if (static_cast<int32_t>(b ^ 0x80000000u) > value) nf |= 1u << (2 * k + 1);
    }
    FlagPipeline& pipes = u.flags();
    pipes.PushClip(u.regs(), (pipes.clip_latest() << 6) | nf);
}

void VuInterpreterTier::DoDiv(VuUnit& u, uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf) {
    VUregs& regs = u.regs();
    const float num = VuFloat::ClampFloat(regs.VF[fs].F[fsf]);
    const float den = VuFloat::ClampFloat(regs.VF[ft].F[ftf]);
    float q;
    uint16_t st;
    if (den != 0.0f) {
        q = num / den;
        st = 0;
    } else {
        st = (num == 0.0f) ? static_cast<uint16_t>(I | IS) : static_cast<uint16_t>(D | DS);
        q = VuFloat::Bits2F(((VuFloat::F2Bits(num) ^ VuFloat::F2Bits(den)) & VuFloat::kSignBit) | VuFloat::kMaxFloatBits);
    }
    u.flags().PushQ(regs, q, FlagPipeline::kDivDelay, st);
}

void VuInterpreterTier::DoSqrt(VuUnit& u, uint32_t ft, uint32_t ftf) {
    VUregs& regs = u.regs();
    const float v = VuFloat::ClampFloat(regs.VF[ft].F[ftf]);
    const uint16_t st = (v < 0.0f) ? static_cast<uint16_t>(I | IS) : 0;
    u.flags().PushQ(regs, std::sqrt(std::fabs(v)), FlagPipeline::kSqrtDelay, st);
}

void VuInterpreterTier::DoRsqrt(VuUnit& u, uint32_t fs, uint32_t fsf, uint32_t ft, uint32_t ftf) {
    VUregs& regs = u.regs();
    const float num = VuFloat::ClampFloat(regs.VF[fs].F[fsf]);
    const float d = VuFloat::ClampFloat(regs.VF[ft].F[ftf]);
    const uint32_t sign = (VuFloat::F2Bits(num) ^ VuFloat::F2Bits(d)) & VuFloat::kSignBit;
    uint16_t st = 0;
    float q;
    if (d == 0.0f) {
        st |= static_cast<uint16_t>(D | DS);
        if (num != 0.0f) {
            q = VuFloat::Bits2F(sign | VuFloat::kMaxFloatBits);
        } else {
            st |= static_cast<uint16_t>(I | IS);
            q = VuFloat::Bits2F(sign);
        }
    } else {
        if (d < 0.0f) st |= static_cast<uint16_t>(I | IS);
        q = num / std::sqrt(std::fabs(d));
    }
    u.flags().PushQ(regs, q, FlagPipeline::kRsqrtDelay, st);
}
