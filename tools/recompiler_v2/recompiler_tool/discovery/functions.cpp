//
// functions.cpp — RECOMP v2 discovery: multi-source function discovery,
// extents, and the whole-pipeline entry point discover_program().
//
// [RECOMP-P2] Implements RECOMP_P0_DESIGN.md §7.1/§7.3 iterated to a fixed
// point (the angr CFGFast multi-source model — ideas only):
//
//   Seeds:   1. ELF entry point            (strong)
//            2. jal targets, full sweep    (strong)
//            3. prologue patterns          (weak: seed-only, never authoritative)
//            4. data-section pointer scan  (weak)
//            5. external jump-table targets(strong, discovered iteratively)
//            6. code-constructed pointers  (weak; P2b — lui/addiu pairs
//               resolving to .text addresses; promoted before prologues so
//               referenced true entries beat late-landing prologue shapes)
//
//   Walker:  global recursive descent from every strong entry. Delay slots
//            execute with their transfer; conditional branches continue at
//            target and pc+8; jal/jalr continue at the return site pc+8;
//            jr $ra / eret stop; `jr rs` runs the two-engine jump-table
//            recovery (jumptables.cpp) — cross-check disagreement is a hard
//            error (throws, with the dossier).
//
//   Extents: a function's region is capped by the next strong start; its
//            extent is trimmed to the reachable-code closure (last reached
//            instruction + 4). Parked branch/jump targets attach when the
//            closure grows to meet them (or across an all-zero padding run);
//            jump-table targets attach only by direct closure contact, so
//            tail-call dispatch tables stay external (fixture SW-04 vs SW-01).
//
//   Weak seeds: absorbed when inside a claimed extent (reached flow), else
//            promoted to functions. An absorbed PROLOGUE seed strictly inside
//            a function's extent marks folds_successor (fixture ST-03; data
//            pointers do not fold — fixture SW-01).
//
// GAME-AGNOSTIC RULE (design §6): no game addresses anywhere in discovery/.
//

#include "functions.h"
#include "jumptables.h"
#include "leaders.h"
#include "rv2_decode.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <stdexcept>

namespace recomp_v2 {

namespace {

using decode::Insn;
using decode::Kind;

struct Walker {
    const DiscoveryInputs& in;
    const DiscoveryOptions& opts;
    const SectionImage& text;
    uint32_t text_end;

    // Function starts (strong + promoted weak). Sorted invariant via std::set.
    std::set<uint32_t> starts;
    // Seed provenance per entry (additive).
    std::map<uint32_t, std::set<SeedSource>> seed_of;
    // Closure-end cache: entry -> last visited word in its region + 4.
    // Maintained incrementally by mark() and repaired on region splits.
    std::map<uint32_t, uint32_t> end_cache;

    // Per-word visited flags (index = (va - base) / 4).
    std::vector<uint8_t> visited;
    // Parked jump-table targets (attach by chaining; promote if never attached).
    std::set<uint32_t> parked_table;
    // Tail-call dispatch targets (never chained; promoted as functions).
    std::set<uint32_t> parked_dispatch;
    // Jump-table recovery cache by site (recover once).
    std::map<uint32_t, TableRecovery> table_cache;
    // Recovered table data ranges [base, end): words inside them are TABLE
    // ENTRIES, not independent vtable pointers — excluded from the weak
    // data-pointer promotion (the 0x21bc1c lesson: a case block whose only
    // data reference is its own jump-table slot must chain, not pre-promote).
    std::map<uint32_t, uint32_t> table_ranges;

    bool va_in_table_data(uint32_t va) const {
        auto it = table_ranges.upper_bound(va);
        if (it == table_ranges.begin()) return false;
        return va < std::prev(it)->second;
    }
    // Walk queue.
    std::deque<uint32_t> queue;

