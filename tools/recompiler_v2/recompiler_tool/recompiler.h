#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include "EEAnalyze/Function.h" // Include your Function analysis class
#include "emit/v2_emission.h"   // [RECOMP-P3a] v2 emission config
#include "map"
#include <set>  // [W16R1] m_body_skipped
#include "instructions/InstructionR5900.hpp"
#include <fstream>
// Forward-declare the rabbitizer C++ instruction class to avoid including the full header.
// This is a best practice for header files to improve compilation times.

extern std::ofstream log_file;
namespace rabbitizer {
    class InstructionR5900;
}

class Recompiler {
public:
    Recompiler(const std::map<uint32_t, Function>& functions);
    // Overload that also receives the raw .text image so the generator can
    // decode an instruction word at an arbitrary VA (needed to recover a
    // branch-delay-slot instruction that Ghidra's block split left out of
    // every basic block - see generate_block_code's out-of-range path).
    Recompiler(const std::map<uint32_t, Function>& functions,
               const uint8_t* text_data, uint32_t text_size, uint32_t text_base);
    // [R126-SHARD] shard_count == 0 (default) emits the classic single-TU
    // recompiled.cpp (byte-identical to the pre-shard tool). shard_count > 0
    // emits `output_cpp` as a bodies-free COMMON TU (file-scope infra +
    // find_containing_function + initialize_recompiled_functions) plus
    // recompiled_shard_000.cpp .. recompiled_shard_<N-1>.cpp next to it, each a
    // self-contained TU holding a contiguous address-ordered slice of the
    // function bodies (balanced by per-function instruction count) so the hot
    // generated code can be compiled at -O2 in parallel (R116 plan).
    bool recompile_to_files(const std::string& output_header, const std::string& output_cpp,
                            int shard_count = 0);

    // [R137-DIRECTCALL] Gate for dense-id direct-call JAL emission. Default ON.
    // When ON, a JAL whose CONSTANT target is a function-table entry point is
    // emitted as `recomp_call(<dense_id>, <target>, ctx)` - an array-indexed
    // dispatch through a pointer INTO recompiled_functions (no map walk).
    // When OFF, the classic `recompiled_functions.count()/operator[]` emission
    // is produced unchanged (A/B fallback). JALR, non-table targets, and every
    // manual/raw-string body keep the classic dispatch in BOTH modes.
    void set_direct_calls(bool enabled) { m_direct_calls = enabled; }

    // [INIT-PARTS] The registration table of initialize_recompiled_functions()
    // is ~8,800 entries x 2 lambdas; as ONE function body GCC 13 needs > 9 GiB
    // to compile it at -O0 (B1 Linux lane, CHARTER2_B1.md). parts > 1 emits the
    // table as `parts` file-static `init_recompiled_functions_part_NNN()`
    // helpers of ceil(N/parts) consecutive registrations each (address order
    // preserved), which initialize_recompiled_functions() calls in order before
    // its post-table blocks, and the default registrations name the body
    // directly (`= &FUN_...;` instead of one stateless-lambda closure type per
    // entry -- the closure types, not the function length, are what GCC keeps
    // > 10 GiB of). parts <= 1 (default) keeps the classic single body with the
    // lambda form, byte-identical to the pre-knob tool. Registration ORDER, the
    // hook classification and the installed callables' behaviour are the same
    // in both forms.
    void set_init_parts(int parts) { m_init_parts = parts; }
    // The first title's hand-written stub/diagnostic blocks in the init tail (hardcoded
    // retail addresses: tiny vtable stubs Ghidra missed, the [PARTICLE-DIAG] / [2C75E8-DUMP]
    // wrappers). Title data, not a mechanism: emitted only when the title's build asks
    // (--legacy-title-stubs). Default OFF -- a second title must not inherit another
    // title's addresses (one of them collides with a real function start elsewhere).
    void set_legacy_title_stubs(bool enabled) { m_legacy_title_stubs = enabled; }
    // The first title's address-keyed emission overrides (diagnostic wrappers, hand-written
    // bodies, HLE hooks keyed on `func.base_address == <retail address>`) are the same
    // title data as the stub blocks: a second title's function at that address must get
    // the default emission. Every such site tests through this predicate.
    bool legacy_title_addr(const Function& func, uint32_t address) const {
        return m_legacy_title_stubs && func.base_address == address;
    }
    // Back-edge yield checks (--yield-backedges): every in-function branch whose
    // target lies at or before the branch's own pc (a loop) is preceded by
    //   if (--g_ee_backedge_budget == 0) ps2r_ee_yield_point(ctx);
    // -- the engine's in-line scheduling point for a guest loop that reaches no
    // kernel call (declared by the ABI forwarder host_app/syscalls.h; the engine
    // owns the budget and what the yield does). Default OFF: no emission change.
    void set_yield_backedges(bool enabled) { m_yield_backedges = enabled; }

    // [RECOMP-P3a] v2 emission mode (RECOMP_P0_DESIGN.md §6-§8). When enabled,
    // unpinned functions take the general emission path with the v2 deltas:
    // the body-time special-case chain is bypassed, jr sites emit ELF-read
    // jump-table target sets (D3) instead of the all-leaders switch, an
    // internal literal reaching the fcf path is a generation-time hard error
    // (D2), and alias entries emit forwarder bodies. Legacy mode (default,
    // m_v2.enabled == false) is byte-identical to the pre-P3a emitter — the
    // G-DET gate enforces that.
    void set_v2_emission(const V2EmissionConfig& cfg) { m_v2 = cfg; }


