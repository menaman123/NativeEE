#pragma once
//
// rv2_decode.h — RECOMP v2 discovery: minimal R5900 control-flow decoder.
//
// [RECOMP-P2] Hand-rolled bit decoder for exactly the instruction classes the
// discovery engine needs (control transfers, prologue shapes, and the handful
// of ALU/load/store forms the jump-table engines track). Everything else is
// Kind::Other. Deliberately dependency-free (the engine library links no
// rabbitizer); the test suite independently cross-derives branch targets with
// rabbitizer, so a classification slip here shows up as a red fixture, not a
// silent hole.
//
// GAME-AGNOSTIC RULE (design §6): no game addresses in this file, ever.
//

#include <cstdint>

namespace recomp_v2 {
namespace decode {

enum class Kind : uint8_t {
    Other = 0,      // anything the walker just steps over
    CondBranch,     // beq/bne/blez/bgtz + REGIMM cond + bc0/bc1/bc2 (target + fallthrough)
    CondBranchLikely, // *l forms (slot taken-only; same leader/edge shape)
    UncondBranch,   // beq rs==rt ("b"), bgez $zero, beql rs==rt (target only)
    J,              // j target (no return)
    Jal,            // jal target (callee + return site pc+8)
    Jr,             // jr rs, rs != ra (indirect: table site or computed jump)
    JrRa,           // jr ra (return)
    Jalr,           // jalr rd, rs (indirect call + return site pc+8)
    Eret,           // cop0 eret (stop)
};

struct Insn {
    Kind kind = Kind::Other;
    uint8_t op = 0, rs = 0, rt = 0, rd = 0, sa = 0, funct = 0;
    int16_t simm = 0;
    uint32_t word = 0;

    bool is_branch_like() const {
        return kind == Kind::CondBranch || kind == Kind::CondBranchLikely ||
               kind == Kind::UncondBranch;
    }
    // Branch target (valid for branch kinds), given the instruction VA.
    uint32_t branch_target(uint32_t pc) const {
        return pc + 4 + (static_cast<int32_t>(simm) << 2);
    }
    // Jump target (valid for J/Jal).
    uint32_t jump_target(uint32_t pc) const {
        return (pc & 0xF0000000u) | ((word & 0x03FFFFFFu) << 2);
    }
};

inline Insn decode(uint32_t word) {
    Insn in;
    in.word = word;
    in.op = static_cast<uint8_t>(word >> 26);
    in.rs = static_cast<uint8_t>((word >> 21) & 31);
    in.rt = static_cast<uint8_t>((word >> 16) & 31);
    in.rd = static_cast<uint8_t>((word >> 11) & 31);
    in.sa = static_cast<uint8_t>((word >> 6) & 31);
    in.funct = static_cast<uint8_t>(word & 63);
    in.simm = static_cast<int16_t>(word & 0xFFFF);

    switch (in.op) {
        case 0: // SPECIAL
            if (in.funct == 8) in.kind = (in.rs == 31) ? Kind::JrRa : Kind::Jr;
            else if (in.funct == 9) in.kind = Kind::Jalr;
            break;
        case 1: // REGIMM: rt 0..3 = bltz/bgez/bltzl/bgezl, 16..19 = *al forms.
                // rt 24/25 are R5900 mtsab/mtsah — NOT branches.
            if (in.rt <= 3 || (in.rt >= 16 && in.rt <= 19)) {
                const bool likely = (in.rt & 2) != 0;
                // bgez $zero (rt==1 or 17) is unconditionally taken.
                if ((in.rt == 1 || in.rt == 17) && in.rs == 0)
                    in.kind = Kind::UncondBranch;
                else
                    in.kind = likely ? Kind::CondBranchLikely : Kind::CondBranch;
            }
            break;
        case 2: in.kind = Kind::J; break;
        case 3: in.kind = Kind::Jal; break;
        case 4:  // beq (rs==rt => unconditional "b")
            in.kind = (in.rs == in.rt) ? Kind::UncondBranch : Kind::CondBranch;
            break;
        case 5: in.kind = Kind::CondBranch; break;         // bne
        case 6: case 7: in.kind = Kind::CondBranch; break; // blez/bgtz
        case 16: // COP0: bc0 branches; eret
            if (in.rs == 8 && in.rt <= 3)
                in.kind = (in.rt & 2) ? Kind::CondBranchLikely : Kind::CondBranch;
            else if (((word >> 25) & 1) && in.funct == 0x18)
                in.kind = Kind::Eret; // CO=1, funct=eret
            break;
        case 17: case 18: // COP1/COP2: bc1*/bc2*
            if (in.rs == 8 && in.rt <= 3)
                in.kind = (in.rt & 2) ? Kind::CondBranchLikely : Kind::CondBranch;
            break;
        case 20: // beql (rs==rt => unconditional, slot always runs)
            in.kind = (in.rs == in.rt) ? Kind::UncondBranch : Kind::CondBranchLikely;
            break;
        case 21: in.kind = Kind::CondBranchLikely; break;  // bnel
        case 22: case 23: in.kind = Kind::CondBranchLikely; break; // blezl/bgtzl
        default: break;
    }
    return in;
}

// Prologue shape (seed source 3, SEED-ONLY): addiu/daddiu sp, sp, -N.
inline bool is_prologue_word(uint32_t word) {
    const uint32_t hi = word >> 16;
    return (hi == 0x27BD || hi == 0x67BD) && (word & 0x8000u) != 0;
}

// Which GPR (if any) does this instruction define? -1 = none. Used by both
// jump-table engines so their view of register kills is identical (the
// cross-check depends on symmetric blindness).
inline int gpr_def(const Insn& in) {
    switch (in.op) {
        case 0:  // SPECIAL: R-type writes rd (jr/jalr/syscall are Kind != Other
                 // at call sites; mult/div/mt* have rd == 0 and fall out below)
            return in.rd != 0 ? in.rd : -1;
        case 1:  // REGIMM: branches/traps/mtsab/mtsah — no GPR write
            return -1;
        case 9: case 10: case 11: case 12: case 13: case 14: case 15:
        case 24: case 25: // addiu/slti/sltiu/andi/ori/xori/lui/daddi/daddiu
            return in.rt != 0 ? in.rt : -1;
        case 26: case 27: case 30: // ldl/ldr/lq
        case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39:
        case 55: // lb/lh/lwl/lw/lbu/lhu/lwr/lwu/ld
            return in.rt != 0 ? in.rt : -1;
        case 28: // MMI: most write rd (plzcw, p*); conservative
            return in.rd != 0 ? in.rd : -1;
        case 16: case 17: case 18: // COP0/1/2: mfc/cfc/dmfc write rt
            if (in.rs == 0 || in.rs == 1 || in.rs == 2)
                return in.rt != 0 ? in.rt : -1;
            return -1;
        default: // stores, cache, pref, lwc1/swc1, lqc2/sqc2, sb..sdr, sd...
            return -1;
    }
}

} // namespace decode
} // namespace recomp_v2
