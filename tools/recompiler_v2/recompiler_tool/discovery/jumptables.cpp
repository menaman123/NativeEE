//
// jumptables.cpp — RECOMP v2 discovery: jump-table pattern library.
//
// [RECOMP-P2] Implements RECOMP_P0_DESIGN.md §7.2: TWO independent recovery
// engines cross-checked per jr site; disagreement is a hard error upstream
// (discover_program throws with the dossier). Table targets are read from the
// ELF data image, never inferred from block metadata (CONVENTIONS D3).
//
// LICENSING / ATTRIBUTION (CONVENTIONS P3):
//   * Engine 1 (forward register-state dataflow, including stack spill/reload
//     tracking) is adapted, at the algorithm level, from N64Recomp:
//       Copyright (c) 2024 N64Recomp contributors, MIT License,
//       https://github.com/N64Recomp/N64Recomp (src/analysis.cpp).
//     The MIT notice above is retained per its license terms.
//   * Engine 2 (backward slice + guard value-range + per-index evaluation +
//     sanity check) is
//       Adapted from Ghidra (Apache-2.0), National Security Agency;
//       https://github.com/NationalSecurityAgency/ghidra
//       (Ghidra/Features/Decompiler/src/decompile/cpp/jumptable.cc —
//       the JumpBasic model: backward PathMeld slice to the switch variable,
//       GuardRecord bound recovery from the dominating comparison,
//       EmulateFunction per-index address evaluation, and sanityCheck).
//     This comment is the Apache-2.0 attribution for the adaptation site.
//   * No PCSX2 (GPL) code or text is present here — concepts only, per the
//     master brief.
//

#include "jumptables.h"
#include "rv2_decode.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <optional>
#include <vector>

