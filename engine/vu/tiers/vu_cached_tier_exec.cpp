// VuCachedTier -- the fast instruction bodies keyed on the decoded kinds.
// Each body is the interpreter's body for that instruction with the decode and
// the unused work removed; the arithmetic goes through the VuFloat lane
// kernels (Add4/Sub4/Mul4/Fma4/Fms4: the interpreter's per-lane expressions,
// single-rounding fma for the MADD/MSUB family; NEON twins under PS2R_SIMD),
// with clamped copies of the VF sources, ACC re-clamped before every upper and
// the MAC flags judged on the raw result. Kinds without a fast form run the
// interpreter's body.
//
// Every body is a template over its kind (ExecUpperK<K> / ExecLowerK<K>), so
// the per-kind choice inside a family folds away at compile time; the two
// tables (UpperFastFn / LowerFastFn) hold one resolved body per kind and
// serve both the cached tier's per-bundle dispatch and the block tier's
// pre-resolved ops. The second template parameter D (-1 = dynamic) makes the
// dest mask a constant too: the (kind, dest) tables (UpperFastFnD /
// LowerFastFnD) are the AOT tier's specialised bodies -- the SAME text, the
// field read replaced by the parameter, so dynamic and specialised are the
// identity by construction (and proven so by Vu1AotSpecializer).
#include "engine/vu/tiers/vu_cached_tier.h"

#include <array>
#include <cmath>
#include <cstring>
#include <utility>

#include "engine/platform/host/platform.h"
#include "engine/vu/tiers/vu_interpreter_tier.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_unit.h"

namespace {

inline uint32_t Le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
inline void StoreLe32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, sizeof(v)); }

// Quadword byte address of (vi + imm) with the unit's data mask.
inline uint32_t QwAddr(uint16_t vi, int32_t imm, uint32_t mask) {
    return static_cast<uint32_t>((static_cast<int32_t>(vi) + imm) * 16) & mask;
}

// ILW word offset: the first selected lane in X,Y,Z priority, else W.
inline uint32_t IlwOffset(uint32_t dest) {
    if (dest & vu_dest::X) return 0;
    if (dest & vu_dest::Y) return 4;
    if (dest & vu_dest::Z) return 8;
    return 12;
}

// ITOFn / FTOIn scale by the bc field (0/4/12/15 fractional bits): the interpreter's kScale.
const float kItofFtoiScale[4] = {1.0f, 16.0f, 4096.0f, 32768.0f};

// The 12-bit immediate of the FS* instructions: bit 21 above the low 11 bits.
inline uint32_t Imm12(uint32_t l) { return ((l >> 10) & 0x800u) | (l & 0x7FFu); }

inline int32_t SignExtend5(uint32_t v) {
    v &= 0x1Fu;
    return static_cast<int32_t>(static_cast<int8_t>((v & 0x10u) ? (v | 0xE0u) : v));
}

// The upper families (compile-time predicates over the kind).
constexpr bool IsFmacBcKind(uint8_t k) {
    return k == VUK_U_ADD_BC || k == VUK_U_SUB_BC || k == VUK_U_MUL_BC || k == VUK_U_MADD_BC || k == VUK_U_MSUB_BC;
}
constexpr bool IsFmacVecKind(uint8_t k) {
    return k == VUK_U_ADD || k == VUK_U_SUB || k == VUK_U_MUL || k == VUK_U_MADD || k == VUK_U_MSUB;
}
constexpr bool IsFmacKind(uint8_t k) {
    return IsFmacBcKind(k) || IsFmacVecKind(k) || k == VUK_U_MULQ || k == VUK_U_MUL_I || k == VUK_U_ADD_I;
}
constexpr bool IsAccKind(uint8_t k) {
    return k == VUK_U_ADDA_BC || k == VUK_U_SUBA_BC || k == VUK_U_MULA_BC || k == VUK_U_MADDA_BC || k == VUK_U_MSUBA_BC ||
           k == VUK_U_MULA || k == VUK_U_MADDA;
}

// The dest mask of a body: the template constant when specialised, the
// slot's field otherwise (the one place the two forms differ).
template <int D>
PS2R_FORCE_INLINE uint8_t UpperDest(const VuDecodedSlot& s) {
    if constexpr (D >= 0) return static_cast<uint8_t>(D);
    else return s.u_dest;
}
template <int D>
PS2R_FORCE_INLINE uint8_t LowerDest(const VuDecodedSlot& s) {
    if constexpr (D >= 0) return static_cast<uint8_t>(D);
    else return s.l_dest;
}

