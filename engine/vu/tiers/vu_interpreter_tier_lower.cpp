// VuInterpreterTier -- the lower (LSU / IALU / flag / branch / FDIV / EFU /
// random / XGKICK) word.
//
// PS2 rules: VI arithmetic is 16-bit; data addresses are quadword indices
// (VI[base] + imm11) * 16 masked to the data memory; loads and stores move raw
// bits; the flag-read instructions observe the pipelines as of this cycle
// (FS*/FM* retire the MAC ring first, FC* the CLIP ring); branch targets are
// relative to the NEXT bundle and BAL/JALR link the 11-bit index of the bundle
// after the delay slot; DIV/SQRT/RSQRT issue into the Q latch with the D/I
// status of the operation, the EFU ops into the P latch with their latency;
// R is a 23-bit LFSR presented with the 1.0f exponent.
#include "engine/vu/tiers/vu_interpreter_tier.h"

#include <cfloat>
#include <cmath>
#include <cstring>

#include "engine/vu/vu_decoder.h"
#include "engine/vu/vu_float.h"
#include "engine/vu/vu_unit.h"

namespace {

inline uint32_t Le32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
inline void StoreLe32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, sizeof(v)); }

inline int32_t SignExtend11(uint32_t l) {
    const uint32_t v = l & 0x7FFu;
    return static_cast<int32_t>(static_cast<int16_t>((v & 0x400u) ? (v | 0xF800u) : v));
}
inline int32_t SignExtend5(uint32_t v) {
    v &= 0x1Fu;
    return static_cast<int32_t>(static_cast<int8_t>((v & 0x10u) ? (v | 0xE0u) : v));
}

// Quadword byte address of (vi + imm) with the unit's data mask.
inline uint32_t QwAddr(uint16_t vi, int32_t imm, uint32_t mask) {
    return static_cast<uint32_t>((static_cast<int32_t>(vi) + imm) * 16) & mask;
}

// ILW/ILWR word offset: the first selected lane in X,Y,Z priority, else W.
inline uint32_t IlwOffset(uint32_t dest) {
    if (dest & vu_dest::X) return 0;
    if (dest & vu_dest::Y) return 4;
    if (dest & vu_dest::Z) return 8;
    return 12;
}

inline uint32_t LfsrStep(uint32_t r) {
    r &= 0x7FFFFFu;
    const uint32_t nb = ((r >> 22) ^ (r >> 17)) & 1u;
    return (((r << 1) | nb) & 0x7FFFFFu) | 0x3F800000u;
}

}  // namespace

