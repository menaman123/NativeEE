// VuDecoder -- the one VU bundle decode (encoding rules in the header).
#include "engine/vu/vu_decoder.h"

namespace {

// VF00 reads never stall and VF00 writes never land: neither enters a set.
void AddRead(VuDecodedSlot& s, uint32_t reg, uint8_t mask) {
    if (reg == 0 || mask == 0) return;
    if (s.n_rd < 5) {
        s.rd_reg[s.n_rd] = static_cast<uint8_t>(reg);
        s.rd_mask[s.n_rd] = mask;
        s.n_rd++;
    }
    s.rd_regs |= 1u << reg;
}

void AddWrite(VuDecodedSlot& s, uint32_t reg, uint8_t mask) {
    if (reg == 0 || mask == 0) return;
    if (s.n_wr < 2) {
        s.wr_reg[s.n_wr] = static_cast<uint8_t>(reg);
        s.wr_mask[s.n_wr] = mask;
        s.n_wr++;
    }
}

inline uint32_t UFunc(uint32_t u) { return u & 0x3Fu; }
inline uint32_t UFd(uint32_t u) { return (u >> 6) & 0x1Fu; }
inline uint32_t UFs(uint32_t u) { return (u >> 11) & 0x1Fu; }
inline uint32_t UFt(uint32_t u) { return (u >> 16) & 0x1Fu; }
inline uint32_t UDest(uint32_t u) { return (u >> 21) & 0xFu; }

inline uint32_t LOpcode(uint32_t l) { return (l >> 25) & 0x7Fu; }
inline uint32_t LFt(uint32_t l) { return (l >> 16) & 0x1Fu; }
inline uint32_t LFs(uint32_t l) { return (l >> 11) & 0x1Fu; }
inline uint32_t LFd(uint32_t l) { return (l >> 6) & 0x1Fu; }
inline uint32_t LDest(uint32_t l) { return (l >> 21) & 0xFu; }
inline uint32_t LFn(uint32_t l) { return l & 0x3Fu; }
inline uint32_t LFunct2(uint32_t l) { return (l >> 6) & 0x1Fu; }
inline uint8_t LFsfLane(uint32_t l) { return static_cast<uint8_t>(1u << (3 - ((l >> 21) & 3u))); }
inline uint8_t LFtfLane(uint32_t l) { return static_cast<uint8_t>(1u << (3 - ((l >> 23) & 3u))); }

}  // namespace