namespace recomp_v2 {

namespace {

using decode::Insn;
using decode::Kind;

constexpr uint8_t kSp = 29;
constexpr uint8_t kZero = 0;

bool is_move(const Insn& in) {
    // addu/daddu/or rd, rs, $zero (canonical MIPS register move)
    return in.op == 0 && (in.funct == 33 || in.funct == 45 || in.funct == 37) &&
           in.rt == kZero && in.rd != kZero;
}

// ---------------------------------------------------------------------------
// ENGINE 1 — forward register-state dataflow (N64Recomp-adapted, MIT; see the
// file header notice). Linear walk of [func_entry, site_pc) maintaining a
// 32-entry register state table plus sp-relative stack-slot states so spilled
// table bases survive (the SW-08 fixture shape).
// ---------------------------------------------------------------------------
struct E1RegState {
    bool has_base = false;   // register holds a lui(+addiu) built constant
    bool indexed = false;    // register holds base+index (post addu/daddu)
    bool loaded = false;     // register holds a value lw'd from table_va
    uint32_t base = 0;       // the constant (hi<<16 [+ simm]) if has_base
    uint32_t table_va = 0;   // load address if loaded
    void clear() { *this = E1RegState{}; }
};

struct E1Result {
    bool fired = false;
    uint32_t table_va = 0;
};

E1Result engine1_forward(const SectionImage& text, uint32_t site_pc,
                         uint32_t func_entry) {
    E1Result r;
    E1RegState regs[32];
    // Stack spill slots keyed by sp-relative offset (sw/sd ... lw/ld).
    // NOTE: sp adjustments between spill and reload would invalidate the
    // keying; the canonical idiom spills and reloads within one frame, so a
    // sp-modifying addiu clears the slots (conservative).
    struct Slot { int16_t off; E1RegState st; };
    std::vector<Slot> slots;

    const Insn jr = decode::decode(text.read_u32(site_pc));
    const uint8_t jr_rs = jr.rs;

    for (uint32_t pc = func_entry; pc < site_pc; pc += 4) {
        if (!text.contains(pc)) return r;
        const Insn in = decode::decode(text.read_u32(pc));
        if (in.kind != Kind::Other) continue; // control flow: linear pass (N64Recomp style)

        if (in.op == 15) { // lui rt, imm
            if (in.rt == kZero) continue;
            regs[in.rt].clear();
            regs[in.rt].has_base = true;
            regs[in.rt].base = static_cast<uint32_t>(static_cast<uint16_t>(in.simm)) << 16;
            continue;
        }
        if (in.op == 9 || in.op == 25) { // addiu/daddiu rt, rs, simm
            if (in.rt == kZero) continue;
            if (in.rs == kSp) { // sp adjust: invalidate tracked spill slots
                if (in.rt == kSp) slots.clear();
                regs[in.rt].clear();
                continue;
            }
            if (regs[in.rs].has_base && !regs[in.rs].indexed) {
                E1RegState st;
                st.has_base = true;
                st.base = regs[in.rs].base + static_cast<int32_t>(in.simm);
                regs[in.rt] = st;
            } else {
                regs[in.rt].clear();
            }
            continue;
        }
        if (is_move(in)) { // addu/daddu/or rd, rs, $zero
            regs[in.rd] = regs[in.rs];
            continue;
        }
        if (in.op == 0 && (in.funct == 33 || in.funct == 45)) { // addu/daddu rd, rs, rt
            if (in.rd == kZero) continue;
            const bool a = regs[in.rs].has_base && !regs[in.rs].indexed;
            const bool b = regs[in.rt].has_base && !regs[in.rt].indexed;
            if (a != b) { // exactly one side carries the table base
                E1RegState st;
                st.has_base = true;
                st.indexed = true;
                st.base = a ? regs[in.rs].base : regs[in.rt].base;
                regs[in.rd] = st;
            } else {
                regs[in.rd].clear();
            }
            continue;
        }
        if (in.op == 43 || in.op == 63) { // sw/sd rt, off(base)
            if (in.rs == kSp) {
                Slot s{in.simm, regs[in.rt]};
                auto it = std::find_if(slots.begin(), slots.end(),
                                       [&](const Slot& x) { return x.off == in.simm; });
                if (it != slots.end()) it->st = s.st; else slots.push_back(s);
            }
            continue; // stores never write registers
        }
        if (in.op == 35 || in.op == 55) { // lw/ld rt, off(base)
            if (in.rt == kZero) continue;
            if (in.rs == kSp) { // reload from a tracked spill slot
                auto it = std::find_if(slots.begin(), slots.end(),
                                       [&](const Slot& x) { return x.off == in.simm; });
                if (it != slots.end()) regs[in.rt] = it->st;
                else regs[in.rt].clear();
                continue;
            }
            if (regs[in.rs].indexed) { // load through base+index => table read
                E1RegState st;
                st.loaded = true;
                st.table_va = regs[in.rs].base + static_cast<int32_t>(in.simm);
                regs[in.rt] = st;
                continue;
            }
            regs[in.rt].clear();
            continue;
        }
        // Any other GPR write invalidates the destination's state.
        const int def = decode::gpr_def(in);
        if (def > 0) regs[def].clear();
    }

    if (regs[jr_rs].loaded) {
        r.fired = true;
        r.table_va = regs[jr_rs].table_va;
    }
    return r;
}

// ---------------------------------------------------------------------------
// ENGINE 2 — backward slice + guard + per-index evaluation.
// Adapted from Ghidra (Apache-2.0), National Security Agency;
// https://github.com/NationalSecurityAgency/ghidra (jumptable.cc, JumpBasic).
// ---------------------------------------------------------------------------
struct E2Result {
    bool fired = false;
    uint32_t table_va = 0;
    uint32_t bound = 0;
    bool has_default = false;   // guard branch's out-of-range destination
    uint32_t default_va = 0;
    std::string reason; // why not fired / notes (for the dossier)
};

// Backward search for the defining instruction of `reg` starting at `from`
// (exclusive), down to func_entry. Follows register moves. Returns the pc of
// the defining instruction, or nullopt.
std::optional<uint32_t> find_def_backward(const SectionImage& text,
                                          uint32_t func_entry, uint32_t from,
                                          uint8_t& reg) {
    for (uint32_t pc = from; pc > func_entry;) {
        pc -= 4;
        if (!text.contains(pc)) return std::nullopt;
        const Insn in = decode::decode(text.read_u32(pc));
        if (in.kind != Kind::Other) continue;
        const int def = decode::gpr_def(in);
        if (def != static_cast<int>(reg)) continue;
        if (is_move(in)) { reg = in.rs; continue; }  // follow the move chain
        return pc;
    }
    return std::nullopt;
}

// Resolve a lui(+addiu) constant chain ending at the instruction defining
// `reg` before `from`. Returns the constant, or nullopt.
std::optional<uint32_t> resolve_const_backward(const SectionImage& text,
                                               uint32_t func_entry,
                                               uint32_t from, uint8_t reg) {
    int32_t addend = 0;
    uint8_t cur = reg;
    uint32_t pos = from;
    for (int depth = 0; depth < 8; ++depth) {
        auto def = find_def_backward(text, func_entry, pos, cur);
        if (!def) return std::nullopt;
        const Insn in = decode::decode(text.read_u32(*def));
        if (in.op == 15) { // lui
            return (static_cast<uint32_t>(static_cast<uint16_t>(in.simm)) << 16) +
                   addend;
        }
        if (in.op == 9 || in.op == 25) { // addiu/daddiu rt, rs, simm
            addend += static_cast<int32_t>(in.simm);
            cur = in.rs;
            pos = *def;
            continue;
        }
        if ((in.op == 35 || in.op == 55) && in.rs == kSp) {
            // Reload from a stack slot: find the matching spill (sw/sd to the
            // same sp offset) above, then keep slicing its source register.
            for (uint32_t pc = *def; pc > func_entry;) {
                pc -= 4;
                const Insn sp = decode::decode(text.read_u32(pc));
                if ((sp.op == 43 || sp.op == 63) && sp.rs == kSp &&
                    sp.simm == in.simm) {
                    cur = sp.rt;
                    pos = pc;
                    break;
                }
                if (pc == func_entry) return std::nullopt;
            }
            continue;
        }
        return std::nullopt;
    }
    return std::nullopt;
}

E2Result engine2_backward(const SectionImage& text,
                          const std::vector<SectionImage>& data,
                          uint32_t site_pc, uint32_t func_entry, uint32_t func_end,
                          const DiscoveryOptions& opts) {
    E2Result r;
    const Insn jr = decode::decode(text.read_u32(site_pc));

    // 1. The jr's source must be defined by a load: lw target, off(addrreg).
    uint8_t reg = jr.rs;
    auto load_pc = find_def_backward(text, func_entry, site_pc, reg);
    if (!load_pc) { r.reason = "no defining load"; return r; }
    Insn load = decode::decode(text.read_u32(*load_pc));
    if (load.op != 35) { r.reason = "def is not lw"; return r; }

    // 2. The load address: addu/daddu addr, a, b — one side the table base
    //    constant, the other the scaled index.
    uint8_t addr_reg = load.rs;
    auto addr_pc = find_def_backward(text, func_entry, *load_pc, addr_reg);
    if (!addr_pc) { r.reason = "no address def"; return r; }
    Insn addr = decode::decode(text.read_u32(*addr_pc));
    if (!(addr.op == 0 && (addr.funct == 33 || addr.funct == 45))) {
        r.reason = "address not base+index add";
        return r;
    }

    // 3. Classify the two operands: constant base vs scaled index.
    auto base_a = resolve_const_backward(text, func_entry, *addr_pc, addr.rs);
    auto base_b = resolve_const_backward(text, func_entry, *addr_pc, addr.rt);
    uint8_t index_side;
    uint32_t base_const;
    if (base_a && !base_b) { base_const = *base_a; index_side = addr.rt; }
    else if (base_b && !base_a) { base_const = *base_b; index_side = addr.rs; }
    else { r.reason = base_a ? "both operands constant" : "no constant base"; return r; }

    // 4. The index side must come through a x4 scale (sll/dsll #2) — 32-bit
    //    table entries (per-index evaluation: address = base + idx*4 + off).
    uint8_t idx_reg = index_side;
    auto scale_pc = find_def_backward(text, func_entry, *addr_pc, idx_reg);
    if (!scale_pc) { r.reason = "no index def"; return r; }
    Insn scale = decode::decode(text.read_u32(*scale_pc));
    if (!(scale.op == 0 && (scale.funct == 0 || scale.funct == 56) && scale.sa == 2)) {
        r.reason = "index not scaled by 4";
        return r;
    }

    // 5. Guard bound (Ghidra GuardRecord concept): find a `sltiu g, x, BOUND`
    //    where x holds the pre-scale index VALUE at that point, paired with a
    //    beq/bne g, $zero. Value equivalence is tracked through register
    //    moves with kill positions, so a guard that tests the move's source
    //    register between the move and the scale is found (the
    //    `move a1,v0; sltiu v0,v0,N; ...; sll v1,a1,2` idiom). Searched to
    //    the function entry — no linear window (the SW-07 lesson).
    const uint8_t idx_src = scale.rt; // the pre-scale index register
    // Confirm a guard branch consuming the sltiu's result near `pc`: scan
    // forward to the jr site, stopping if the guard register is redefined
    // (the branch may sit many instructions later — compilers hoist the
    // sltiu above setup loops).
    // `start` = the first instruction of the path the guard's result flows along.
    auto scan_guard_from = [&](uint32_t start, uint32_t pc, const Insn& sltiu_in) -> bool {
        const uint32_t bound = static_cast<uint16_t>(sltiu_in.simm);
        for (uint32_t bpc = start; bpc < site_pc && text.contains(bpc); bpc += 4) {
            const Insn br = decode::decode(text.read_u32(bpc));
            if ((br.op == 4 || br.op == 5 || br.op == 20 || br.op == 21) &&
                ((br.rs == sltiu_in.rt && br.rt == kZero) ||
                 (br.rt == sltiu_in.rt && br.rs == kZero))) {
                if (bound == 0 || bound > opts.table_bound_cap) return false;
                r.fired = true;
                r.table_va = base_const + static_cast<int32_t>(load.simm);
                r.bound = bound;
                // The default path (GuardRecord): for `beq g,$zero` the
                // TAKEN side is out-of-range; for `bne g,$zero` (inverted
                // guard, SW-09) the FALLTHROUGH is.
                r.has_default = true;
                r.default_va = (br.op == 4 || br.op == 20)
                                   ? br.branch_target(bpc)
                                   : bpc + 8;
                return true;
            }
            if (decode::gpr_def(br) == static_cast<int>(sltiu_in.rt)) {
                // A redefinition in the delay slot of an unconditional
                // transfer belongs to the diverted path, not the guard's
                // fall path (the sltiu-in-delay-slot idiom) — skip it.
                const Insn prev = decode::decode(text.read_u32(bpc - 4));
                const bool diverted = prev.kind == Kind::UncondBranch ||
                                      prev.kind == Kind::J ||
                                      prev.kind == Kind::Jr ||
                                      prev.kind == Kind::JrRa;
                if (!diverted)
                    return false; // guard result overwritten before use
            }
        }
        return false;
    };
    auto confirm_guard = [&](uint32_t pc, const Insn& sltiu_in) -> bool {
        if (scan_guard_from(pc + 4, pc, sltiu_in)) return true;
        // The sltiu in the DELAY SLOT of a conditional branch (`bne x,c,L; sltiu g,x,N`):
        // the guard's consumer (`beqz g, default` at L) is on the TAKEN path, and the
        // fall-through block between pc+4 and L belongs to the other arm (it may redefine
        // g freely). Follow the taken path too.
        if (pc >= func_entry + 4 && text.contains(pc - 4)) {
            const Insn br = decode::decode(text.read_u32(pc - 4));
            if (br.kind == Kind::CondBranch || br.kind == Kind::CondBranchLikely) {
                const uint32_t target = br.branch_target(pc - 4);
                if (target > pc && target < site_pc && scan_guard_from(target, pc, sltiu_in)) return true;
            }
        }
        return false;
    };

    struct ScanState {
        bool alias[32] = {};
        uint32_t kill_at[32] = {};
    };
    ScanState init;
    init.alias[idx_src] = true;
    for (int i = 0; i < 32; ++i) init.kill_at[i] = *scale_pc; // nothing killed below scale yet
    // A block that only a branch can enter: its linear predecessor is the delay slot of an
    // unconditional transfer. The backward scan reaching such a leader has walked off the
    // path the index value actually travelled, so it continues from every in-function branch
    // that targets the leader (starting at that branch's delay slot) -- the
    // `bne x,c,L; sltiu g,x,N; <other arm>; L: beqz g,default; sll ..` idiom, where the guard
    // sits in the branch's delay slot and the other arm redefines x. Visited leaders are
    // remembered so a loop cannot recurse forever; the hop depth is bounded.
    std::vector<uint32_t> hopped_leaders;
    auto branch_only_leader = [&](uint32_t pc) -> bool {
        if (pc < func_entry + 8 || !text.contains(pc - 8)) return false;
        const Insn prev = decode::decode(text.read_u32(pc - 8));
        return prev.kind == Kind::UncondBranch || prev.kind == Kind::J ||
               prev.kind == Kind::Jr || prev.kind == Kind::JrRa;
    };
    // One instruction of the backward walk: the guard test, the move-chain aliasing and the
    // kill bookkeeping of the original linear scan. Returns true when the guard was confirmed.
    auto consume = [&](uint32_t pc, ScanState& st) -> bool {
        const Insn in = decode::decode(text.read_u32(pc));
        if (in.kind != Kind::Other) return false;
        if (in.op == 11 && st.alias[in.rs]) { // sltiu g, live-alias, imm
            if (confirm_guard(pc, in)) return true;
        }
        const int def = decode::gpr_def(in);
        if (is_move(in) && st.alias[in.rd]) {
            // The move's source holds the value in (pc, kill_at[rs]] — the
            // kill instruction itself reads its sources first, so an sltiu
            // that both tests and redefines the register still counts.
            const uint8_t src = in.rs;
            for (uint32_t spc = st.kill_at[src]; spc > pc; spc -= 4) {
                if (!text.contains(spc)) break;
                const Insn s = decode::decode(text.read_u32(spc));
                if (s.kind != Kind::Other) continue;
                if (s.op == 11 && s.rs == src && confirm_guard(spc, s)) return true;
            }
            st.alias[src] = true;
            st.alias[in.rd] = false;   // above the move, rd no longer holds it
            if (def > 0) st.kill_at[def] = pc;
            return false;
        }
        if (def > 0) {
            if (st.alias[def]) st.alias[def] = false; // non-move redefinition breaks it
            st.kill_at[def] = pc;
        }
        return false;
    };
    std::function<bool(uint32_t, ScanState, int)> scan_back = [&](uint32_t from, ScanState st, int depth) -> bool {
        for (uint32_t pc = from; pc > func_entry;) {
            pc -= 4;
            if (consume(pc, st)) return true;
            // `pc` has been consumed; if it is a branch-only leader, the value came in by a branch.
            if (depth < 4 && branch_only_leader(pc) &&
                std::find(hopped_leaders.begin(), hopped_leaders.end(), pc) == hopped_leaders.end()) {
                hopped_leaders.push_back(pc);
                for (uint32_t bpc = func_entry; bpc + 4 < func_end && text.contains(bpc); bpc += 4) {
                    const Insn br = decode::decode(text.read_u32(bpc));
                    if (!br.is_branch_like() || br.branch_target(bpc) != pc) continue;
                    // Continue ABOVE the branch's delay slot: the slot (bpc+4) is the
                    // first instruction the hopped scan consumes.
                    if (scan_back(bpc + 8, st, depth + 1)) return true;
                }
                // No predecessor path confirmed a guard: fall through to the linear
                // continuation, exactly the scan this hop was added to (a strict superset
                // of the original search -- it can only ADD a found guard, never lose one).
            }
        }
        return false;
    };
    if (scan_back(*scale_pc, init, 0)) return r;
    r.reason = "no dominating sltiu guard";
    return r;
}

const SectionImage* find_section(const std::vector<SectionImage>& data,
                                 const SectionImage& text, uint32_t va,
                                 uint32_t bytes) {
    for (const auto& s : data)
        if (s.contains(va) && s.contains(va + bytes - 1)) return &s;
    if (text.contains(va) && text.contains(va + bytes - 1)) return &text;
    return nullptr;
}

std::string hex32(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

} // namespace

TableRecovery recover_jump_table(const SectionImage& text,
                                 const std::vector<SectionImage>& data,
                                 uint32_t site_pc,
                                 uint32_t func_entry,
                                 uint32_t func_end,
                                 const DiscoveryOptions& opts) {
    TableRecovery out;
    if (!text.contains(site_pc)) {
        out.is_table = false;
        return out;
    }

    const E1Result e1 = engine1_forward(text, site_pc, func_entry);
    const E2Result e2 = engine2_backward(text, data, site_pc, func_entry, func_end, opts);

    // Cross-check (design §7.2): the two engines must agree on whether the
    // site is a table and on the table base. Disagreement = hard error with a
    // dossier (discover_program turns this into a build failure).
    if (e1.fired != e2.fired) {
        out.engines_agree = false;
        out.dossier = "jump-table cross-check disagreement at site " + hex32(site_pc) +
                      " (function " + hex32(func_entry) + "): engine1(forward,N64Recomp)=" +
                      (e1.fired ? "table@" + hex32(e1.table_va) : "no-table") +
                      " engine2(backward,Ghidra)=" +
                      (e2.fired ? "table@" + hex32(e2.table_va) +
                                      " bound=" + std::to_string(e2.bound)
                                : "no-table(" + e2.reason + ")");
        return out;
    }
    if (!e1.fired) { out.is_table = false; return out; } // agreed: computed jump (D4)
    if (e1.table_va != e2.table_va) {
        out.engines_agree = false;
        out.dossier = "jump-table base disagreement at site " + hex32(site_pc) +
                      ": engine1=" + hex32(e1.table_va) +
                      " engine2=" + hex32(e2.table_va) +
                      " bound=" + std::to_string(e2.bound);
        return out;
    }

    // Agreed table: read the entries from the ELF image (never from block
    // metadata) and sanity-check them (Ghidra sanityCheck concept).
    const uint32_t bound = e2.bound;
    const SectionImage* sec = find_section(data, text, e2.table_va, bound * 4);
    if (!sec) { out.is_table = false; return out; } // base not in any image

    JumpTable t;
    t.site_pc = site_pc;
    t.table_base = e2.table_va;
    t.bound = bound;
    uint32_t bad = 0;
    for (uint32_t i = 0; i < bound; ++i) {
        const uint32_t entry = sec->read_u32(e2.table_va + i * 4);
        if ((entry & 3u) != 0 || !text.contains(entry)) ++bad;
        t.targets.push_back(entry);
    }
    if (bad * 2 >= bound) { out.is_table = false; return out; } // >= half invalid
    out.has_guard_default = e2.has_default;
    out.guard_default = e2.default_va;
    // internal/external classification is finalized by discover_program
    // against the owner's final extent; provide a provisional split here for
    // direct callers (tests/review) using [func_entry, func_end).
    for (uint32_t tgt : t.targets) {
        if ((tgt & 3u) != 0 || !text.contains(tgt)) continue;
        if (tgt >= func_entry && tgt < func_end) t.internal_targets.insert(tgt);
        else t.external_targets.insert(tgt);
    }
    out.is_table = true;
    out.table = std::move(t);
    return out;
}

// ---------------------------------------------------------------------------
// [RECOMP-P2b] Weak seed source 6 — code-constructed pointers (see the
// declaration comment in jumptables.h). This is Engine 1's constant-
// resolution state machine (lui base / addiu derive / move copy / def kill,
// control flow passed over linearly — the same rules as engine1_forward
// above, N64Recomp-adapted per the file-header MIT notice) run once over the
// whole .text image, collecting every derived constant that lands 4-aligned
// inside .text. The delay-slot form `lui rX,hi ; jal F ; addiu rX,rX,lo`
// resolves because control-flow words are skipped, exactly as in E1.
// ---------------------------------------------------------------------------
std::map<uint32_t, std::vector<uint32_t>> scan_code_constructed_pointers(
    const SectionImage& text) {
    std::map<uint32_t, std::vector<uint32_t>> out;
    struct St { bool has = false; uint32_t base = 0; };
    St regs[32];
    const uint32_t tbase = text.base_va;
    const uint32_t tend = tbase + static_cast<uint32_t>(text.bytes.size());
    for (uint32_t pc = tbase; pc + 4 <= tend; pc += 4) {
        const Insn in = decode::decode(text.read_u32(pc));
        if (in.kind != Kind::Other) continue; // linear pass through control flow (E1 rule)
        if (in.op == 15) { // lui rt, imm
            if (in.rt == kZero) continue;
            regs[in.rt].has = true;
            regs[in.rt].base =
                static_cast<uint32_t>(static_cast<uint16_t>(in.simm)) << 16;
            continue;
        }
        if (in.op == 9 || in.op == 25) { // addiu/daddiu rt, rs, simm
            if (in.rt == kZero) continue;
            if (in.rs == kSp) { // sp-relative arithmetic never builds code ptrs
                regs[in.rt].has = false;
                continue;
            }
            if (regs[in.rs].has) {
                const uint32_t v = regs[in.rs].base + static_cast<int32_t>(in.simm);
                if ((v & 3u) == 0 && v >= tbase && v <= tend - 4)   // no u32 wrap for v near 0xFFFFFFFF
                    out[v].push_back(pc);
                regs[in.rt].has = true;
                regs[in.rt].base = v;
            } else {
                regs[in.rt].has = false;
            }
            continue;
        }
        if (is_move(in)) { // addu/daddu/or rd, rs, $zero
            regs[in.rd] = regs[in.rs];
            continue;
        }
        const int d = decode::gpr_def(in);
        if (d > 0) regs[d].has = false;
    }
    return out;
}

} // namespace recomp_v2