// VuFloat::WriteMaskClamp with the mask a constant: the full mask is a plain
// clamped store, a partial one a lane select on a constant vector (the table
// load goes away); the dynamic form is VuFloat's.
template <int D>
PS2R_FORCE_INLINE void WriteMaskClampD(VR_reg& dst, const VR_reg& result, uint8_t dest) {
    if constexpr (D < 0) {
        VuFloat::WriteMaskClamp(dst, result.I, dest);
    } else if constexpr (D == vu_dest::XYZW) {
        dst = VuFloat::ClampVec(result);
    } else {
#if PS2R_VU_NEON
        const uint32x4_t lm = {(D & 8) ? ~0u : 0u, (D & 4) ? ~0u : 0u, (D & 2) ? ~0u : 0u, (D & 1) ? ~0u : 0u};
        vst1q_u32(dst.I, vbslq_u32(lm, VuFloat::ClampBits4(vld1q_u32(result.I)), vld1q_u32(dst.I)));
#else
        if constexpr ((D & 8) != 0) dst.I[0] = VuFloat::ClampBits1(result.I[0]);
        if constexpr ((D & 4) != 0) dst.I[1] = VuFloat::ClampBits1(result.I[1]);
        if constexpr ((D & 2) != 0) dst.I[2] = VuFloat::ClampBits1(result.I[2]);
        if constexpr ((D & 1) != 0) dst.I[3] = VuFloat::ClampBits1(result.I[3]);
#endif
    }
}

}  // namespace

// FMAC result family (fd written): the VuFloat lane kernels compute the raw
// result from clamped source copies and the re-clamped ACC; the dest lanes of
// fd take the clamped result (VF00 protected) and the flags judge the raw one.
template <uint8_t K, int D>
void VuCachedTier::ExecUpperFmac(VuUnit& u, const VuDecodedSlot& s) {
    static_assert(IsFmacKind(K), "an FMAC result kind");
    VUregs& regs = u.regs();
    const uint32_t fd = s.u_fd, fs = s.u_fs, ft = s.u_ft, bc = s.u_bc;
    const uint8_t dest = UpperDest<D>(s);
    const VR_reg s1 = VuFloat::ClampVec(regs.VF[fs]);
    VuFloat::ClampStoreACC(regs.ACC);
    VR_reg result;
    if constexpr (IsFmacBcKind(K)) {
        const VR_reg bv = VuFloat::Splat4(VuFloat::Bits2F(VuFloat::ClampBits1(regs.VF[ft].I[bc])));
        if constexpr (K == VUK_U_ADD_BC) result = VuFloat::Add4(s1, bv);
        else if constexpr (K == VUK_U_SUB_BC) result = VuFloat::Sub4(s1, bv);
        else if constexpr (K == VUK_U_MUL_BC) result = VuFloat::Mul4(s1, bv);
        else if constexpr (K == VUK_U_MADD_BC) result = VuFloat::Fma4(s1, bv, regs.ACC);
        else result = VuFloat::Fms4(s1, bv, regs.ACC);
    } else if constexpr (IsFmacVecKind(K)) {
        const VR_reg s2 = VuFloat::ClampVec(regs.VF[ft]);
        if constexpr (K == VUK_U_ADD) result = VuFloat::Add4(s1, s2);
        else if constexpr (K == VUK_U_SUB) result = VuFloat::Sub4(s1, s2);
        else if constexpr (K == VUK_U_MUL) result = VuFloat::Mul4(s1, s2);
        else if constexpr (K == VUK_U_MADD) result = VuFloat::Fma4(s1, s2, regs.ACC);
        else result = VuFloat::Fms4(s1, s2, regs.ACC);
    } else if constexpr (K == VUK_U_MULQ) {
        result = VuFloat::Mul4(s1, VuFloat::Splat4(u.flags().VisibleQ(regs)));
    } else if constexpr (K == VUK_U_MUL_I) {
        result = VuFloat::Mul4(s1, VuFloat::Splat4(VuFloat::ClampFloat(VuFloat::Bits2F(regs.I))));
    } else {  // ADD_I
        result = VuFloat::Add4(s1, VuFloat::Splat4(VuFloat::ClampFloat(VuFloat::Bits2F(regs.I))));
    }
    if (fd != 0) WriteMaskClampD<D>(regs.VF[fd], result, dest);
    u.flags().UpdateMACFlags(regs, result, dest);
}