void VuDecoder::DecodeStallStatics(VuDecodedSlot& s) {
    const uint32_t u = s.upper;
    const uint32_t l = s.lower;
    const bool has_lower = !s.i_bit;

    // IALU: VI writes with load latency, VI reads by branches.
    if (has_lower) {
        const uint32_t op = LOpcode(l);
        const uint32_t l_it = (l >> 16) & 0xFu;
        const uint32_t l_is = (l >> 11) & 0xFu;
        switch (op) {
            case 0x04: s.ialu_wr_vi = static_cast<int8_t>(l_it); break;                                   // ILW
            case 0x28: case 0x29:                                                                          // IBEQ / IBNE
                s.ialu_branch_rd = static_cast<uint16_t>((1u << l_is) | (1u << l_it));
                break;
            case 0x24: case 0x25: case 0x2C: case 0x2D: case 0x2E: case 0x2F:                              // JR/JALR/IBLTZ/IBGTZ/IBLEZ/IBGEZ
                s.ialu_branch_rd = static_cast<uint16_t>(1u << l_is);
                break;
            case 0x40: {
                const uint32_t fn = LFn(l);
                if (fn >= 0x3C && LFunct2(l) == 0x0F && (fn - 0x3C) == 2) s.ialu_wr_vi = static_cast<int8_t>(l_it);  // ILWR
                break;
            }
            default: break;
        }
    }

    // Upper (always present).
    {
        const uint32_t func = UFunc(u);
        const uint32_t fd = UFd(u), fs = UFs(u), ft = UFt(u);
        const uint8_t dest = static_cast<uint8_t>(UDest(u));
        const uint8_t bc_lane = static_cast<uint8_t>(0x8u >> (func & 3u));
        if (func <= 0x1B) {                       // ADD/SUB/MADD/MSUB/MAX/MINI/MUL broadcast forms
            AddRead(s, fs, dest);
            AddRead(s, ft, bc_lane);
            AddWrite(s, fd, dest);
        } else if (func <= 0x27) {                // q / i forms
            AddRead(s, fs, dest);
            AddWrite(s, fd, dest);
        } else if (func <= 0x2F) {                // vector forms; OPMSUB reads/writes xyz
            const uint8_t m = (func == 0x2E) ? 0x0E : dest;
            AddRead(s, fs, m);
            AddRead(s, ft, m);
            AddWrite(s, fd, m);
        } else if (func <= 0x3B) {                // unused encodings
            AddRead(s, fs, dest);
            AddRead(s, ft, bc_lane);
        } else {                                  // special2: op in the fd field
            switch (fd) {
                case 0x00: case 0x01: case 0x02: case 0x03: case 0x06:   // ADDA/SUBA/MADDA/MSUBA/MULA bc
                    AddRead(s, fs, dest);
                    AddRead(s, ft, bc_lane);
                    break;
                case 0x04: case 0x05:                                    // ITOFn / FTOIn
                    AddRead(s, fs, dest);
                    AddWrite(s, ft, dest);
                    break;
                case 0x07:
                    if (func == 0x3D) {                                  // ABS
                        AddRead(s, fs, dest);
                        AddWrite(s, ft, dest);
                    } else if (func == 0x3F) {                           // CLIP: fs.xyz against ft.w
                        AddRead(s, fs, 0x0E);
                        AddRead(s, ft, 0x01);
                    } else {                                             // MULAq / MULAi
                        AddRead(s, fs, dest);
                    }
                    break;
                case 0x08: case 0x09:                                    // ADDAq/MADDAq/ADDAi/MADDAi
                    AddRead(s, fs, dest);
                    break;
                case 0x0A:
                    if (func != 0x3F) {                                  // ADDA / MADDA / MULA
                        AddRead(s, fs, dest);
                        AddRead(s, ft, dest);
                    }
                    break;
                case 0x0B:
                    if (func == 0x3E) {                                  // OPMULA
                        AddRead(s, fs, 0x0E);
                        AddRead(s, ft, 0x0E);
                    } else if (func != 0x3F) {                           // SUBA / MSUBA (0x3F = NOP)
                        AddRead(s, fs, dest);
                        AddRead(s, ft, dest);
                    }
                    break;
                default: break;
            }
        }
    }

    // Lower.
    if (has_lower) {
        const uint32_t opcode = LOpcode(l);
        const uint32_t ft = LFt(l), fs = LFs(l);
        const uint8_t dest = static_cast<uint8_t>(LDest(l));
        const uint8_t fsf_lane = LFsfLane(l), ftf_lane = LFtfLane(l);
        if (opcode == 0x00) {                     // LQ
            AddWrite(s, ft, dest);
        } else if (opcode == 0x01) {              // SQ
            AddRead(s, fs, dest);
        } else if (opcode == 0x40) {
            const uint32_t fn = LFn(l);
            if (fn >= 0x3C) {
                const uint32_t t3 = fn - 0x3C;
                switch (LFunct2(l)) {
                    case 0x0C:
                        if (t3 == 0) {                                   // MOVE (ft == 0 is a NOP)
                            if (ft != 0) {
                                AddRead(s, fs, dest);
                                AddWrite(s, ft, dest);
                            }
                        } else if (t3 == 1) {                            // MR32 reads the rotated lanes
                            AddRead(s, fs, static_cast<uint8_t>(((dest >> 1) | (dest << 3)) & 0xFu));
                            AddWrite(s, ft, dest);
                        }
                        break;
                    case 0x0D:                                           // LQI/LQD write, SQI/SQD read
                        if (t3 == 0 || t3 == 2) AddWrite(s, ft, dest);
                        else AddRead(s, fs, dest);
                        break;
                    case 0x0E:                                           // DIV/SQRT/RSQRT/WAITQ
                        if (t3 == 0 || t3 == 2) {
                            AddRead(s, fs, fsf_lane);
                            AddRead(s, ft, ftf_lane);
                        } else if (t3 == 1) {
                            AddRead(s, ft, ftf_lane);
                        }
                        s.fdiv_pipe_op = true;
                        break;
                    case 0x0F:                                           // MTIR reads, MFIR writes
                        if (t3 == 0) AddRead(s, fs, fsf_lane);
                        else if (t3 == 1) AddWrite(s, ft, dest);
                        break;
                    case 0x10:                                           // RNEXT/RGET write, RINIT/RXOR read
                        if (t3 <= 1) AddWrite(s, ft, dest);
                        else AddRead(s, fs, fsf_lane);
                        break;
                    case 0x19:                                           // MFP
                        if (t3 == 0) AddWrite(s, ft, dest);
                        break;
                    case 0x1C:                                           // ESADD/ERSADD/ELENG/ERLENG
                        AddRead(s, fs, dest);
                        s.efu_pipe_op = true;
                        break;
                    case 0x1D:                                           // EATANxy/EATANxz/ESUM
                        if (t3 <= 2) {
                            AddRead(s, fs, dest);
                            s.efu_pipe_op = true;
                        }
                        break;
                    case 0x1E:                                           // ESQRT/ERSQRT/ERCPR/WAITP
                        if (t3 <= 2) AddRead(s, fs, fsf_lane);
                        s.efu_pipe_op = true;
                        break;
                    case 0x1F:                                           // ESIN/EATAN/EEXP
                        if (t3 == 0 || t3 == 1 || t3 == 3) {
                            AddRead(s, fs, fsf_lane);
                            s.efu_pipe_op = true;
                        }
                        break;
                    default: break;
                }
            }
        }
    }
}