    explicit Walker(const DiscoveryInputs& in_, const DiscoveryOptions& opts_)
        : in(in_), opts(opts_), text(in_.text),
          text_end(in_.text.base_va + static_cast<uint32_t>(in_.text.bytes.size())) {
        visited.assign(text.bytes.size() / 4 + 1, 0);
    }

    // `va <= end - 4`, not `va + 4 <= end`: a data word such as 0xFFFFFFFC wraps the latter
    // (functions.cpp:361 / jumptables.cpp:502 feed every word of .data through this).
    bool in_text(uint32_t va) const { return va >= text.base_va && va <= text_end - 4; }
    // True when `va` is the delay slot of the transfer instruction at va-4.
    // Every R5900 branch/jump kind (conditional, likely, unconditional, j,
    // jal, jr, jalr) carries one delay slot; eret and ordinary instructions
    // do not. A static decode of the preceding word is used because the
    // question is asked for addresses NOT yet reached by any flow (reached
    // delay slots are already visited and never promote).
    bool in_delay_slot(uint32_t va) const {
        if (va < text.base_va + 4 || !in_text(va)) return false;
        const Kind k = decode::decode(text.read_u32(va - 4)).kind;
        return k != Kind::Other && k != Kind::Eret;
    }
    bool is_visited(uint32_t va) const { return visited[(va - text.base_va) / 4] != 0; }
    void mark(uint32_t va) {
        auto& v = visited[(va - text.base_va) / 4];
        if (v) return;
        v = 1;
        auto own = owner_of(va);
        if (own) {
            auto& e = end_cache[*own];
            if (e < va + 4) e = va + 4;
        }
    }

    // The function start owning `va`: greatest start <= va (nullopt if before
    // the first start).
    std::optional<uint32_t> owner_of(uint32_t va) const {
        auto it = starts.upper_bound(va);
        if (it == starts.begin()) return std::nullopt;
        return *std::prev(it);
    }
    // Region cap for a function: the next start (or end of .text).
    uint32_t region_cap(uint32_t entry) const {
        auto it = starts.upper_bound(entry);
        return it == starts.end() ? text_end : *it;
    }
    // Closure end of a function = last visited word in its region + 4.
    uint32_t closure_end(uint32_t entry) const {
        auto it = end_cache.find(entry);
        return it == end_cache.end() ? entry : std::max(entry, it->second);
    }

    void add_seed(uint32_t entry, SeedSource src) {
        seed_of[entry].insert(src);
    }
    void add_start(uint32_t entry, SeedSource src) {
        add_seed(entry, src);
        if (!starts.insert(entry).second) return;
        queue.push_back(entry);
        // Region split repair: visited words >= entry that belonged to the
        // predecessor now belong to `entry` — rescan both bounded regions.
        auto it = starts.find(entry);
        if (it != starts.begin()) {
            const uint32_t pred = *std::prev(it);
            auto ce = end_cache.find(pred);
            if (ce != end_cache.end() && ce->second > entry) {
                uint32_t pe = pred;
                for (uint32_t va = pred; va < entry; va += 4)
                    if (is_visited(va)) pe = va + 4;
                if (pe > pred) ce->second = pe; else end_cache.erase(ce);
                const uint32_t cap = region_cap(entry);
                uint32_t ne = entry;
                for (uint32_t va = entry; va < cap; va += 4)
                    if (is_visited(va)) ne = va + 4;
                if (ne > entry) end_cache[entry] = ne;
            }
        }
    }

    // Route a control-flow target: at a start = call/tail edge (walked as a
    // function); inside a region = a leader of that region's function — walk
    // it directly (the region cap bounds the extent; a branch to another
    // function's interior is the ST-01/ST-02 mid-entry case).
    void route_flow_target(uint32_t tgt) {
        if (!in_text(tgt) || (tgt & 3u)) return;
        if (starts.count(tgt)) return;      // another function's entry
        if (is_visited(tgt)) return;        // already claimed
        if (!owner_of(tgt)) return;         // before the first function: gap
        queue.push_back(tgt);
    }