// ACC family: lanes outside dest keep the incoming (clamped) ACC, the flags
// judge the raw ACC, then the dest lanes are clamped in place.
template <uint8_t K, int D>
void VuCachedTier::ExecUpperAcc(VuUnit& u, const VuDecodedSlot& s) {
    static_assert(IsAccKind(K), "an ACC kind");
    VUregs& regs = u.regs();
    const uint32_t fs = s.u_fs, ft = s.u_ft, bc = s.u_bc;
    const uint8_t dest = UpperDest<D>(s);
    VuFloat::ClampStoreACC(regs.ACC);
    const VR_reg acc_in = regs.ACC;
    const VR_reg s1 = VuFloat::ClampVec(regs.VF[fs]);
    const VR_reg s2 = VuFloat::ClampVec(regs.VF[ft]);
    const VR_reg acc_c = VuFloat::ClampVec(acc_in);
    const VR_reg bv = VuFloat::Splat4(s2.F[bc]);
    VR_reg acc;
    if constexpr (K == VUK_U_ADDA_BC) acc = VuFloat::Add4(s1, bv);
    else if constexpr (K == VUK_U_SUBA_BC) acc = VuFloat::Sub4(s1, bv);
    else if constexpr (K == VUK_U_MULA_BC) acc = VuFloat::Mul4(s1, bv);
    else if constexpr (K == VUK_U_MADDA_BC) acc = VuFloat::Fma4(s1, bv, acc_c);
    else if constexpr (K == VUK_U_MSUBA_BC) acc = VuFloat::Fms4(s1, bv, acc_c);
    else if constexpr (K == VUK_U_MULA) acc = VuFloat::Mul4(s1, s2);
    else acc = VuFloat::Fma4(s1, s2, acc_c);  // MADDA
    for (int k = 0; k < 4; ++k) {
        if (!(dest & (8u >> k))) acc.I[k] = acc_in.I[k];
    }
    u.flags().UpdateMACFlags(regs, acc, dest);
    WriteMaskClampD<D>(regs.ACC, acc, dest);  // dest lanes clamped in place; the others are acc_in already
}

template <uint8_t K, int D>
void VuCachedTier::ExecUpperK(VuInterpreterTier& g, VuUnit& u, const VuDecodedSlot& s) {
    VUregs& regs = u.regs();
    const uint32_t fd = s.u_fd, fs = s.u_fs, ft = s.u_ft, bc = s.u_bc;
    const uint8_t dest = UpperDest<D>(s);

    auto minmax_bc = [&](uint32_t bits, bool is_max) {
        VR_reg b4;
        b4.I[0] = b4.I[1] = b4.I[2] = b4.I[3] = bits;
        VuFloat::ApplyMinMax4(regs.VF[fd], regs.VF[fs], b4, dest, fd == 0, is_max);
    };
    auto write_ft_vec = [&](const VR_reg& v) {
        if (ft == 0) return;
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) regs.VF[ft].I[k] = v.I[k];
        }
    };

    if constexpr (K == VUK_U_NOP) {
        VuFloat::ClampStoreACC(regs.ACC);
    } else if constexpr (IsFmacKind(K)) {
        ExecUpperFmac<K, D>(u, s);
    } else if constexpr (IsAccKind(K)) {
        ExecUpperAcc<K, D>(u, s);
    } else if constexpr (K == VUK_U_MAX_BC) {
        VuFloat::ClampStoreACC(regs.ACC);
        minmax_bc(regs.VF[ft].I[bc], true);
    } else if constexpr (K == VUK_U_MINI_BC) {
        VuFloat::ClampStoreACC(regs.ACC);
        minmax_bc(regs.VF[ft].I[bc], false);
    } else if constexpr (K == VUK_U_MINI_I) {
        VuFloat::ClampStoreACC(regs.ACC);
        minmax_bc(regs.I, false);
    } else if constexpr (K == VUK_U_MAX) {
        VuFloat::ClampStoreACC(regs.ACC);
        VuFloat::ApplyMinMax4(regs.VF[fd], regs.VF[fs], regs.VF[ft], dest, fd == 0, true);
    } else if constexpr (K == VUK_U_MINI) {
        VuFloat::ClampStoreACC(regs.ACC);
        VuFloat::ApplyMinMax4(regs.VF[fd], regs.VF[fs], regs.VF[ft], dest, fd == 0, false);
    } else if constexpr (K == VUK_U_FTOI0 || K == VUK_U_FTOI4 || K == VUK_U_FTOI_N) {
        // raw lanes * (1 / 16 / 4096 / 32768 by bc), saturating truncation
        VuFloat::ClampStoreACC(regs.ACC);
        write_ft_vec(VuFloat::Ftoi4Scaled(regs.VF[fs], kItofFtoiScale[bc]));
    } else if constexpr (K == VUK_U_ITOF0 || K == VUK_U_ITOF_N) {  // raw integer lanes -> float / scale
        VuFloat::ClampStoreACC(regs.ACC);
        write_ft_vec(VuFloat::Itof4Scaled(regs.VF[fs], kItofFtoiScale[bc]));
    } else if constexpr (K == VUK_U_CLIP) {
        VuFloat::ClampStoreACC(regs.ACC);
        const VR_reg src1 = regs.VF[fs];
        const VR_reg src2 = regs.VF[ft];
        g.RunClip(u, src1, src2);
    } else {
        static_assert(K == VUK_U_GENERIC, "every fast upper kind has a body above");
        g.RunUpper(u, s.upper);
    }
}

