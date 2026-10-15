//
// driver_v2.cpp — [RECOMP-P3a] the v2 emission driver: turns the discovery
// engine's DiscoveryResult into the Function map + V2EmissionConfig the
// (reused) legacy Recompiler emits from.
//
// Faithful-reuse contract (RECOMP_REFACTOR_BRIEF pillar "no IR"): this file
// produces DATA ONLY — no instruction is translated here. Blocks are decoded
// with rabbitizer exactly as EEAnalyze/analyze.cpp does for the legacy
// engine, so translate_instruction sees byte-identical inputs.
//
// Determinism (CONVENTIONS P2): every container is std::map/std::set; no
// unordered iteration, no timestamps, no pointers in any output.
//
#include "emit/driver_v2.h"

#include "discovery/leaders.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace recomp_v2_emit {

namespace {

std::string fun_name(uint32_t addr) {
    char b[16];
    std::snprintf(b, sizeof b, "FUN_%08x", addr);
    return b;
}

// Decode one little-endian instruction word at `vram` (mirrors analyze.cpp).
bool read_word(const uint8_t* text_data, uint32_t text_size, uint32_t text_base,
               uint32_t vram, uint32_t* out) {
    if (vram < text_base) return false;
    const uint32_t off = vram - text_base;
    if (off + 4 > text_size) return false;
    *out = static_cast<uint32_t>(text_data[off]) |
           (static_cast<uint32_t>(text_data[off + 1]) << 8) |
           (static_cast<uint32_t>(text_data[off + 2]) << 16) |
           (static_cast<uint32_t>(text_data[off + 3]) << 24);
    return true;
}

// Build the Block list for [entry, end): maximal runs of `included` words,
// split at every address in `splits`. Every block's instructions are decoded
// from the .text image (analyze.cpp's exact decode). Runs whose words are
// excluded (unreached gap words inside a v2 extent = padding/data islands)
// are never emitted — matching the legacy engine, whose Ghidra blocks also
// cover real code only (design §7.3: "Unreached gaps ... never translated").
template <typename IncludedPred>
void build_blocks(Function& fn, uint32_t entry, uint32_t end,
                  const std::set<uint32_t>& splits, IncludedPred included,
                  const uint8_t* text_data, uint32_t text_size,
                  uint32_t text_base) {
    Block* cur = nullptr;
    for (uint32_t pc = entry; pc + 4 <= end; pc += 4) {
        if (!included(pc)) {
            cur = nullptr; // island: close the current run
            continue;
        }
        if (cur == nullptr || splits.count(pc)) {
            fn.blocks.emplace_back();
            cur = &fn.blocks.back();
            cur->start_address = pc;
            cur->end_address = pc;
        }
        uint32_t word = 0;
        if (!read_word(text_data, text_size, text_base, pc, &word)) {
            cur = nullptr; // out of image (defensive; extents live in .text)
            continue;
        }
        cur->instructions.emplace_back(word, pc);
        cur->end_address = pc + 4;
    }
    // Drop trailing empty blocks (defensive; build loop never leaves one).
    while (!fn.blocks.empty() && fn.blocks.back().instructions.empty())
        fn.blocks.pop_back();
}

} // namespace