void VuDecoder::ClassifyFastKinds(VuDecodedSlot& s) {
    const uint32_t u = s.upper;
    const uint32_t func = UFunc(u);
    s.u_fd = static_cast<uint8_t>(UFd(u));
    s.u_fs = static_cast<uint8_t>(UFs(u));
    s.u_ft = static_cast<uint8_t>(UFt(u));
    s.u_dest = static_cast<uint8_t>(UDest(u));
    s.u_bc = static_cast<uint8_t>(func & 3u);

    uint8_t uk = VUK_U_GENERIC;
    if (func >= 0x3C) {
        const uint32_t bc2 = func & 3u;
        switch (s.u_fd) {
            case 0x02: uk = VUK_U_MADDA_BC; break;
            case 0x03: uk = VUK_U_MSUBA_BC; break;
            case 0x04: if (bc2 == 0) uk = VUK_U_ITOF0; break;
            case 0x05: if (bc2 == 0) uk = VUK_U_FTOI0; else if (bc2 == 1) uk = VUK_U_FTOI4; break;
            case 0x06: uk = VUK_U_MULA_BC; break;
            case 0x07: if (bc2 == 3) uk = VUK_U_CLIP; break;
            case 0x0A: if (bc2 == 1) uk = VUK_U_MADDA; else if (bc2 == 2) uk = VUK_U_MULA; break;
            case 0x0B: if (bc2 == 3) uk = VUK_U_NOP; break;
            default: break;
        }
    } else if (func <= 0x03) uk = VUK_U_ADD_BC;
    else if (func <= 0x07) uk = VUK_U_SUB_BC;
    else if (func <= 0x0B) uk = VUK_U_MADD_BC;
    else if (func <= 0x0F) uk = VUK_U_MSUB_BC;
    else if (func <= 0x13) uk = VUK_U_MAX_BC;
    else if (func <= 0x17) uk = VUK_U_MINI_BC;
    else if (func <= 0x1B) uk = VUK_U_MUL_BC;
    else {
        switch (func) {
            case 0x1C: uk = VUK_U_MULQ; break;
            case 0x1E: uk = VUK_U_MUL_I; break;
            case 0x1F: uk = VUK_U_MINI_I; break;
            case 0x22: uk = VUK_U_ADD_I; break;
            case 0x28: uk = VUK_U_ADD; break;
            case 0x29: uk = VUK_U_MADD; break;
            case 0x2A: uk = VUK_U_MUL; break;
            case 0x2B: uk = VUK_U_MAX; break;
            case 0x2C: uk = VUK_U_SUB; break;
            case 0x2D: uk = VUK_U_MSUB; break;
            default: break;
        }
    }
    s.upper_kind = uk;

    if (s.i_bit) return;

    const uint32_t l = s.lower;
    s.l_ft = static_cast<uint8_t>(LFt(l));
    s.l_fs = static_cast<uint8_t>(LFs(l));
    s.l_fd = static_cast<uint8_t>(LFd(l));
    s.l_dest = static_cast<uint8_t>(LDest(l));
    s.l_fsf = static_cast<uint8_t>((l >> 21) & 3u);
    s.l_imm11 = static_cast<int16_t>((l & 0x7FFu) | ((l & 0x400u) ? 0xF800u : 0u));
    s.l_imm15u = static_cast<uint16_t>((((l >> 10) & 0x7800u) | (l & 0x7FFu)) & 0x7FFFu);

    uint8_t lk = VUK_L_GENERIC;
    switch (LOpcode(l)) {
        case 0x00: lk = VUK_L_LQ; break;
        case 0x01: lk = VUK_L_SQ; break;
        case 0x04: lk = VUK_L_ILW; break;
        case 0x05: lk = VUK_L_ISW; break;
        case 0x08: lk = VUK_L_IADDIU; break;
        case 0x09: lk = VUK_L_ISUBIU; break;
        case 0x1C: lk = VUK_L_FCGET; break;
        case 0x20: lk = VUK_L_B; break;
        case 0x21: lk = VUK_L_BAL; break;
        case 0x24: lk = VUK_L_JR; break;
        case 0x25: lk = VUK_L_JALR; break;
        case 0x28: lk = VUK_L_IBEQ; break;
        case 0x29: lk = VUK_L_IBNE; break;
        case 0x40: {
            const uint32_t fn = LFn(l);
            const uint32_t f2 = LFunct2(l);
            switch (fn) {
                case 0x30: lk = VUK_L_IADD; break;
                case 0x31: lk = VUK_L_ISUB; break;
                case 0x32: lk = VUK_L_IADDI; break;
                case 0x34: lk = VUK_L_IAND; break;
                case 0x35: lk = VUK_L_IOR; break;
                case 0x3C:
                    if (f2 == 0x0C) lk = (s.l_ft == 0) ? VUK_L_NOP : VUK_L_MOVE;
                    else if (f2 == 0x0D) lk = VUK_L_LQI;
                    else if (f2 == 0x0E) lk = VUK_L_DIV;
                    else if (f2 == 0x0F) lk = VUK_L_MTIR;
                    break;
                case 0x3D:
                    if (f2 == 0x0C) lk = (s.l_ft == 0) ? VUK_L_NOP : VUK_L_MR32;
                    else if (f2 == 0x0D) lk = VUK_L_SQI;
                    else if (f2 == 0x0F) lk = VUK_L_MFIR;
                    else if (f2 == 0x10) lk = VUK_L_RGET;
                    break;
                case 0x3E:
                    if (f2 == 0x10) lk = VUK_L_RINIT;
                    break;
                case 0x3F:
                    if (f2 == 0x0E) lk = VUK_L_WAITQ;
                    break;
                default: break;
            }
            break;
        }
        default: break;
    }
    s.lower_kind = lk;
}