template <uint8_t K, int D>
void VuCachedTier::ExecLowerK(VuInterpreterTier& g, VuUnit& u, const VuDecodedSlot& s, uint32_t pc, bool& branch_taken,
                              uint32_t& branch_target) {
    VUregs& regs = u.regs();
    FlagPipeline& pipes = u.flags();
    uint8_t* data = u.state().data;
    const uint32_t mask = u.mem_mask();
    const uint32_t ft = s.l_ft, fs = s.l_fs;
    const uint32_t vit = ft & 0xFu, vis = fs & 0xFu, vid = s.l_fd & 0xFu;
    const uint32_t dest = LowerDest<D>(s);
    const int32_t imm11 = s.l_imm11;
    branch_taken = false;

    auto set_vi = [&](uint32_t idx, uint32_t v) {
        if (idx != 0) regs.VI[idx] = static_cast<uint16_t>(v);
    };
    auto rel_target = [&]() {
        return static_cast<uint32_t>(static_cast<int32_t>(pc + 8) + imm11 * 8) & kPcMask;
    };
    auto link = [&]() { return static_cast<uint16_t>(((pc + 16) / 8) & VuInterpreterTier::kLinkMask); };
    auto cond_branch = [&](bool cond) {
        if (cond) {
            branch_taken = true;
            branch_target = rel_target();
        }
    };
    auto write_masked_vec = [&](uint32_t reg, const VR_reg& v) {
        if (reg == 0) return;
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) regs.VF[reg].I[k] = v.I[k];
        }
    };
    auto write_masked_all = [&](uint32_t reg, uint32_t bits) {
        if (reg == 0) return;
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) regs.VF[reg].I[k] = bits;
        }
    };
    auto store_masked = [&](uint32_t addr, const VR_reg& v) {
        if (dest == vu_dest::XYZW) {  // the whole quadword (the lanes are little-endian words in order)
            std::memcpy(data + addr, v.I, 16);
            return;
        }
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) StoreLe32(data + addr + 4 * k, v.I[k]);
        }
    };
    auto load_masked = [&](uint32_t reg, uint32_t addr) {
        if (reg == 0) return;
        if (dest == vu_dest::XYZW) {
            std::memcpy(regs.VF[reg].I, data + addr, 16);
            return;
        }
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) regs.VF[reg].I[k] = Le32(data + addr + 4 * k);
        }
    };

    switch (K) {  // K is a constant: the compiler keeps one case
        case VUK_L_NOP:
            break;
        case VUK_L_LQ: load_masked(ft, QwAddr(regs.VI[vis], imm11, mask)); break;
        case VUK_L_SQ: store_masked(QwAddr(regs.VI[vit], imm11, mask), regs.VF[fs]); break;
        case VUK_L_ILW: {
            const uint32_t addr = QwAddr(regs.VI[vis], imm11, mask);
            set_vi(vit, Le32(data + addr + IlwOffset(dest)));
            break;
        }
        case VUK_L_ISW: {
            const uint32_t addr = QwAddr(regs.VI[vis], imm11, mask);
            const uint32_t word = regs.VI[vit];
            for (int k = 0; k < 4; ++k) {
                if (dest & (8u >> k)) StoreLe32(data + addr + 4 * k, word);
            }
            break;
        }
        case VUK_L_IADDIU: set_vi(vit, regs.VI[vis] + s.l_imm15u); break;
        case VUK_L_ISUBIU: set_vi(vit, regs.VI[vis] - s.l_imm15u); break;
        case VUK_L_IBEQ: cond_branch(regs.VI[vis] == regs.VI[vit]); break;
        case VUK_L_IBNE: cond_branch(regs.VI[vis] != regs.VI[vit]); break;
        case VUK_L_IADD: set_vi(vid, regs.VI[vis] + regs.VI[vit]); break;
        case VUK_L_ISUB: set_vi(vid, regs.VI[vis] - regs.VI[vit]); break;
        case VUK_L_IAND: set_vi(vid, regs.VI[vis] & regs.VI[vit]); break;
        case VUK_L_IOR: set_vi(vid, regs.VI[vis] | regs.VI[vit]); break;
        case VUK_L_IADDI:
            set_vi(vit, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(regs.VI[vis])) +
                                              SignExtend5(s.l_fd)));
            break;
        case VUK_L_MOVE: write_masked_vec(ft, regs.VF[fs]); break;
        case VUK_L_MR32: {
            const VR_reg src = regs.VF[fs];  // copied first: fs may equal ft
            VR_reg v = {};
            v.x = src.y; v.y = src.z; v.z = src.w; v.w = src.x;
            write_masked_vec(ft, v);
            break;
        }
        case VUK_L_MTIR: set_vi(vit, regs.VF[fs].I[s.l_fsf] & 0xFFFFu); break;
        case VUK_L_MFIR:
            write_masked_all(ft, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(regs.VI[vis]))));
            break;
        case VUK_L_LQI: {
            const uint32_t addr = (static_cast<uint32_t>(regs.VI[vis]) * 16u) & mask;
            load_masked(ft, addr);
            set_vi(vis, regs.VI[vis] + 1u);
            break;
        }
        case VUK_L_SQI: {
            const uint32_t addr = (static_cast<uint32_t>(regs.VI[vit]) * 16u) & mask;
            store_masked(addr, regs.VF[fs]);
            set_vi(vit, regs.VI[vit] + 1u);
            break;
        }
        case VUK_L_RGET: write_masked_all(ft, regs.R); break;
        case VUK_L_RINIT: regs.R = (regs.VF[fs].I[s.l_fsf] & 0x7FFFFFu) | 0x3F800000u; break;
        case VUK_L_B: cond_branch(true); break;
        case VUK_L_BAL:
            set_vi(vit, link());
            cond_branch(true);
            break;
        case VUK_L_JR:
            branch_taken = true;
            branch_target = (static_cast<uint32_t>(regs.VI[vis]) * 8u) & kPcMask;
            break;
        case VUK_L_JALR:  // the target is read before the link is written
            branch_target = (static_cast<uint32_t>(regs.VI[vis]) * 8u) & kPcMask;
            set_vi(vit, link());
            branch_taken = true;
            break;
        case VUK_L_DIV: g.DoDiv(u, fs, s.l_fsf, ft, (s.lower >> 23) & 3u); break;
        case VUK_L_FCGET: set_vi(vit, pipes.VisibleClip(regs) & 0xFFFu); break;
        case VUK_L_WAITQ: pipes.FlushQPipe(regs); break;
        // Clip-flag tests write VI01 directly (the interpreter's bodies; imm24 = the low 24 bits).
        case VUK_L_FCEQ: regs.VI[1] = (pipes.VisibleClip(regs) == (s.lower & 0xFFFFFFu)) ? 1 : 0; break;
        case VUK_L_FCSET: pipes.SetClipImmediate(regs, s.lower & 0xFFFFFFu); break;
        case VUK_L_FCAND: regs.VI[1] = ((pipes.VisibleClip(regs) & (s.lower & 0xFFFFFFu)) != 0) ? 1 : 0; break;
        case VUK_L_FCOR: regs.VI[1] = ((pipes.VisibleClip(regs) | (s.lower & 0xFFFFFFu)) == 0xFFFFFFu) ? 1 : 0; break;
        // STATUS / MAC readers: retire the due entries first; imm12 = {bit 21, bits 0-10}.
        case VUK_L_FSEQ:
            pipes.RetireMacStatus(regs);
            set_vi(vit, ((regs.status_flag & 0xFFFu) == Imm12(s.lower)) ? 1 : 0);
            break;
        case VUK_L_FSSET:  // sticky bits replaced, the current six kept
            pipes.RetireMacStatus(regs);
            regs.status_flag = static_cast<uint16_t>((regs.status_flag & 0x3Fu) | (Imm12(s.lower) & 0xFC0u));
            break;
        case VUK_L_FSAND:
            pipes.RetireMacStatus(regs);
            set_vi(vit, (regs.status_flag & 0xFFFu) & Imm12(s.lower));
            break;
        case VUK_L_FSOR:
            pipes.RetireMacStatus(regs);
            set_vi(vit, (regs.status_flag & 0xFFFu) | Imm12(s.lower));
            break;
        case VUK_L_FMEQ:
            pipes.RetireMacStatus(regs);
            set_vi(vit, (regs.mac_flag == regs.VI[vis]) ? 1 : 0);
            break;
        case VUK_L_FMAND:
            pipes.RetireMacStatus(regs);
            set_vi(vit, regs.mac_flag & regs.VI[vis]);
            break;
        case VUK_L_FMOR:
            pipes.RetireMacStatus(regs);
            set_vi(vit, regs.mac_flag | regs.VI[vis]);
            break;
        case VUK_L_IBLTZ: cond_branch(static_cast<int16_t>(regs.VI[vis]) < 0); break;
        case VUK_L_IBGTZ: cond_branch(static_cast<int16_t>(regs.VI[vis]) > 0); break;
        case VUK_L_IBLEZ: cond_branch(static_cast<int16_t>(regs.VI[vis]) <= 0); break;
        case VUK_L_IBGEZ: cond_branch(static_cast<int16_t>(regs.VI[vis]) >= 0); break;
        case VUK_L_XTOP: {  // + the VU1 yield rule
            Vu1State& st = u.state();
            set_vi(vit, u.vif_top());
            if (u.id() == 1 && ++st.xtop_read_count > VuInterpreterTier::kXtopYieldReads) st.xtop_exit_requested = true;
            break;
        }
        case VUK_L_XITOP: set_vi(vit, u.vif_itop()); break;  // reads the ITOPS shadow
        case VUK_L_XGKICK:  // VU1 only
            if (XgKick* xgk = u.xgkick()) {
                Vu1State& st = u.state();
                xgk->Arm(data, regs.VI[vis], pipes.cycle());
                ++st.xgkick_count;
                st.xgkick_has_fired = true;
                st.xtop_read_count = 0;
            }
            break;
        default:
            static_assert(K < VUK_L_COUNT, "a lower kind");
            g.RunLower(u, s.lower, pc, branch_taken, branch_target);
            break;
    }
}