    // --- Differential-semantics snippet mode (tests/recomp_semantics) -------
    // One test case: a stable name + a raw 32-bit instruction word.
    struct SnippetCase {
        std::string name;
        uint32_t word;
    };
    // Emits, for each case, the UNCHANGED translate_instruction output wrapped
    // in `void recomp_snip_<name>(CpuContext& ctx) { ... }`, plus a
    // `g_recomp_snippets[]` registry array, into `output_cpp`. Purely additive:
    // reuses the per-instruction emitter verbatim (zero emission-logic
    // changes), so the test exercises the recompiler's REAL generated C++.
    // Synthetic vrams are 0x00100000 + 8*i (avoids the 0x1cd768 probe).
    bool emit_semantics_snippets(const std::vector<SnippetCase>& cases,
                                 const std::string& output_cpp);

private:
    std::map<uint32_t, Function> m_functions;

    // [RECOMP-P3a] v2 emission config (inert by default: enabled == false).
    V2EmissionConfig m_v2;
    // True while recompile_function is emitting a v2-auto (unpinned,
    // non-alias) body; drives the jr-table delta and the D2 hard error.
    bool m_v2_auto_current = false;
    // [RECOMP-P3a] v2 deltas (translate/control_flow_v2.cpp).
    void emit_v2_jr_table_switch(uint32_t site_pc, const Function& func,
                                 std::ofstream& file);
    void emit_v2_alias_body(const Function& func, std::ofstream& file);

    // [R137-DIRECTCALL] Dense function ids: address-sorted index into the
    // generated g_recomp_fn_addrs / g_recomp_fn_cache tables. Built once in
    // the constructor from m_functions (std::map iteration order IS address
    // order, so ids are stable across header/common-TU/shard emission).
    std::map<uint32_t, uint32_t> m_addr_to_id;
    bool m_direct_calls = true;
    int m_init_parts = 0;
    bool m_legacy_title_stubs = false;
    bool m_yield_backedges = false;
    // The pc of the branch translate_control_flow is emitting (0 outside it):
    // emit_branch_target compares the target against it to recognise a back-edge.
    uint32_t m_branch_from_pc = 0;
    // [W16R1][FIX-E1] Function addrs whose body emission was SKIPPED
    // (HLE-replaced): their FUN_ symbol does not exist, so the raw table
    // emits nullptr for them and the hook-list recompute force-hooks any
    // null-raw id (they are all explicit registrations anyway).
    std::set<uint32_t> m_body_skipped;
    void build_addr_to_id();

    // Raw .text section image (same buffer analyze.cpp decoded blocks from).
    // Used only to fetch an out-of-block delay-slot word at a known VA.
    const uint8_t* m_text_data = nullptr;
    uint32_t m_text_size = 0;
    uint32_t m_text_base = 0;

    // Reads the 32-bit (little-endian) instruction word at virtual address
    // `vram` from the .text image. Returns false if no image is available or
    // the address is out of bounds. Mirrors analyze.cpp's decode.
    bool fetch_program_word(uint32_t vram, uint32_t& word_out) const;

    void write_header_file(std::ofstream& file);
    void write_cpp_file(std::ofstream& file, const std::string& output_header_filename);
    // [R126-SHARD] write_cpp_file split into three mechanical pieces so the
    // sharded emitter can reuse them verbatim. Calling includes+infra+bodies+
    // tail in sequence reproduces the classic single TU byte-for-byte.
    void write_cpp_includes(std::ofstream& file, const std::string& output_header_filename);
    void write_cpp_infra(std::ofstream& file);
    void write_cpp_tail(std::ofstream& file);
    // [W16R1][FIX-E2] Post-pass over a generated file: rewrite the manual
    // raw-string bodies' classic map dispatch
    //   if (recompiled_functions.count(0xT)) { recompiled_functions[0xT](ctx, 0xT); }
    //   else { ctx.cpuRegs.pc = 0xT; }
    // into the exact static-JAL emission `recomp_call(<id>u, 0xTu, ctx);`
    // (dense id resolved from m_addr_to_id). Only exact-shape matches with
    // a table-entry T are rewritten; residuals are reported to stderr.
    // Returns rewritten-site count, -1 on I/O failure.
    int rewrite_manual_dispatch_file(const std::string& path);
    // [W16R1][FIX-E1] Post-pass over the generated common TU: scan
    // initialize_recompiled_functions for explicit (non-default-form)
    // `recompiled_functions[0x...] = ` assignments -- the generator-known
    // manual-override / HLE-hook registrations, INCLUDING the post-loop
    // re-wrap blocks -- and inject the sorted hook-address list into the
    // [W16R1-E1-HOOKLIST] placeholder. Fails CLOSED: if the placeholder is
    // missing or no assignments parse, hooked[] stays all-true (classic
    // dispatch verbatim). Returns hook count, -1 on I/O failure.
    int inject_gen_hook_list(const std::string& common_tu_path);
    bool write_sharded_cpp_files(const std::string& output_cpp,
                                 const std::string& output_header_filename,
                                 int shard_count);
    void recompile_function(const Function& func, std::ofstream& file);
    void translate_instruction(const rabbitizer::InstructionR5900& instr, std::ofstream& file);
    void translate_branch_or_jump(const rabbitizer::InstructionR5900& instr, std::ofstream& file);
    void translate_branch_with_goto(const rabbitizer::InstructionR5900& instr, 
                                           const Function& func,
                                           size_t current_block_idx,
                                           std::ofstream& file);

    // Helper function to identify instructions that have a delay slot.
    bool has_delay_slot(const rabbitizer::InstructionR5900& instr) const;
    void generate_block_code(const Function& func, const Block& block, size_t block_idx, std::ofstream& file);
    void translate_control_flow(const rabbitizer::InstructionR5900& instr, const Function& func, const Block& block, size_t current_block_idx, size_t current_instr_idx, std::ofstream& file);
    void emit_branch_target(uint32_t target_addr, const Function& func, std::ofstream& file, const std::string& indent);
};