    // Step one instruction; enqueue successors. Returns after handling one pc.
    void step(uint32_t pc) {
        if (!in_text(pc) || is_visited(pc)) return;
        mark(pc);
        const Insn ins = decode::decode(text.read_u32(pc));
        const uint32_t slot = pc + 4;
        switch (ins.kind) {
            case Kind::CondBranch:
            case Kind::CondBranchLikely: {
                if (in_text(slot)) mark(slot);
                route_flow_target(ins.branch_target(pc));
                if (in_text(pc + 8)) queue.push_back(pc + 8); // fallthrough
                break;
            }
            case Kind::UncondBranch: {
                if (in_text(slot)) mark(slot);
                route_flow_target(ins.branch_target(pc));
                break;
            }
            case Kind::J: {
                if (in_text(slot)) mark(slot);
                route_flow_target(ins.jump_target(pc));
                break;
            }
            case Kind::Jal: {
                if (in_text(slot)) mark(slot);
                // Callee is a strong seed (collected in the pre-scan; also
                // here for jals discovered in promoted-weak code).
                const uint32_t callee = ins.jump_target(pc);
                if (in_text(callee)) add_start(callee, SeedSource::kJalTarget);
                if (in_text(pc + 8)) queue.push_back(pc + 8); // return site
                break;
            }
            case Kind::Jalr: {
                if (in_text(slot)) mark(slot);
                if (in_text(pc + 8)) queue.push_back(pc + 8); // return site
                break;
            }
            case Kind::JrRa:
            case Kind::Eret: {
                if (ins.kind == Kind::JrRa && in_text(slot)) mark(slot);
                break; // hard stop
            }
            case Kind::Jr: {
                if (in_text(slot)) mark(slot);
                handle_jr_site(pc);
                break;
            }
            case Kind::Other:
            default:
                // A no-return kernel syscall (Exit / ExitThread / ExitDeleteThread in the SDK's
                // `addiu v1,zero,N ; syscall` stub shape) is a hard stop like `jr ra`: the next
                // word may be another function's entry (rv2_decode.h is_noreturn_syscall_imm).
                if (decode::is_syscall_word(ins.word) && pc >= text.base_va + 4 &&
                    decode::is_noreturn_syscall_imm(text.read_u32(pc - 4)))
                    break;
                queue.push_back(pc + 4);
                break;
        }
    }

    void handle_jr_site(uint32_t site) {
        auto own = owner_of(site);
        const uint32_t fentry = own ? *own : text.base_va;
        auto it = table_cache.find(site);
        if (it == table_cache.end()) {
            TableRecovery rec = recover_jump_table(text, in.data, site, fentry,
                                                   region_cap(fentry), opts);
            it = table_cache.emplace(site, std::move(rec)).first;
        }
        if (!it->second.engines_agree) {
            // CONVENTIONS D3 / design §7.2: a cross-check disagreement at a
            // REACHED jr site is a HARD ERROR with the dossier.
            throw std::runtime_error("[recomp-v2] " + it->second.dossier);
        }
        if (!it->second.is_table) return; // computed jump: runtime dispatcher (D4)
        const TableRecovery& rec = it->second;
        {
            auto& e = table_ranges[rec.table.table_base];
            const uint32_t hi = rec.table.table_base + rec.table.bound * 4;
            if (e < hi) e = hi;
        }

        // Tail-call-dispatch layout test (fixtures ST-05/SW-04 vs SW-01/SW-10;
        // pinned by the fixture catalog): when the switch's own default block
        // sits between the jr and the first case target, the function is
        // complete without the case region — the targets are dispatch
        // destinations (external function entries), so case-chaining is
        // suppressed and they promote as functions at the fixed point.
        uint32_t min_target = UINT32_MAX;
        for (uint32_t tgt : rec.table.targets)
            if ((tgt & 3u) == 0 && in_text(tgt) && tgt < min_target) min_target = tgt;
        const bool tail_dispatch =
            rec.has_guard_default && min_target != UINT32_MAX &&
            rec.guard_default >= site && rec.guard_default < min_target;

        // The guard default itself is the function's own flow either way.
        if (rec.has_guard_default) route_flow_target(rec.guard_default);

        for (uint32_t tgt : rec.table.targets) {
            if (!in_text(tgt) || (tgt & 3u)) continue;
            if (starts.count(tgt) || is_visited(tgt)) continue;
            if (tail_dispatch) parked_dispatch.insert(tgt);
            else parked_table.insert(tgt);
        }
    }