// The kind -> body tables: entry K is &ExecUpperK<K> / &ExecLowerK<K>, the
// compile-time recursion unrolled over the kind range once at load.
const std::array<VuCachedTier::UpperFn, VUK_U_COUNT> VuCachedTier::kUpperTable = [] {
    std::array<UpperFn, VUK_U_COUNT> t{};
    auto fill = [&t](auto self, auto kc) {
        constexpr uint8_t K = decltype(kc)::value;
        t[K] = &VuCachedTier::ExecUpperK<K>;
        if constexpr (K + 1 < VUK_U_COUNT) self(self, std::integral_constant<uint8_t, K + 1>{});
    };
    fill(fill, std::integral_constant<uint8_t, 0>{});
    return t;
}();

const std::array<VuCachedTier::LowerFn, VUK_L_COUNT> VuCachedTier::kLowerTable = [] {
    std::array<LowerFn, VUK_L_COUNT> t{};
    auto fill = [&t](auto self, auto kc) {
        constexpr uint8_t K = decltype(kc)::value;
        t[K] = &VuCachedTier::ExecLowerK<K>;
        if constexpr (K + 1 < VUK_L_COUNT) self(self, std::integral_constant<uint8_t, K + 1>{});
    };
    fill(fill, std::integral_constant<uint8_t, 0>{});
    return t;
}();