void VuInterpreterTier::RunLower(VuUnit& u, uint32_t l, uint32_t pc, bool& branch_taken, uint32_t& branch_target) {
    VUregs& regs = u.regs();
    FlagPipeline& pipes = u.flags();
    uint8_t* data = u.state().data;
    const uint32_t mask = u.mem_mask();
    const uint32_t opcode = (l >> 25) & 0x7Fu;
    const uint32_t ft = (l >> 16) & 0x1Fu;
    const uint32_t fs = (l >> 11) & 0x1Fu;
    const uint32_t dest = (l >> 21) & 0xFu;
    const uint32_t vit = ft & 0xFu;
    const uint32_t vis = fs & 0xFu;
    const int32_t imm11 = SignExtend11(l);
    const uint32_t imm15 = (((l >> 10) & 0x7800u) | (l & 0x7FFu)) & 0x7FFFu;
    const uint32_t imm24 = l & 0xFFFFFFu;
    const uint32_t imm12 = ((l >> 10) & 0x800u) | (l & 0x7FFu);
    branch_taken = false;

    auto rel_target = [&]() { return static_cast<uint32_t>(static_cast<int32_t>(pc + 8) + imm11 * 8) & kPcMask; };
    auto link = [&]() { return static_cast<uint16_t>(((pc + 16) / 8) & kLinkMask); };
    auto cond_branch = [&](bool cond) {
        if (cond) {
            branch_taken = true;
            branch_target = rel_target();
        }
    };
    auto set_vi = [&](uint32_t idx, uint32_t v) {
        if (idx != 0) regs.VI[idx] = static_cast<uint16_t>(v);
    };

    switch (opcode) {
        case 0x00: {  // LQ
            const uint32_t addr = QwAddr(regs.VI[vis], imm11, mask);
            if (ft != 0) {
                for (int k = 0; k < 4; ++k) {
                    if (dest & (8u >> k)) regs.VF[ft].I[k] = Le32(data + addr + 4 * k);
                }
            }
            break;
        }
        case 0x01: {  // SQ: read-modify-write of the quadword
            const uint32_t addr = QwAddr(regs.VI[vit], imm11, mask);
            for (int k = 0; k < 4; ++k) {
                if (dest & (8u >> k)) StoreLe32(data + addr + 4 * k, regs.VF[fs].I[k]);
            }
            break;
        }
        case 0x04: {  // ILW
            const uint32_t addr = QwAddr(regs.VI[vis], imm11, mask);
            set_vi(vit, Le32(data + addr + IlwOffset(dest)));
            break;
        }
        case 0x05: {  // ISW: the VI word into every selected lane
            const uint32_t addr = QwAddr(regs.VI[vis], imm11, mask);
            const uint32_t word = regs.VI[vit];
            for (int k = 0; k < 4; ++k) {
                if (dest & (8u >> k)) StoreLe32(data + addr + 4 * k, word);
            }
            break;
        }
        case 0x08: case 0x23: set_vi(vit, regs.VI[vis] + imm15); break;  // IADDIU (+ alt encoding)
        case 0x09: case 0x27: set_vi(vit, regs.VI[vis] - imm15); break;  // ISUBIU (+ alt encoding)
        case 0x10: regs.VI[1] = (pipes.VisibleClip(regs) == imm24) ? 1 : 0; break;               // FCEQ
        case 0x11: pipes.SetClipImmediate(regs, imm24); break;                                     // FCSET
        case 0x12: regs.VI[1] = ((pipes.VisibleClip(regs) & imm24) != 0) ? 1 : 0; break;          // FCAND
        case 0x13: regs.VI[1] = ((pipes.VisibleClip(regs) | imm24) == 0xFFFFFFu) ? 1 : 0; break;  // FCOR
        case 0x14:  // FSEQ
            pipes.RetireMacStatus(regs);
            set_vi(vit, ((regs.status_flag & 0xFFFu) == imm12) ? 1 : 0);
            break;
        case 0x15:  // FSSET: sticky bits replaced, the current six kept
            pipes.RetireMacStatus(regs);
            regs.status_flag = static_cast<uint16_t>((regs.status_flag & 0x3Fu) | (imm12 & 0xFC0u));
            break;
        case 0x16:  // FSAND
            pipes.RetireMacStatus(regs);
            set_vi(vit, (regs.status_flag & 0xFFFu) & imm12);
            break;
        case 0x17:  // FSOR
            pipes.RetireMacStatus(regs);
            set_vi(vit, (regs.status_flag & 0xFFFu) | imm12);
            break;
        case 0x18:  // FMEQ
            pipes.RetireMacStatus(regs);
            set_vi(vit, (regs.mac_flag == regs.VI[vis]) ? 1 : 0);
            break;
        case 0x1A:  // FMAND
            pipes.RetireMacStatus(regs);
            set_vi(vit, regs.mac_flag & regs.VI[vis]);
            break;
        case 0x1B:  // FMOR
            pipes.RetireMacStatus(regs);
            set_vi(vit, regs.mac_flag | regs.VI[vis]);
            break;
        case 0x1C: set_vi(vit, pipes.VisibleClip(regs) & 0xFFFu); break;  // FCGET (no MAC retire)
        case 0x20: cond_branch(true); break;                               // B
        case 0x21:                                                         // BAL
            set_vi(vit, link());
            cond_branch(true);
            break;
        case 0x22: set_vi(vit, regs.VI[vis] + SignExtend5((l >> 6) & 0x1Fu)); break;  // IADDI (primary encoding)
        case 0x24:  // JR
            branch_taken = true;
            branch_target = (static_cast<uint32_t>(regs.VI[vis]) * 8u) & kPcMask;
            break;
        case 0x25:  // JALR: the target is read before the link is written
            branch_target = (static_cast<uint32_t>(regs.VI[vis]) * 8u) & kPcMask;
            set_vi(vit, link());
            branch_taken = true;
            break;
        case 0x28: cond_branch(regs.VI[vis] == regs.VI[vit]); break;                    // IBEQ
        case 0x29: cond_branch(regs.VI[vis] != regs.VI[vit]); break;                    // IBNE
        case 0x2C: cond_branch(static_cast<int16_t>(regs.VI[vis]) < 0); break;          // IBLTZ
        case 0x2D: cond_branch(static_cast<int16_t>(regs.VI[vis]) > 0); break;          // IBGTZ
        case 0x2E: cond_branch(static_cast<int16_t>(regs.VI[vis]) <= 0); break;         // IBLEZ
        case 0x2F: cond_branch(static_cast<int16_t>(regs.VI[vis]) >= 0); break;         // IBGEZ
        case 0x40: RunLowerSpecial(u, l); break;
        default:
            break;
    }
}