VuDecodedSlot VuDecoder::DecodeVuSlot(uint32_t lower, uint32_t upper) {
    VuDecodedSlot s;
    s.upper = upper;
    s.lower = lower;
    s.i_bit = ((upper >> 31) & 1u) != 0;
    s.e_bit = ((upper >> 30) & 1u) != 0;
    s.is_nop_pair = (upper == 0 && lower == 0);
    ClassifyFastKinds(s);
    DecodeStallStatics(s);
    if (s.i_bit) return s;

    // The VF the upper writes (special2 ITOF/FTOI/ABS write ft; ABS only for func 0x3D).
    const uint32_t func = UFunc(upper);
    const uint32_t fd = UFd(upper), ft = UFt(upper);
    int upper_vf_write = -1;
    if (func <= 0x2F) upper_vf_write = static_cast<int>(fd);
    else if (func >= 0x3C && (fd == 0x04 || fd == 0x05 || (fd == 0x07 && func == 0x3D))) upper_vf_write = static_cast<int>(ft);
    if (upper_vf_write == 0) upper_vf_write = -1;

    // The VF the lower writes / reads.
    int lower_vf_write = -1, lower_vf_read0 = -1, lower_vf_read1 = -1;
    const uint32_t opcode = LOpcode(lower);
    const int lft = static_cast<int>(LFt(lower)), lfs = static_cast<int>(LFs(lower));
    if (opcode == 0x00) {
        lower_vf_write = lft;
    } else if (opcode == 0x01) {
        lower_vf_read0 = lfs;
    } else if (opcode == 0x40) {
        const uint32_t fn = LFn(lower);
        if (fn >= 0x3C) {
            const uint32_t t3 = fn - 0x3C;
            switch (LFunct2(lower)) {
                case 0x0C:
                    if (t3 <= 1) { lower_vf_read0 = lfs; lower_vf_write = lft; }
                    break;
                case 0x0D:
                    if (t3 == 0 || t3 == 2) lower_vf_write = lft;
                    else lower_vf_read0 = lfs;
                    break;
                case 0x0E:
                    if (t3 == 0 || t3 == 2) { lower_vf_read0 = lfs; lower_vf_read1 = lft; }
                    else if (t3 == 1) lower_vf_read0 = lft;
                    break;
                case 0x0F:
                    if (t3 == 0) lower_vf_read0 = lfs;
                    else if (t3 == 1) lower_vf_write = lft;
                    break;
                case 0x10:
                    if (t3 <= 1) lower_vf_write = lft;
                    else lower_vf_read0 = lfs;
                    break;
                case 0x19:
                    if (t3 == 0) lower_vf_write = lft;
                    break;
                case 0x1C: case 0x1D: case 0x1E: case 0x1F:
                    lower_vf_read0 = lfs;
                    break;
                default: break;
            }
        }
    }
    if (lower_vf_write == 0) lower_vf_write = -1;
    if (lower_vf_read0 == 0) lower_vf_read0 = -1;
    if (lower_vf_read1 == 0) lower_vf_read1 = -1;

    s.upper_vf_write = static_cast<int8_t>(upper_vf_write);
    s.discard_lower = (upper_vf_write >= 0 && upper_vf_write == lower_vf_write);
    s.hide_upper = (upper_vf_write >= 0 && (upper_vf_write == lower_vf_read0 || upper_vf_write == lower_vf_read1));
    return s;
}