// The (kind, dest) tables: entry K * 16 + D is &ExecUpperK<K, D>; a lower kind
// that never reads dest keeps its dynamic body in every D slot.
const std::array<VuCachedTier::UpperFn, VUK_U_COUNT * 16> VuCachedTier::kUpperTableD = [] {
    std::array<UpperFn, VUK_U_COUNT * 16> t{};
    auto fill_d = [&t](auto self, auto kc, auto dc) {
        constexpr uint8_t K = decltype(kc)::value;
        constexpr int D = decltype(dc)::value;
        t[K * 16 + D] = &VuCachedTier::ExecUpperK<K, D>;
        if constexpr (D + 1 < 16) self(self, kc, std::integral_constant<int, D + 1>{});
    };
    auto fill_k = [&](auto self, auto kc) {
        fill_d(fill_d, kc, std::integral_constant<int, 0>{});
        constexpr uint8_t K = decltype(kc)::value;
        if constexpr (K + 1 < VUK_U_COUNT) self(self, std::integral_constant<uint8_t, K + 1>{});
    };
    fill_k(fill_k, std::integral_constant<uint8_t, 0>{});
    return t;
}();

bool VuCachedTier::LowerKindReadsDest(uint8_t k) {
    switch (k) {
        case VUK_L_LQ: case VUK_L_SQ: case VUK_L_ILW: case VUK_L_ISW: case VUK_L_MOVE: case VUK_L_MR32:
        case VUK_L_MFIR: case VUK_L_RGET: case VUK_L_LQI: case VUK_L_SQI:
            return true;
        default:
            return false;
    }
}

