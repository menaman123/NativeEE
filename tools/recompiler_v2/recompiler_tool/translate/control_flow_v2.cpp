//
// control_flow_v2.cpp — [RECOMP-P3a] the v2-only control-flow emission deltas,
// implemented as Recompiler methods in their own TU (design §6 translate/).
//
// Everything here EMITS TEXT ONLY; instruction semantics stay in
// recompiler.cpp's translate_instruction / translate_control_flow (reused
// verbatim by both engines — the brief's no-new-semantics mandate).
//
// Delta 1 (CONVENTIONS D3/D4) — the jr (rs != $ra) dispatch:
//   * site classified as a jump table  => `switch (target)` over EXACTLY the
//     ELF-recovered target set: internal targets goto their block label
//     (every one is a discovery leader => a label by construction), external
//     targets tail-dispatch through the dense-id cache; the runtime-
//     dispatcher fallback stays reachable as the default (D4).
//   * site NOT classified              => no switch at all; the fallback is
//     the entire emission (D3's "only the runtime-dispatcher fallback").
// The legacy engine's all-block-leaders switch (the class-B spurious/missing
// case source, design §4.2) is never emitted under v2.
//
// Delta 2 — alias forwarder bodies for absorbed legacy entries (design §10
// nothing-lost rule): one direct call into the container at the alias's
// mid-entry label. Direct symbol call (not map dispatch) preserves legacy
// hook semantics: a hook on the CONTAINER address does not intercept entries
// through the absorbed address, exactly as two separate legacy functions
// behaved; a hook on the ALIAS address value-replaces the alias's own map
// entry as usual (contract surface #4).
//
#include "../recompiler.h"

#include <iomanip>
#include <stdexcept>

// Emit the v2 jump-table `switch (target)` cases for a classified jr site.
// Returns without emitting anything when the site has no recovered table.
// The caller emits the shared fcf fallback right after (both engines share
// that text verbatim).
void Recompiler::emit_v2_jr_table_switch(uint32_t site_pc, const Function& func,
                                         std::ofstream& file) {
    auto it = m_v2.tables.find(site_pc);
    if (it == m_v2.tables.end()) {
        file << "        // [RECOMP-P3a] jr site 0x" << std::hex << site_pc
             << " not classified as a jump table: runtime-dispatcher fallback"
                " only (CONVENTIONS D3)\n";
        file << std::setfill(' ');
        return;
    }
    const V2JumpTableEmission& t = it->second;
    file << "        // [RECOMP-P3a] ELF-recovered jump table @0x" << std::hex
         << site_pc << ": " << std::dec << t.internal_targets.size()
         << " internal / " << t.external_targets.size()
         << " external target(s) (CONVENTIONS D3)\n";
    file << "        switch (target) {\n";
    for (uint32_t tgt : t.internal_targets) {
        bool found = false;
        for (size_t i = 0; i < func.blocks.size(); ++i) {
            if (func.blocks[i].start_address == tgt) {
                file << "            case 0x" << std::hex << tgt << ": goto Label_"
                     << std::setw(4) << std::setfill('0') << std::dec << i
                     << ";\n";
                found = true;
                break;
            }
        }
        if (!found) {
            // Leader-complete discovery guarantees a label for every internal
            // target; a miss is an engine/driver bug — fail the build, never
            // emit the class-A shape (CONVENTIONS D1/D2).
            char msg[160];
            std::snprintf(msg, sizeof msg,
                          "[RECOMP-P3a][D1] internal jump-table target 0x%08x "
                          "of site 0x%08x has no block label in %s",
                          tgt, site_pc, func.name.c_str());
            throw std::runtime_error(msg);
        }
    }
    for (uint32_t tgt : t.external_targets) {
        auto dc = m_addr_to_id.find(tgt);
        if (dc != m_addr_to_id.end()) {
            file << "            case 0x" << std::hex << tgt << ": recomp_call("
                 << std::dec << dc->second << "u, 0x" << std::hex << tgt
                 << "u, ctx); return; // [RECOMP-P3a] external table target\n";
        }
        // No dense id (target not an emitted function): leave it to the
        // default fcf fallback — the always-correct universal path (D4).
    }
    file << "        }\n";
    file << std::setfill(' ');
}

// Emit an alias forwarder body for an absorbed legacy entry.
void Recompiler::emit_v2_alias_body(const Function& func, std::ofstream& file) {
    const uint32_t a = func.base_address;
    const uint32_t c = m_v2.alias_container.at(a);
    const std::string& cname = m_v2.alias_container_name.at(a);
    file << "// Function: " << func.name << " at 0x" << std::hex << a << "\n";
    file << "// [RECOMP-P3a] alias: legacy entry absorbed inside " << cname
         << " [0x" << std::hex << c << ", 0x" << m_v2.extents.at(c)
         << ") — forwards to the container's mid-entry label (a discovery\n";
    file << "// leader injected for exactly this entry; design §10 nothing-lost"
            " rule).\n";
    file << "void " << func.name << "(CpuContext& ctx, uint32_t addr) {\n";
    file << "    " << cname << "(ctx, addr ? addr : 0x" << std::hex << a
         << "u);\n";
    file << "}\n\n";
}