BuildResult build_v2_functions(const recomp_v2::DiscoveryResult& result,
                               const recomp_v2::DiscoveryInputs& inputs,
                               const std::map<uint32_t, Function>& ghidra_functions,
                               const std::set<uint32_t>& pinned,
                               const uint8_t* text_data, uint32_t text_size,
                               uint32_t text_base) {
    BuildResult out;
    out.config.enabled = true;

    // ---- pass 0: v2 extents + containment helper ---------------------------
    for (const auto& kv : result.functions)
        out.config.extents[kv.first] = kv.second.end;
    auto container_of = [&](uint32_t a) -> const recomp_v2::DiscoveredFunction* {
        auto it = result.functions.upper_bound(a);
        if (it == result.functions.begin()) return nullptr;
        const auto& fn = std::prev(it)->second;
        return (a >= fn.entry && a < fn.end) ? &fn : nullptr;
    };

    // ---- pass 1: classify legacy-compat entries (Ghidra input, P3a only) ---
    // absorbed  -> alias forwarder + leader injection into the container
    // true gap  -> compat function (linear decode over the Ghidra extent)
    std::map<uint32_t, std::set<uint32_t>> extra_leaders; // container -> {alias}
    std::map<uint32_t, const Function*> compat;            // entry -> ghidra fn
    for (const auto& kv : ghidra_functions) {
        const uint32_t a = kv.first;
        if (result.functions.count(a)) continue; // matched: v2 emits it
        const auto* c = container_of(a);
        if (c != nullptr) {
            out.config.aliases.insert(a);
            out.config.alias_container[a] = c->entry;
            extra_leaders[c->entry].insert(a);
        } else {
            compat[a] = &kv.second;
        }
    }

    // ---- pass 1b: pin closure -----------------------------------------------
    // [RECOMP-P4-R2] THE PIN-CLOSURE CONTRACT (RECOMP_P4_REVIEW.md P4-R2):
    // pinning a container reproduces its LEGACY body (Ghidra blocks), whose
    // extent does not span the absorbed entries; those aliases would forward
    // into a switch with no matching case. Any alias whose container is
    // pinned is therefore DEMOTED and emitted as its own legacy function
    // (the legacy pipeline's separate body for that entry) — aliases follow
    // their container's pin. The function-NAME set is invariant under pins
    // (design §10 nothing-lost): every demoted entry keeps a real emitted
    // body. Before this fix the demotion marked the entries pinned but never
    // added their Function objects, silently DROPPING them from the model
    // (P4 review measured 9328->8991 functions on pin=0x001017c8); gate
    // RecompV2OverridePinAlias pins the corrected behavior.
    //
    // Pinning an absorbed ALIAS member directly is diagnosed and IGNORED:
    // its bytes live inside the container's v2 extent, so emitting a second
    // overlapping body without demoting the whole class would create two
    // implementations of the same code. Pin the container instead.
    std::set<uint32_t> pin_closure = pinned;
    {
        std::vector<uint32_t> alias_pins;
        for (uint32_t p : pin_closure)
            if (out.config.aliases.count(p)) alias_pins.push_back(p);
        for (uint32_t p : alias_pins) {
            std::fprintf(stderr,
                         "[v2-emit] pin 0x%08x is an absorbed alias of "
                         "container 0x%08x; alias-member pins are ignored — "
                         "pin the container to restore the legacy topology "
                         "for the whole class\n",
                         p, out.config.alias_container[p]);
            pin_closure.erase(p);
        }
        std::vector<uint32_t> demote;
        for (uint32_t a : out.config.aliases)
            if (pin_closure.count(out.config.alias_container[a]))
                demote.push_back(a);
        for (uint32_t a : demote) {
            const uint32_t c = out.config.alias_container[a];
            out.config.aliases.erase(a);
            extra_leaders[c].erase(a);
            out.config.alias_container.erase(a);
            auto g = ghidra_functions.find(a);
            if (g != ghidra_functions.end()) {
                pin_closure.insert(a);        // legacy chain emits it
                out.functions[a] = g->second; // the legacy body IS emitted
                std::fprintf(stderr,
                             "[v2-emit] pin closure: absorbed entry 0x%08x "
                             "emitted as its legacy body (container 0x%08x is "
                             "pinned)\n",
                             a, c);
            } else {
                // Unreachable by construction (aliases are built FROM
                // ghidra_functions keys); defensive dossier if it ever fires.
                std::fprintf(stderr,
                             "[v2-emit] WARNING: pin-closure entry 0x%08x has "
                             "no legacy function input; entry dropped\n", a);
            }
        }
    }
    out.config.pinned = pin_closure;

    // ---- pass 2: global sweep leaders (compat-function block splits) --------
    // The engine's own D1 primitive over the whole image; splitting at MORE
    // leaders is always safe (labels + cases are additive).
    std::set<uint32_t> sweep_leaders;
    if (!compat.empty()) {
        std::set<uint32_t> entries;
        for (const auto& kv : result.functions) entries.insert(kv.first);
        for (const auto& kv : compat) entries.insert(kv.first);
        sweep_leaders = recomp_v2::analyze_leaders(inputs.text, entries).leaders;
    }

    // ---- pass 3: emit-model functions ---------------------------------------
    // (a) discovered v2 functions (pinned ones swap to their Ghidra object)
    for (const auto& kv : result.functions) {
        const recomp_v2::DiscoveredFunction& d = kv.second;
        if (out.config.pinned.count(d.entry)) {
            auto g = ghidra_functions.find(d.entry);
            if (g != ghidra_functions.end()) {
                out.functions[d.entry] = g->second; // the legacy body source
                continue;
            }
            std::fprintf(stderr,
                         "[v2-emit] WARNING: pin 0x%08x has no legacy function "
                         "input; emitting the v2 body instead\n",
                         d.entry);
            out.config.pinned.erase(d.entry);
        }
        Function fn;
        fn.base_address = d.entry;
        fn.size = d.end - d.entry;
        fn.name = (d.entry == inputs.elf_entry) ? "entry" : fun_name(d.entry);
        std::set<uint32_t> splits = d.leaders; // leader-complete by contract
        auto el = extra_leaders.find(d.entry);
        if (el != extra_leaders.end())
            splits.insert(el->second.begin(), el->second.end());
        build_blocks(
            fn, d.entry, d.end, splits,
            [&](uint32_t pc) { return result.unreached_gaps.count(pc) == 0; },
            text_data, text_size, text_base);
        // jr-site table emission config (CONVENTIONS D3).
        for (const auto& t : d.tables) {
            V2JumpTableEmission e;
            e.internal_targets = t.internal_targets;
            e.external_targets = t.external_targets;
            out.config.tables[t.site_pc] = e;
        }
        out.functions[d.entry] = std::move(fn);
    }

    // (b) alias forwarders (bodies emitted by recompile_function's alias path)
    for (uint32_t a : out.config.aliases) {
        const auto g = ghidra_functions.find(a);
        Function fn;
        fn.base_address = a;
        fn.size = (g != ghidra_functions.end()) ? g->second.size : 8u;
        fn.name = fun_name(a);
        out.functions[a] = std::move(fn); // no blocks: alias marker drives it
    }

    // (c) compat true-gap functions: v2-auto emission over the legacy extent,
    // blocks split at the global sweep's leaders (leader-complete within the
    // extent: the sweep records every branch/jump target program-wide).
    for (const auto& kv : compat) {
        const uint32_t a = kv.first;
        const Function* g = kv.second;
        Function fn;
        fn.base_address = a;
        fn.size = g->size;
        fn.name = fun_name(a);
        const uint32_t end = a + g->size;
        std::set<uint32_t> splits;
        for (auto it = sweep_leaders.lower_bound(a);
             it != sweep_leaders.end() && *it < end; ++it)
            splits.insert(*it);
        splits.insert(a);
        build_blocks(fn, a, end, splits, [](uint32_t) { return true; },
                     text_data, text_size, text_base);
        out.config.extents[a] = end;
        out.functions[a] = std::move(fn);
    }

    // ---- pass 4: alias container names (after all names are final) ----------
    for (uint32_t a : out.config.aliases) {
        const uint32_t c = out.config.alias_container[a];
        auto it = out.functions.find(c);
        out.config.alias_container_name[a] =
            (it != out.functions.end()) ? it->second.name : fun_name(c);
    }

    return out;
}

} // namespace recomp_v2_emit
