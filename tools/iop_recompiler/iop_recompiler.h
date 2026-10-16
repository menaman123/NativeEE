#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <string>

// Forward-declare rabbitizer instruction class for R3000A (standard MIPS CPU).
namespace rabbitizer {
    class InstructionCpu;
}

// Describes a single function extracted from the IOP ELF.
struct IopFunction {
    uint32_t start_addr; // First instruction address
    uint32_t end_addr;   // One past the last instruction address
    std::string name;    // Human-readable name (e.g. "iop_func_00008D28")
};

// Static recompiler that translates R3000A (MIPS I) IOP code to C++.
// Adapted from the EE recompiler but targeting 32-bit IopContext instead
// of the 128-bit EE CpuContext.  No FPU, COP2, MMI, or 64/128-bit ops.
class IopRecompiler {
public:
    // Generic-module options (all default to the frozen tool's behaviour).
    struct Options {
        bool legacy_title_stubs = false;   // the first title's probe arms / RPC thunk
        std::string ns;                    // wrap the TU in namespace <ns>; indirect calls via iop_stubs::iop_resolve
        bool import_call = false;          // imports -> iop_stubs::import_call("lib", idx, ctx)
        uint32_t base = 0;                 // the relocation base (recorded in the TU)
        uint32_t entry = 0;                // the relocated module entry (recorded in the TU)
        uint32_t load_extent = 0;          // module-relative end of the allocated sections
        std::string input_name;            // the input file name (comment only)
    };
    // What the emission saw (the tool's summary / fail-loudly report).
    struct Census {
        uint32_t import_calls = 0;
        uint32_t unimplemented = 0;
        std::map<std::string, uint32_t> unimplemented_opcodes;
        uint32_t jalr_sites = 0;
        uint32_t jr_switch_sites = 0;
        uint32_t tail_j_sites = 0;
        uint32_t unresolved_jal = 0;
        uint32_t branch_in_delay_slot = 0;
    };

    // Construct with a map of address → IopFunction (from Ghidra or built-in analysis).
    explicit IopRecompiler(const std::map<uint32_t, IopFunction>& functions);

    // Set the import stub map (stub address → "library_funcN" name).
    // JAL targets that land in this map emit HLE stub calls instead of
    // recompiled function calls.
    void set_import_stubs(const std::map<uint32_t, std::string>& stubs);

    // Set the .text section data so recompile_function can read instruction bytes.
    void set_text_data(const uint8_t* data, uint32_t size, uint32_t base_addr);

    void set_options(const Options& opts) { m_opts = opts; }
    const Census& census() const { return m_census; }

    // Main entry point — writes the recompiled header and cpp files.
    bool recompile_to_files(const std::string& output_header,
                            const std::string& output_cpp);

private:
    std::map<uint32_t, IopFunction> m_functions;
    std::map<uint32_t, std::string> m_import_stubs; // stub addr → stub name
    Options m_opts;
    Census m_census;

    // .text section data for reading instruction bytes
    const uint8_t* m_text_data = nullptr;
    uint32_t m_text_size = 0;
    uint32_t m_text_base = 0;

    void write_header_file(std::ofstream& file);
    void write_cpp_file(std::ofstream& file,
                        const std::string& header_filename);
    void recompile_function(const IopFunction& func, std::ofstream& file);
    void translate_instruction(const rabbitizer::InstructionCpu& instr,
                               uint32_t pc, std::ofstream& file);

    // Map import stub names (e.g. "cdvdman_0") to iop_stubs:: C++ function names.
    std::string resolve_stub_name(const std::string& raw_name) const;
    // The call expression for an import stub ("lib_idx"): the named forwarder, or
    // iop_stubs::import_call("lib", idx, iop_ctx) under Options::import_call.
    std::string import_call_expr(const std::string& raw_name);
    // The C++ expression that runs the guest function at a computed address
    // (`target_expr`): the TU's own map (legacy) or the shared iop_resolve hook.
    void emit_indirect_call(std::ofstream& file, const std::string& indent, const std::string& target_expr,
                            const std::string& on_missing, const std::string& after_call);
};