bool VuDecoder::VuUpperCanTouchHardwired(uint32_t u) {
    const uint32_t func = UFunc(u);
    if (func <= 0x2F) return UFd(u) == 0;
    if (func < 0x3C) return true;
    const uint32_t fd = UFd(u), ft = UFt(u);
    switch (fd) {
        case 0x00: case 0x01: case 0x02: case 0x03: case 0x06: case 0x08: case 0x09: case 0x0A: case 0x0B:
            return false;
        case 0x04: case 0x05:
            return ft == 0;
        case 0x07:
            return (func & 3u) == 1 ? (ft == 0) : false;   // ABS (0x3D) writes ft; MULAq/MULAi write ACC, CLIP the clip flags
        default:
            return true;
    }
}

bool VuDecoder::VuLowerCanTouchHardwired(uint32_t l) {
    const uint32_t opcode = LOpcode(l);
    const uint32_t it = LFt(l), is = LFs(l), fd = LFd(l);
    switch (opcode) {
        case 0x00: return it == 0;
        case 0x01: return false;
        case 0x04: return it == 0;
        case 0x05: return false;
        case 0x08: case 0x09: return it == 0;
        case 0x10: case 0x12: case 0x13: return false;
        case 0x11: case 0x15: return false;
        case 0x14: case 0x16: case 0x17: case 0x18: case 0x1A: case 0x1B: return it == 0;
        case 0x1C: return it == 0;
        case 0x20: case 0x24: return false;
        case 0x21: case 0x25: return it == 0;
        case 0x22: case 0x23: return it == 0;
        case 0x28: case 0x29: case 0x2C: case 0x2D: case 0x2E: case 0x2F: return false;
        case 0x40: {
            const uint32_t fn = LFn(l);
            switch (fn) {
                case 0x30: case 0x31: case 0x34: case 0x35: return fd == 0;
                case 0x32: return it == 0;
                default: break;
            }
            if (fn < 0x3C) return true;
            const uint32_t t3 = fn - 0x3C;
            switch (LFunct2(l)) {
                case 0x0C: return (t3 <= 1) ? (it == 0) : true;
                case 0x0D: return (t3 == 0 || t3 == 2) ? (it == 0 || is == 0) : (it == 0);
                case 0x0E: return false;
                case 0x0F: return (t3 == 3) ? false : (it == 0);
                case 0x10: return (t3 <= 1) ? (it == 0) : false;
                case 0x19: return (t3 == 0) ? (it == 0) : true;
                case 0x1A: return (t3 <= 1) ? (it == 0) : true;
                case 0x1B: return (t3 == 0) ? false : true;
                case 0x1C: case 0x1D: case 0x1E: case 0x1F: return false;
                default: return true;
            }
        }
        default: return true;
    }
}

bool VuDecoder::VuSlotCanTouchHardwired(const VuDecodedSlot& s) {
    if (VuUpperCanTouchHardwired(s.upper)) return true;
    if (s.i_bit) return false;
    return VuLowerCanTouchHardwired(s.lower);
}

bool VuDecoder::IsBranchKind(uint8_t k) {
    return k == VUK_L_IBEQ || k == VUK_L_IBNE || k == VUK_L_B || k == VUK_L_BAL || k == VUK_L_JR || k == VUK_L_JALR;
}