const std::array<VuCachedTier::LowerFn, VUK_L_COUNT * 16> VuCachedTier::kLowerTableD = [] {
    std::array<LowerFn, VUK_L_COUNT * 16> t{};
    auto fill_d = [&t](auto self, auto kc, auto dc) {
        constexpr uint8_t K = decltype(kc)::value;
        constexpr int D = decltype(dc)::value;
        t[K * 16 + D] = &VuCachedTier::ExecLowerK<K, D>;
        if constexpr (D + 1 < 16) self(self, kc, std::integral_constant<int, D + 1>{});
    };
    auto fill_k = [&](auto self, auto kc) {
        constexpr uint8_t K = decltype(kc)::value;
        if (LowerKindReadsDest(K)) {
            fill_d(fill_d, kc, std::integral_constant<int, 0>{});
        } else {
            for (int d = 0; d < 16; ++d) t[K * 16 + d] = &VuCachedTier::ExecLowerK<K>;
        }
        if constexpr (K + 1 < VUK_L_COUNT) self(self, std::integral_constant<uint8_t, K + 1>{});
    };
    fill_k(fill_k, std::integral_constant<uint8_t, 0>{});
    return t;
}();

VuCachedTier::UpperFn VuCachedTier::UpperFastFn(uint8_t upper_kind) {
    return kUpperTable[upper_kind < VUK_U_COUNT ? upper_kind : VUK_U_GENERIC];
}

VuCachedTier::LowerFn VuCachedTier::LowerFastFn(uint8_t lower_kind) {
    return kLowerTable[lower_kind < VUK_L_COUNT ? lower_kind : VUK_L_GENERIC];
}

VuCachedTier::UpperFn VuCachedTier::UpperFastFnD(uint8_t upper_kind, uint8_t dest) {
    const uint8_t k = upper_kind < VUK_U_COUNT ? upper_kind : VUK_U_GENERIC;
    return kUpperTableD[k * 16 + (dest & 0xFu)];
}

VuCachedTier::LowerFn VuCachedTier::LowerFastFnD(uint8_t lower_kind, uint8_t dest) {
    const uint8_t k = lower_kind < VUK_L_COUNT ? lower_kind : VUK_L_GENERIC;
    return kLowerTableD[k * 16 + (dest & 0xFu)];
}

void VuCachedTier::ExecUpperFast(VuUnit& u, const VuDecodedSlot& s) { kUpperTable[s.upper_kind](generic_, u, s); }

void VuCachedTier::ExecLowerFast(VuUnit& u, const VuDecodedSlot& s, uint32_t pc, bool& branch_taken,
                                 uint32_t& branch_target) {
    kLowerTable[s.lower_kind](generic_, u, s, pc, branch_taken, branch_target);
}