    void drain_queue() {
        while (!queue.empty()) {
            const uint32_t pc = queue.front();
            queue.pop_front();
            step(pc);
        }
    }

    // Attach parked TABLE targets whose owner's closure now reaches them.
    // Chaining is by direct contact only: a padding gap before a target
    // means dispatch territory (SW-04/SW-05), never a case chain.
    bool attach_parked() {
        bool any = false;
        for (auto it = parked_table.begin(); it != parked_table.end();) {
            const uint32_t tgt = *it;
            if (is_visited(tgt) || starts.count(tgt)) {
                it = parked_table.erase(it);
                any = true;
                continue;
            }
            auto own = owner_of(tgt);
            if (own && tgt <= closure_end(*own)) {
                queue.push_back(tgt);
                it = parked_table.erase(it);
                any = true;
            } else {
                ++it;
            }
        }
        return any;
    }

    // Saturate: walk + attach until nothing moves.
    void saturate() {
        for (;;) {
            drain_queue();
            if (!attach_parked()) break;
        }
    }
};

} // namespace

DiscoveryResult discover_program(const DiscoveryInputs& in,
                                 const DiscoveryOptions& opts) {
    DiscoveryResult result;
    if (in.text.bytes.size() < 4) return result;

    Walker w(in, opts);

    // ---- Seed collection --------------------------------------------------
    // Seed 1: ELF entry point.
    if (w.in_text(in.elf_entry)) w.add_start(in.elf_entry, SeedSource::kElfEntry);

    // Seed 2: jal targets (full linear sweep — jals in not-yet-discovered
    // code still count; the census showed the sweep is safe on this class of
    // images and the walker re-adds any jal found in promoted code anyway).
    const uint32_t tbase = in.text.base_va;
    const uint32_t tend = tbase + static_cast<uint32_t>(in.text.bytes.size());
    for (uint32_t pc = tbase; pc + 4 <= tend; pc += 4) {
        const Insn ins = decode::decode(in.text.read_u32(pc));
        if (ins.kind == Kind::Jal) {
            const uint32_t tgt = ins.jump_target(pc);
            if (w.in_text(tgt)) w.add_start(tgt, SeedSource::kJalTarget);
        }
    }

    // Weak seeds (SEED-ONLY, design §7.3): prologue patterns + data pointers
    // + code-constructed pointers (P2b seed source 6, RECOMP_P2_REVIEW.md #3).
    std::set<uint32_t> weak_prologue;
    std::map<uint32_t, std::vector<uint32_t>> weak_pointer; // target -> source VAs
    if (opts.enable_prologue_seeds) {
        for (uint32_t pc = tbase; pc + 4 <= tend; pc += 4)
            if (decode::is_prologue_word(in.text.read_u32(pc)))
                weak_prologue.insert(pc);
    }
    if (opts.enable_data_pointer_seeds) {
        for (const auto& sec : in.data) {
            const uint32_t n = static_cast<uint32_t>(sec.bytes.size());
            for (uint32_t off = 0; off + 4 <= n; off += 4) {
                const uint32_t v = sec.read_u32(sec.base_va + off);
                if ((v & 3u) == 0 && v >= tbase && v <= tend - 4)   // no u32 wrap for v near 0xFFFFFFFF
                    weak_pointer[v].push_back(sec.base_va + off);
            }
        }
    }
    // Seed source 6: lui/addiu-constructed .text addresses (address-taken
    // functions; the FUN_002d3218 class). E1's constant-resolution rules,
    // program-wide (jumptables.cpp).
    const std::map<uint32_t, std::vector<uint32_t>> weak_codeptr =
        scan_code_constructed_pointers(in.text);

    // Table pre-pass: recover every `jr rs` (rs != $ra) site ONCE, up front,
    // using the strong-start regions. This registers every table's data range
    // before any weak-pointer decision is made (a case block whose only data
    // reference is its own jump-table slot must chain, never pre-promote).
    // Disagreements are cached, NOT thrown here: unreached words in .text can
    // be data that happens to decode as jr — the hard error fires in
    // handle_jr_site when (and only when) real control flow reaches the site.
    for (uint32_t pc = tbase; pc + 4 <= tend; pc += 4) {
        const Insn ins = decode::decode(in.text.read_u32(pc));
        if (ins.kind != Kind::Jr) continue;
        auto own = w.owner_of(pc);
        const uint32_t fentry = own ? *own : tbase;
        TableRecovery rec = recover_jump_table(in.text, in.data, pc, fentry,
                                               w.region_cap(fentry), opts);
        if (rec.engines_agree && rec.is_table) {
            auto& e = w.table_ranges[rec.table.table_base];
            const uint32_t hi = rec.table.table_base + rec.table.bound * 4;
            if (e < hi) e = hi;
        }
        w.table_cache.emplace(pc, std::move(rec));
    }

    // A pointer target counts as vtable/callback-referenced only if some
    // source word lies OUTSIDE every recovered jump table's data range
    // (table slots are table entries, not independent code pointers).
    auto independently_pointed = [&](uint32_t target) {
        auto it = weak_pointer.find(target);
        if (it == weak_pointer.end()) return false;
        for (uint32_t src : it->second)
            if (!w.va_in_table_data(src)) return true;
        return false;
    };

    // ---- Fixed point: saturate; promote external table targets; then
    //      promote unreached weak seeds; repeat until stable. ---------------
    for (;;) {
        w.saturate();

        // Promote external table targets to functions (seed source 5, ST-05):
        // tail-dispatch targets always; ordinary table targets only when they
        // never chained into their owner (SW-05's gap case).
        bool changed = false;
        // Tail-call dispatch targets are DEFINITE function entries: promote
        // unconditionally (consecutive stubs may fall through into each
        // other — the FUN_002b19d0 continuation-block layout — and still be
        // separate dispatch destinations).
        for (auto it = w.parked_dispatch.begin(); it != w.parked_dispatch.end();) {
            w.add_start(*it, SeedSource::kTableExternal);
            it = w.parked_dispatch.erase(it);
            changed = true;
        }
        // Ordinary table targets that never chained are external (SW-05's
        // gap case) — unless they sit inside an already-claimed extent.
        for (auto it = w.parked_table.begin(); it != w.parked_table.end();) {
            const uint32_t tgt = *it;
            auto own = w.owner_of(tgt);
            const bool inside_extent = own && tgt < w.closure_end(*own);
            if (!inside_extent) {
                w.add_start(tgt, SeedSource::kTableExternal);
                it = w.parked_table.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (changed) continue;

        // Promote weak seeds not reached by any flow and not inside a claimed
        // extent (PCSX2 #4024 lesson: prologues are never authoritative).
        // A weak seed in a branch delay slot is NEVER a function start: the
        // slot executes with its transfer on both paths, so a function that
        // began there would re-run the slot (a prologue `addiu sp,sp,-N` in
        // the slot of the entry's guard branch would be executed twice on the
        // fall-through path — one epilogue, net sp drift per call). Such a
        // seed is rejected and recorded; the real entry is found by its own
        // seed (jal / pointer / table) or the words stay a reported gap.
        auto eligible = [&](uint32_t s) {
            if (w.starts.count(s) || w.is_visited(s)) return false;
            auto own = w.owner_of(s);
            if (own && s < w.closure_end(*own)) return false; // inside claimed extent
            if (w.in_delay_slot(s)) {
                result.delay_slot_weak_rejected.insert(s);
                return false;
            }
            return true;
        };
        bool promoted = false;
        // [RECOMP-P2b] Code-constructed pointers promote FIRST among the weak
        // sources: they carry referenced-by-code evidence at the TRUE entry,
        // where the shape-only prologue scan can land late (FUN_002d3218's
        // first word is a lui, so its prologue word sits at entry+4 — a
        // prologue-first promotion would mint that false entry and shift the
        // 0x2d32d8 table's ownership; reviewer finding F1). Promoting the
        // referenced entry first lets its walk absorb the interior prologue.
        for (const auto& kv : weak_codeptr) {
            if (!eligible(kv.first)) continue;
            w.add_start(kv.first, SeedSource::kCodePointer);
            promoted = true; // one promotion per round: extents settle between
            break;
        }
        if (promoted) continue;
        for (uint32_t s : weak_prologue) {
            if (!eligible(s)) continue;
            w.add_start(s, SeedSource::kProloguePattern);
            promoted = true; // one promotion per round: extents settle between
            break;
        }
        if (promoted) continue;
        for (const auto& kv : weak_pointer) {
            if (!eligible(kv.first) || !independently_pointed(kv.first)) continue;
            w.add_start(kv.first, SeedSource::kDataPointer);
            promoted = true;
            break;
        }
        if (promoted) continue;
        break;
    }

    // ---- Materialize functions + extents ----------------------------------
    for (uint32_t entry : w.starts) {
        DiscoveredFunction fn;
        fn.entry = entry;
        fn.end = std::max(w.closure_end(entry), entry + 4);
        fn.seeds = w.seed_of[entry];
        result.functions.emplace(entry, std::move(fn));
    }
    // Record additive pointer provenance on discovered entries (vtable-style
    // references only; table slots are not independent pointers). Same for
    // code-constructed pointers (P2b seed source 6).
    for (const auto& kv : weak_pointer)
        if (result.functions.count(kv.first) && independently_pointed(kv.first))
            result.functions[kv.first].seeds.insert(SeedSource::kDataPointer);
    for (const auto& kv : weak_codeptr)
        if (result.functions.count(kv.first))
            result.functions[kv.first].seeds.insert(SeedSource::kCodePointer);

    auto containing = [&](uint32_t va) -> DiscoveredFunction* {
        auto it = result.functions.upper_bound(va);
        if (it == result.functions.begin()) return nullptr;
        auto& fn = std::prev(it)->second;
        return (va >= fn.entry && va < fn.end) ? &fn : nullptr;
    };

    // ---- Tables: attach to owners; final internal/external split ----------
    // Only sites the walker actually reached count (the pre-pass may have
    // recovered "tables" at data words that decode as jr — never executed).
    for (auto& kv : w.table_cache) {
        if (!kv.second.is_table || !kv.second.engines_agree) continue;
        if (!w.is_visited(kv.first)) continue;
        DiscoveredFunction* owner = containing(kv.first);
        if (!owner) continue;
        JumpTable t = kv.second.table;
        t.internal_targets.clear();
        t.external_targets.clear();
        for (uint32_t tgt : t.targets) {
            if ((tgt & 3u) || !w.in_text(tgt)) continue;
            if (tgt >= owner->entry && tgt < owner->end) t.internal_targets.insert(tgt);
            else t.external_targets.insert(tgt);
        }
        owner->tables.push_back(std::move(t));
    }
    for (auto& kv : result.functions) {
        auto& v = kv.second.tables;
        std::sort(v.begin(), v.end(),
                  [](const JumpTable& a, const JumpTable& b) { return a.site_pc < b.site_pc; });
    }

    // ---- Leaders + edges: linear sweep over each extent (CONVENTIONS D1) --
    // Every address that flow can enter FROM OUTSIDE its function must be a
    // leader of the function that contains it (the emitted `switch (addr)`
    // carries a case per leader): branch targets, the fall-through successor
    // of a branch delay slot, jal/jalr return sites and jump-table targets —
    // whichever function contains them, not only the one holding the site.
    for (auto& kv : result.functions) {
        DiscoveredFunction& fn = kv.second;
        fn.leaders.insert(fn.entry);
        for (const auto& t : fn.tables) {
            for (uint32_t tgt : t.internal_targets) fn.leaders.insert(tgt);
            for (uint32_t tgt : t.external_targets)
                if (DiscoveredFunction* o = containing(tgt)) o->leaders.insert(tgt);
        }
    }
    for (auto& kv : result.functions) {
        DiscoveredFunction& fn = kv.second;
        for (uint32_t pc = fn.entry; pc + 4 <= fn.end; pc += 4) {
            if (!w.in_text(pc)) break;
            const Insn ins = decode::decode(in.text.read_u32(pc));
            switch (ins.kind) {
                case Kind::CondBranch:
                case Kind::CondBranchLikely: {
                    const uint32_t tgt = ins.branch_target(pc);
                    if (DiscoveredFunction* o = containing(tgt)) o->leaders.insert(tgt);
                    result.edges.emplace(pc, tgt);
                    const uint32_t ft = pc + 8;
                    if (DiscoveredFunction* o = containing(ft)) o->leaders.insert(ft);
                    result.edges.emplace(pc, ft);
                    break;
                }
                case Kind::UncondBranch: {
                    const uint32_t tgt = ins.branch_target(pc);
                    if (DiscoveredFunction* o = containing(tgt)) o->leaders.insert(tgt);
                    result.edges.emplace(pc, tgt);
                    break;
                }
                case Kind::J: {
                    const uint32_t tgt = ins.jump_target(pc);
                    if (DiscoveredFunction* o = containing(tgt)) o->leaders.insert(tgt);
                    result.edges.emplace(pc, tgt);
                    break;
                }
                case Kind::Jal: {
                    const uint32_t tgt = ins.jump_target(pc);
                    result.edges.emplace(pc, tgt);
                    const uint32_t ret = pc + 8;
                    if (DiscoveredFunction* o = containing(ret)) o->leaders.insert(ret);
                    break;
                }
                case Kind::Jalr: {
                    const uint32_t ret = pc + 8;
                    if (DiscoveredFunction* o = containing(ret)) o->leaders.insert(ret);
                    break;
                }
                default:
                    break;
            }
        }
    }

    // ---- strong entries in a delay slot (diagnostics; the entry is kept) ----
    for (const auto& kv : result.functions)
        if (w.in_delay_slot(kv.first)) result.delay_slot_strong_entries.insert(kv.first);

    // ---- folds_successor: absorbed PROLOGUE seed strictly inside an extent
    for (uint32_t p : weak_prologue) {
        if (w.starts.count(p)) continue;         // it became a function
        if (!w.is_visited(p)) continue;          // never reached: padding/data
        DiscoveredFunction* o = containing(p);
        if (o && p > o->entry) o->folds_successor = true;
    }

    // ---- unreached gaps (diagnostics) --------------------------------------
    for (uint32_t pc = tbase; pc + 4 <= tend; pc += 4)
        if (!w.is_visited(pc)) result.unreached_gaps.insert(pc);

    // ---- table data ranges (diagnostics, P2b) ------------------------------
    result.table_data_ranges = w.table_ranges;

    result.engine_ran = true;
    return result;
}

} // namespace recomp_v2