void VuInterpreterTier::RunLowerSpecial(VuUnit& u, uint32_t l) {
    VUregs& regs = u.regs();
    FlagPipeline& pipes = u.flags();
    Vu1State& st = u.state();
    uint8_t* data = st.data;
    const uint32_t mask = u.mem_mask();
    const uint32_t fn = l & 0x3Fu;
    const uint32_t ft = (l >> 16) & 0x1Fu;
    const uint32_t fs = (l >> 11) & 0x1Fu;
    const uint32_t fd = (l >> 6) & 0x1Fu;
    const uint32_t dest = (l >> 21) & 0xFu;
    const uint32_t vit = ft & 0xFu;
    const uint32_t vis = fs & 0xFu;
    const uint32_t vid = fd & 0xFu;
    const uint32_t fsf = (l >> 21) & 3u;
    const uint32_t ftf = (l >> 23) & 3u;

    auto set_vi = [&](uint32_t idx, uint32_t v) {
        if (idx != 0) regs.VI[idx] = static_cast<uint16_t>(v);
    };

    if (fn < 0x3C) {
        switch (fn) {
            case 0x30: set_vi(vid, regs.VI[vis] + regs.VI[vit]); break;  // IADD
            case 0x31: set_vi(vid, regs.VI[vis] - regs.VI[vit]); break;  // ISUB
            case 0x32:                                                   // IADDI
                set_vi(vit, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(regs.VI[vis])) +
                                                  SignExtend5((l >> 6) & 0x1Fu)));
                break;
            case 0x34: set_vi(vid, regs.VI[vis] & regs.VI[vit]); break;  // IAND
            case 0x35: set_vi(vid, regs.VI[vis] | regs.VI[vit]); break;  // IOR
            default: break;
        }
        return;
    }

    const uint32_t t3 = fn - 0x3Cu;
    const uint32_t funct2 = (l >> 6) & 0x1Fu;

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
    auto load_qw = [&](uint32_t addr) {
        VR_reg v = {};
        std::memcpy(v.I, data + addr, 16);
        return v;
    };
    auto store_masked = [&](uint32_t addr, const VR_reg& v) {
        for (int k = 0; k < 4; ++k) {
            if (dest & (8u >> k)) StoreLe32(data + addr + 4 * k, v.I[k]);
        }
    };
    auto fsf_bits = [&]() { return regs.VF[fs].I[fsf]; };

    switch (funct2) {
        case 0x0C:
            if (t3 == 0) {  // MOVE
                write_masked_vec(ft, regs.VF[fs]);
            } else if (t3 == 1 && ft != 0) {  // MR32
                const VR_reg src = regs.VF[fs];
                VR_reg v = {};
                v.x = src.y; v.y = src.z; v.z = src.w; v.w = src.x;
                write_masked_vec(ft, v);
            }
            break;
        case 0x0D:
            switch (t3) {
                case 0: {  // LQI: load, then post-increment
                    const uint32_t addr = (static_cast<uint32_t>(regs.VI[vis]) * 16u) & mask;
                    write_masked_vec(ft, load_qw(addr));
                    set_vi(vis, regs.VI[vis] + 1u);
                    break;
                }
                case 1: {  // SQI
                    const uint32_t addr = (static_cast<uint32_t>(regs.VI[vit]) * 16u) & mask;
                    store_masked(addr, regs.VF[fs]);
                    set_vi(vit, regs.VI[vit] + 1u);
                    break;
                }
                case 2: {  // LQD: pre-decrement, then load
                    set_vi(vis, regs.VI[vis] - 1u);
                    const uint32_t addr = (static_cast<uint32_t>(regs.VI[vis]) * 16u) & mask;
                    write_masked_vec(ft, load_qw(addr));
                    break;
                }
                default: {  // SQD
                    set_vi(vit, regs.VI[vit] - 1u);
                    const uint32_t addr = (static_cast<uint32_t>(regs.VI[vit]) * 16u) & mask;
                    store_masked(addr, regs.VF[fs]);
                    break;
                }
            }
            break;
        case 0x0E:
            switch (t3) {
                case 0: DoDiv(u, fs, fsf, ft, ftf); break;
                case 1: DoSqrt(u, ft, ftf); break;
                case 2: DoRsqrt(u, fs, fsf, ft, ftf); break;
                default: pipes.FlushQPipe(regs); break;  // WAITQ
            }
            break;
        case 0x0F:
            switch (t3) {
                case 0: set_vi(vit, fsf_bits() & 0xFFFFu); break;  // MTIR
                case 1: {                                          // MFIR: sign-extended VI into the lanes
                    const uint32_t iv = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(regs.VI[vis])));
                    write_masked_all(ft, iv);
                    break;
                }
                case 2: {  // ILWR
                    const uint32_t addr = (static_cast<uint32_t>(regs.VI[vis]) * 16u) & mask;
                    set_vi(vit, Le32(data + addr + IlwOffset(dest)));
                    break;
                }
                default: {  // ISWR
                    const uint32_t addr = (static_cast<uint32_t>(regs.VI[vit]) * 16u) & mask;
                    const uint32_t word = regs.VI[vis];
                    for (int k = 0; k < 4; ++k) {
                        if (dest & (8u >> k)) StoreLe32(data + addr + 4 * k, word);
                    }
                    break;
                }
            }
            break;
        case 0x10:
            switch (t3) {
                case 0:  // RNEXT: step the LFSR, then present R
                    regs.R = LfsrStep(regs.R);
                    write_masked_all(ft, regs.R);
                    break;
                case 1: write_masked_all(ft, regs.R); break;                                    // RGET (R as stored)
                case 2: regs.R = (fsf_bits() & 0x7FFFFFu) | 0x3F800000u; break;                  // RINIT
                default: regs.R = ((regs.R ^ fsf_bits()) & 0x7FFFFFu) | 0x3F800000u; break;      // RXOR
            }
            break;
        case 0x19:
            if (t3 == 0) write_masked_all(ft, VuFloat::F2Bits(pipes.VisibleP(regs)));  // MFP
            break;
        case 0x1A:
            if (t3 == 0) {  // XTOP (+ the VU1 yield rule)
                set_vi(vit, u.vif_top());
                if (u.id() == 1 && ++st.xtop_read_count > kXtopYieldReads) st.xtop_exit_requested = true;
            } else if (t3 == 1) {  // XITOP reads the ITOPS shadow
                set_vi(vit, u.vif_itop());
            }
            break;
        case 0x1B:
            if (t3 == 0) {  // XGKICK (VU1 only)
                if (XgKick* xgk = u.xgkick()) {
                    xgk->Arm(data, regs.VI[vis], pipes.cycle());
                    ++st.xgkick_count;
                    st.xgkick_has_fired = true;
                    st.xtop_read_count = 0;
                }
            }
            break;
        case 0x1C: case 0x1D: case 0x1E: case 0x1F:
            RunEfu(u, l, funct2, t3);
            break;
        default:
            break;
    }
}

