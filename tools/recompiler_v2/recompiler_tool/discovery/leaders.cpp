//
// leaders.cpp — RECOMP v2 discovery: complete block-leader analysis.
//
// [RECOMP-P2] Implements RECOMP_P0_DESIGN.md §7.1 / CONVENTIONS D1: every
// branch/jump target is a leader; the fall-through successor of every
// conditional branch's delay slot is a leader; jal/jalr return sites are
// leaders. This is the linear-sweep half of the "linear sweep + recursive
// descent" pillar; discover_program() combines it with the reachability
// walker and jump-table recovery for the per-function leader sets.
//
// Concept provenance: multi-pass/fixed-point recovery per the angr CFGFast
// documentation (ideas only; BSD-2-Clause project docs).
//

#include "leaders.h"
#include "rv2_decode.h"

namespace recomp_v2 {

LeaderAnalysis analyze_leaders(const SectionImage& text,
                               const std::set<uint32_t>& function_entries) {
    LeaderAnalysis out;
    using decode::Kind;

    for (uint32_t e : function_entries)
        if (text.contains(e)) out.leaders.insert(e);

    const uint32_t end = text.base_va + static_cast<uint32_t>(text.bytes.size());
    for (uint32_t pc = text.base_va; pc + 4 <= end; pc += 4) {
        const decode::Insn in = decode::decode(text.read_u32(pc));
        switch (in.kind) {
            case Kind::CondBranch:
            case Kind::CondBranchLikely: {
                const uint32_t tgt = in.branch_target(pc);
                if (text.contains(tgt)) {
                    out.leaders.insert(tgt);
                    out.edges.emplace(pc, tgt);
                }
                const uint32_t ft = pc + 8; // successor AFTER the delay slot
                if (text.contains(ft)) {
                    out.leaders.insert(ft);
                    out.edges.emplace(pc, ft);
                }
                break;
            }
            case Kind::UncondBranch: {
                const uint32_t tgt = in.branch_target(pc);
                if (text.contains(tgt)) {
                    out.leaders.insert(tgt);
                    out.edges.emplace(pc, tgt);
                }
                break;
            }
            case Kind::J: {
                const uint32_t tgt = in.jump_target(pc);
                if (text.contains(tgt)) {
                    out.leaders.insert(tgt);
                    out.edges.emplace(pc, tgt);
                }
                break;
            }
            case Kind::Jal: {
                const uint32_t tgt = in.jump_target(pc);
                if (text.contains(tgt)) {
                    out.leaders.insert(tgt);
                    out.edges.emplace(pc, tgt);
                }
                if (text.contains(pc + 8)) out.leaders.insert(pc + 8); // return site
                break;
            }
            case Kind::Jalr:
                if (text.contains(pc + 8)) out.leaders.insert(pc + 8); // return site
                break;
            default:
                break;
        }
    }
    return out;
}

} // namespace recomp_v2