void VuInterpreterTier::RunEfu(VuUnit& u, uint32_t l, uint32_t funct2, uint32_t t3) {
    VUregs& regs = u.regs();
    FlagPipeline& pipes = u.flags();
    const uint32_t fs = (l >> 11) & 0x1Fu;
    const uint32_t fsf = (l >> 21) & 3u;
    const VR_reg v = Clamp4(regs.VF[fs]);
    const float lane = VuFloat::ClampFloat(regs.VF[fs].F[fsf]);
    auto push = [&](float value, int cycles) { pipes.PushP(regs, value, cycles); };
    // Sum of squares as the fused chain the reference's compiler formed.
    auto sum_sq = [&]() { return std::fmaf(v.z, v.z, std::fmaf(v.x, v.x, v.y * v.y)); };

    switch (funct2) {
        case 0x1C:
            switch (t3) {
                case 0: push(sum_sq(), FlagPipeline::kEfuEsadd); break;  // ESADD
                case 1: {                                                // ERSADD
                    const float s = sum_sq();
                    push(s != 0.0f ? 1.0f / s : FLT_MAX, FlagPipeline::kEfuErsadd);
                    break;
                }
                case 2: push(std::sqrt(sum_sq()), FlagPipeline::kEfuEleng); break;  // ELENG
                default: {                                                          // ERLENG
                    const float len = std::sqrt(sum_sq());
                    push(len != 0.0f ? 1.0f / len : FLT_MAX, FlagPipeline::kEfuErleng);
                    break;
                }
            }
            break;
        case 0x1D:
            switch (t3) {
                case 0: push(v.x != 0.0f ? std::atan(v.y / v.x) : 0.0f, FlagPipeline::kEfuEatanxy); break;  // EATANxy
                case 1: push(v.x != 0.0f ? std::atan(v.z / v.x) : 0.0f, FlagPipeline::kEfuEatanxz); break;  // EATANxz
                case 2: push(v.x + v.y + v.z + v.w, FlagPipeline::kEfuEsum); break;                          // ESUM
                default: break;
            }
            break;
        case 0x1E:
            switch (t3) {
                case 0: push(std::sqrt(std::fabs(lane)), FlagPipeline::kEfuEsqrt); break;  // ESQRT
                case 1: {                                                                  // ERSQRT
                    const float s = std::sqrt(std::fabs(lane));
                    push(s != 0.0f ? 1.0f / s : FLT_MAX, FlagPipeline::kEfuErsqrt);
                    break;
                }
                case 2: push(lane != 0.0f ? 1.0f / lane : FLT_MAX, FlagPipeline::kEfuErcpr); break;  // ERCPR
                default: break;  // WAITP: the busy-wait already ran in the stall model
            }
            break;
        default:  // 0x1F
            switch (t3) {
                case 0: push(std::sin(lane), FlagPipeline::kEfuEsin); break;                       // ESIN
                case 1: push(std::atan(lane), FlagPipeline::kEfuEatan); break;                     // EATAN
                case 3: push(VuFloat::ClampFloat(std::exp(lane)), FlagPipeline::kEfuEexp); break;  // EEXP (the only clamped EFU result)
                default: break;
            }
            break;
    }
}
