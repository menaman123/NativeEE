#include "iop_recompiler.h"

#include "rabbitizer.hpp"
#include "instructions/InstructionCpu.hpp"

#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

// ---------------------------------------------------------------------------
// Helper: return the C++ expression for an IOP GPR access.
// IOP is 32-bit only — no 128-bit union like the EE.
// gpr[0] is hardwired zero; writes are discarded by iop_fix_zero().
// ---------------------------------------------------------------------------
static std::string gpr(uint8_t reg) {
    return "iop_ctx.gpr[" + std::to_string(reg) + "]";
}

// Format a hex literal for an unsigned value.
static std::string hex(uint32_t val) {
    std::stringstream ss;
    ss << "0x" << std::hex << val;
    return ss.str();
}

// Format a signed immediate (sign-extended from 16-bit).
static std::string simm(int16_t imm) {
    std::stringstream ss;
    if (imm < 0) {
        ss << "(-" << "0x" << std::hex << static_cast<uint16_t>(-imm) << ")";
    } else {
        ss << "0x" << std::hex << static_cast<uint16_t>(imm);
    }
    return ss.str();
}

// Format an address label name.
static std::string label(uint32_t addr) {
    std::stringstream ss;
    ss << "label_" << std::hex << std::setfill('0') << std::setw(8) << addr;
    return ss.str();
}

// Read a little-endian 32-bit word from a byte buffer.
static uint32_t read_u32_le(const uint8_t* data) {
    return static_cast<uint32_t>(data[0])
         | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16)
         | (static_cast<uint32_t>(data[3]) << 24);
}

// ---------------------------------------------------------------------------
// Import stub name → iop_stubs:: C++ function name mapping.
// Keys are "library_kernelindex" as extracted from the import table delay
// slot words.  Values are the C++ stub function names in iop_stubs::.
// Based on decompilation analysis of the actual STREAM.IRX import tables.
// ---------------------------------------------------------------------------
static const std::map<std::string, std::string> g_stub_name_map = {
    // sysmem v1.2 (4 stubs)
    {"sysmem_4",  "iop_stubs::AllocSysMemory"},
    {"sysmem_5",  "iop_stubs::FreeSysMemory"},
    {"sysmem_7",  "iop_stubs::QueryMaxFreeMemSize"},
    {"sysmem_14", "iop_stubs::Kprintf"},

    // intrman v1.2 (2 stubs)
    {"intrman_17", "iop_stubs::CpuSuspendIntr"},
    {"intrman_18", "iop_stubs::CpuResumeIntr"},

    // ioman v1.4 (4 stubs)
    {"ioman_4", "iop_stubs::iop_open"},
    {"ioman_5", "iop_stubs::iop_close"},
    {"ioman_6", "iop_stubs::iop_read"},
    {"ioman_8", "iop_stubs::iop_lseek"},

    // sifcmd v1.1 (6 stubs)
    {"sifcmd_14", "iop_stubs::sceSifInitRpc"},
    {"sifcmd_15", "iop_stubs::sceSifBindRpc"},
    {"sifcmd_16", "iop_stubs::sceSifCallRpc"},
    {"sifcmd_17", "iop_stubs::sceSifRegisterRpc"},
    {"sifcmd_19", "iop_stubs::sceSifSetRpcQueue"},
    {"sifcmd_22", "iop_stubs::sceSifRpcLoop"},

    // sifman v1.1 (4 stubs)
    {"sifman_5",  "iop_stubs::sceSifInit"},
    {"sifman_7",  "iop_stubs::sceSifSetDma"},
    {"sifman_8",  "iop_stubs::sceSifDmaStat"},
    {"sifman_29", "iop_stubs::sceSifCheckInit"},

    // stdio v1.3 (1 stub)
    {"stdio_4", "iop_stubs::stdio_printf"},

    // sysclib v1.4 (7 stubs)
    // [S44] Ordinal map corrected against the real sysclib export table
    // (pcsx2-src/pcsx2/IopModuleNames.cpp:613 — 10=memchr 11=memcmp
    // 12=memcpy 13=memmove 14=memset 18=prnt 20=strcat 21=strchr 22=strcmp
    // 23=strcpy 25=index 27=strlen 29=strncmp 32=strrchr 34=strstr) and the
    // module's own linked import thunks (live PCSX2 IOP RAM 0x9b074-0x9b0a8:
    // ordinals {12,14,20,23,27,29,34}). The old map bound every thunk to the
    // name of ordinal-2 (12→memcmp, 14→memcpy, 20→prnt, 23→strchr,
    // 27→index, 29→strlen, 34→strrchr) — the same +2 counting defect class
    // as the S43 thbase R2 mismap. Consequence: STREAM.IRX's CD-job
    // executor FUN_00001930 assembles each SPU-RAM sample upload as
    // head-slice memcpy(ring, scratch+off, n) [pc 0x1a8c] + bulk sector
    // read + tail-slice memcpy — and ordinal 12 ran MEMCMP (writes
    // nothing), leaving stale ring bytes in the first/last partial sector
    // of EVERY uploaded sample: the wrong-sound-per-action defect (S44).
    {"sysclib_12", "iop_stubs::iop_memcpy"},
    {"sysclib_14", "iop_stubs::iop_memset"},
    {"sysclib_20", "iop_stubs::iop_strcat"},
    {"sysclib_23", "iop_stubs::iop_strcpy"},
    {"sysclib_27", "iop_stubs::iop_strlen"},
    {"sysclib_29", "iop_stubs::iop_strncmp"},
    {"sysclib_34", "iop_stubs::iop_strstr"},

    // thbase v1.2 (10 stubs)
    // [S43] Ordinal map corrected against the real thbase export table
    // (ordinal 21 is CheckThreadStack, which shifts everything after 20):
    //   20=GetThreadId 24=SleepThread 25=WakeupThread 33=DelayThread.
    // The old map (20→SleepThread, 24→DelayThread, 25→GetSystemTime,
    // 33→GetThreadId) silently broke STREAM.IRX's dispatcher↔worker
    // exclusion protocol (dispatcher preamble SleepThread at 0x8d6c, worker
    // wake FUN_000025dc WakeupThread([0x13328])) — the S42 boot-wedge race
    // class — and turned every DelayThread poll loop into a hot spin.
    {"thbase_4",  "iop_stubs::CreateThread"},
    {"thbase_5",  "iop_stubs::DeleteThread"},
    {"thbase_6",  "iop_stubs::StartThread"},
    {"thbase_10", "iop_stubs::TerminateThread"},
    {"thbase_14", "iop_stubs::ChangeThreadPriority"},
    {"thbase_20", "iop_stubs::GetThreadId"},
    {"thbase_24", "iop_stubs::SleepThread"},
    {"thbase_25", "iop_stubs::WakeupThread"},
    {"thbase_33", "iop_stubs::DelayThread"},
    {"thbase_39", "iop_stubs::USec2SysClock"},

    // thsemap v1.1 (5 stubs)
    {"thsemap_4", "iop_stubs::CreateSema"},
    {"thsemap_5", "iop_stubs::DeleteSema"},
    {"thsemap_6", "iop_stubs::SignalSema"},
    {"thsemap_7", "iop_stubs::iSignalSema"},
    {"thsemap_8", "iop_stubs::WaitSema"},

    // timrman v1.3 (5 stubs)
    // [S43] 22=SetupHardTimer, 23=StartHardTimer (the old 22→SetOverflow/
    // 23→Setup mislabel hid the START of STREAM.IRX's 60Hz heartbeat timer:
    // cmd-12 init FUN_000012c0/FUN_00001348 arm handler 0x136c at 16.666ms —
    // the tick that sets [0x12f1c]=1 and signals the CD worker semaphore).
    {"timrman_4",  "iop_stubs::AllocHardTimer"},
    {"timrman_6",  "iop_stubs::FreeHardTimer"},
    {"timrman_20", "iop_stubs::SetTimerHandler"},
    {"timrman_22", "iop_stubs::SetupHardTimer"},
    {"timrman_23", "iop_stubs::StartHardTimer"},

    // libsd v1.5 (16 stubs)
    {"libsd_4",  "iop_stubs::libsd_stub_0"},
    {"libsd_5",  "iop_stubs::libsd_stub_1"},
    {"libsd_6",  "iop_stubs::libsd_stub_2"},
    {"libsd_7",  "iop_stubs::libsd_stub_3"},
    {"libsd_8",  "iop_stubs::libsd_stub_4"},
    {"libsd_9",  "iop_stubs::libsd_stub_5"},
    {"libsd_10", "iop_stubs::libsd_stub_6"},
    {"libsd_11", "iop_stubs::libsd_stub_7"},
    {"libsd_15", "iop_stubs::libsd_stub_8"},
    {"libsd_17", "iop_stubs::libsd_stub_9"},
    {"libsd_18", "iop_stubs::libsd_stub_10"},
    {"libsd_19", "iop_stubs::libsd_stub_11"},
    {"libsd_20", "iop_stubs::libsd_stub_12"},
    {"libsd_23", "iop_stubs::libsd_stub_13"},
    {"libsd_25", "iop_stubs::libsd_stub_14"},
    {"libsd_26", "iop_stubs::libsd_stub_15"},

    // cdvdman v1.1 (9 stubs)
    {"cdvdman_4",  "iop_stubs::sceCdInit"},
    {"cdvdman_6",  "iop_stubs::sceCdRead"},
    {"cdvdman_8",  "iop_stubs::sceCdGetError"},
    {"cdvdman_10", "iop_stubs::sceCdSearchFile"},
    {"cdvdman_11", "iop_stubs::sceCdSync"},
    {"cdvdman_12", "iop_stubs::sceCdGetDiskType"},
    {"cdvdman_13", "iop_stubs::sceCdDiskReady"},
    {"cdvdman_37", "iop_stubs::sceCdCallback"},
    {"cdvdman_75", "iop_stubs::sceCdMmode"},
};

// ---------------------------------------------------------------------------
// Constructor and setters
// ---------------------------------------------------------------------------
IopRecompiler::IopRecompiler(const std::map<uint32_t, IopFunction>& functions)
    : m_functions(functions) {}

void IopRecompiler::set_import_stubs(
        const std::map<uint32_t, std::string>& stubs) {
    m_import_stubs = stubs;
}

void IopRecompiler::set_text_data(const uint8_t* data, uint32_t size,
                                   uint32_t base_addr) {
    m_text_data = data;
    m_text_size = size;
    m_text_base = base_addr;
}

std::string IopRecompiler::resolve_stub_name(const std::string& raw_name) const {
    auto it = g_stub_name_map.find(raw_name);
    if (it != g_stub_name_map.end()) {
        return it->second;
    }
    // Fallback: generate a generic stub call with a warning comment
    return "/* UNKNOWN STUB: " + raw_name + " */ iop_stubs::stdio_printf";
}

std::string IopRecompiler::import_call_expr(const std::string& raw_name) {
    m_census.import_calls++;
    if (!m_opts.import_call) return resolve_stub_name(raw_name) + "(iop_ctx)";
    // "lib_idx" -> import_call("lib", idx, iop_ctx): the host's library table binds it.
    const size_t us = raw_name.rfind('_');
    const std::string lib = us == std::string::npos ? raw_name : raw_name.substr(0, us);
    const std::string idx = us == std::string::npos ? "0" : raw_name.substr(us + 1);
    return "iop_stubs::import_call(\"" + lib + "\", " + idx + ", iop_ctx)";
}

void IopRecompiler::emit_indirect_call(std::ofstream& file, const std::string& indent,
                                       const std::string& target_expr, const std::string& on_missing,
                                       const std::string& after_call) {
    if (m_opts.ns.empty()) {
        file << indent << "if (iop_recompiled_functions.count(" << target_expr << ")) {\n";
        file << indent << "    iop_recompiled_functions[" << target_expr << "](iop_ctx);\n";
        if (!after_call.empty()) file << indent << "    " << after_call << "\n";
        file << indent << "}";
        if (!on_missing.empty()) {
            file << " else {\n";
            file << indent << "    " << on_missing << "\n";
            file << indent << "}";
        }
        file << "\n";
        return;
    }
    // Namespaced module: the shared resolver (every hosted module + the HLE
    // libraries' guest-visible entries) answers a computed address.
    file << indent << "if (const auto* _fn = iop_stubs::iop_resolve(" << target_expr << ")) {\n";
    file << indent << "    (*_fn)(iop_ctx);\n";
    if (!after_call.empty()) file << indent << "    " << after_call << "\n";
    file << indent << "}";
    if (!on_missing.empty()) {
        file << " else {\n";
        file << indent << "    " << on_missing << "\n";
        file << indent << "}";
    }
    file << "\n";
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------
bool IopRecompiler::recompile_to_files(const std::string& output_header,
                                        const std::string& output_cpp) {
    if (!m_text_data || m_text_size == 0) {
        std::cerr << "[IOP-RECOMP] No .text data set — cannot recompile.\n";
        return false;
    }

    std::ofstream header_file(output_header, std::ios::binary);   // LF on every host: the text is sha256-pinned
    if (!header_file.is_open()) {
        std::cerr << "[IOP-RECOMP] Could not open header for writing: "
                  << output_header << std::endl;
        return false;
    }
    write_header_file(header_file);
    header_file.close();
    std::cout << "[IOP-RECOMP] Generated header: " << output_header << std::endl;

    std::ofstream cpp_file(output_cpp, std::ios::binary);   // LF on every host: the text is sha256-pinned
    if (!cpp_file.is_open()) {
        std::cerr << "[IOP-RECOMP] Could not open cpp for writing: "
                  << output_cpp << std::endl;
        return false;
    }
    write_cpp_file(cpp_file,
                   std::filesystem::path(output_header).filename().string());
    cpp_file.close();
    std::cout << "[IOP-RECOMP] Generated cpp:    " << output_cpp << std::endl;

    return true;
}

// ---------------------------------------------------------------------------
// Header file generation
// ---------------------------------------------------------------------------
void IopRecompiler::write_header_file(std::ofstream& file) {
    file << "#pragma once\n\n";
    file << "#include \"../host_app/iop_context.h\"\n";
    file << "#include \"../host_app/iop_memory.h\"\n";
    file << "#include \"../host_app/iop_stubs.h\"\n\n";
    file << "#include <cstdint>\n";
    file << "#include <cstdio>\n";
    file << "#include <functional>\n";
    file << "#include <map>\n\n";

    if (!m_opts.ns.empty()) file << "namespace " << m_opts.ns << " {\n\n";

    for (const auto& [addr, func] : m_functions) {
        if (m_import_stubs.count(addr)) continue; // Skip import stubs
        file << "void " << func.name << "(IopContext& iop_ctx);\n";
    }

    file << "\n// Function lookup map (address -> recompiled function).\n";
    file << "extern std::map<uint32_t, std::function<void(IopContext&)>> "
            "iop_recompiled_functions;\n";
    file << "void initialize_iop_recompiled_functions();\n";

    if (!m_opts.ns.empty()) {
        file << "\n// The module as recompiled: its load base, relocated entry and allocated extent, and\n";
        file << "// its import stubs (address -> library, index) so a stub address taken as a function\n";
        file << "// pointer resolves to the same import_call the JAL sites make.\n";
        file << "struct ImportStub { uint32_t addr; const char* lib; uint32_t index; };\n";
        file << "constexpr uint32_t kBase = 0x" << std::hex << m_opts.base << ";\n";
        file << "constexpr uint32_t kEntry = 0x" << m_opts.entry << ";\n";
        file << "constexpr uint32_t kLoadExtent = 0x" << m_opts.load_extent << std::dec << ";\n";
        file << "constexpr uint32_t kImportStubCount = " << m_import_stubs.size() << ";\n";
        file << "extern const ImportStub kImportStubs[" << (m_import_stubs.empty() ? 1 : m_import_stubs.size()) << "];\n";
        file << "\n}  // namespace " << m_opts.ns << "\n";
    }
}

// ---------------------------------------------------------------------------
// Cpp file generation
// ---------------------------------------------------------------------------
void IopRecompiler::write_cpp_file(std::ofstream& file,
                                    const std::string& header_filename) {
    file << "#include \"" << header_filename << "\"\n";
    file << "#include \"../host_app/iop_context.h\"\n";
    file << "#include \"../host_app/iop_memory.h\"\n";
    file << "#include \"../host_app/iop_stubs.h\"\n\n";
    file << "#include <cstdio>\n";
    file << "#include <cstdint>\n";
    file << "#include <cstdlib>\n";
    file << "#include <functional>\n";
    file << "#include <map>\n\n";

    if (!m_opts.ns.empty()) {
        file << "// Recompiled from " << m_opts.input_name << " at base 0x" << std::hex << m_opts.base << std::dec
             << " (ps2r_iop_recompiler --namespace " << m_opts.ns << ")\n";
        file << "namespace " << m_opts.ns << " {\n\n";
    }

    file << "std::map<uint32_t, std::function<void(IopContext&)>> "
            "iop_recompiled_functions;\n\n";

    // Emit each function body (skip functions in the import stub range).
    for (const auto& [addr, func] : m_functions) {
        if (m_import_stubs.count(addr)) continue; // Skip import stubs
        recompile_function(func, file);
    }

    // Emit the initialization function that populates the lookup map.
    file << "void initialize_iop_recompiled_functions() {\n";
    for (const auto& [addr, func] : m_functions) {
        if (m_import_stubs.count(addr)) continue; // Skip import stubs
        file << "    iop_recompiled_functions[0x" << std::hex << addr
             << "] = " << func.name << ";\n";
    }
    file << "}\n";

    if (!m_opts.ns.empty()) {
        file << std::dec << "\nconst ImportStub kImportStubs[" << (m_import_stubs.empty() ? 1 : m_import_stubs.size()) << "] = {\n";
        if (m_import_stubs.empty()) file << "    {0, \"\", 0},\n";
        for (const auto& [addr, raw] : m_import_stubs) {
            const size_t us = raw.rfind('_');
            const std::string lib = us == std::string::npos ? raw : raw.substr(0, us);
            const std::string idx = us == std::string::npos ? "0" : raw.substr(us + 1);
            file << "    {0x" << std::hex << addr << std::dec << ", \"" << lib << "\", " << idx << "},\n";
        }
        file << "};\n";
        file << "\n}  // namespace " << m_opts.ns << "\n";
    }
}

// ---------------------------------------------------------------------------
// Per-function recompilation — reads instruction bytes, decodes, emits C++.
// Handles delay slots: when a branch/jump is encountered, the delay slot
// instruction is emitted first, then the branch/goto.
// ---------------------------------------------------------------------------
void IopRecompiler::recompile_function(const IopFunction& func,
                                        std::ofstream& file) {
    uint32_t func_size = func.end_addr - func.start_addr;
    uint32_t num_instrs = func_size / 4;

    file << "// Function at 0x" << std::hex << func.start_addr
         << " - 0x" << func.end_addr << " (" << std::dec << num_instrs
         << " instructions)\n";
    file << "void " << func.name << "(IopContext& iop_ctx) {\n";

    // [S63 STRMQ] Env-gated (PS2_STRMQ=1, default OFF) ra-stamped entry
    // probes for the stream-prime wedge investigation (LOADTIME review
    // change 4, step a): the job-retire surfaces FUN_0000c8f0 (resolver)
    // and FUN_00000594 (per-slot retire), plus the prime-pipeline edges
    // FUN_0000e9d4 (upload-job enqueue), FUN_0000f230 (state-2 armed-
    // transfer completion = the state-2->state-0 transition writer) and
    // FUN_0000fc0c (CD refill request). Read-only stderr trace; emitted at
    // the recompiler level per house convention. gpr[31]=ra names the
    // retiring caller; a0..a3 carry the MIPS args.
    static const uint32_t kStrmqProbeAddrs[] = {0xc8f0, 0x594, 0xe9d4,
                                                0xf230, 0xfc0c};
    for (uint32_t probe_addr : kStrmqProbeAddrs) {
        if (!m_opts.legacy_title_stubs) break;
        if (func.start_addr != probe_addr) continue;
        file << "    { static const bool s_strmq = [] { const char* e = "
                "std::getenv(\"PS2_STRMQ\"); return e && e[0] == '1'; }();\n"
                "      if (s_strmq) std::fprintf(stderr, \"[STRMQ] "
             << func.name
             << " ra=0x%x a0=0x%x a1=0x%x a2=0x%x a3=0x%x\\n\", "
                "iop_ctx.gpr[31], iop_ctx.gpr[4], iop_ctx.gpr[5], "
                "iop_ctx.gpr[6], iop_ctx.gpr[7]); }\n";
        break;
    }

    // Check that the function's instructions are within our .text data
    if (func.start_addr < m_text_base ||
        func.end_addr > m_text_base + m_text_size) {
        file << "    // ERROR: function outside .text range\n";
        file << "    std::printf(\"[IOP-RECOMP] ERROR: " << func.name
             << " outside .text range\\n\");\n";
        file << "}\n\n";
        return;
    }

    // Iterate over each instruction in the function
    for (uint32_t i = 0; i < num_instrs; ++i) {
        uint32_t pc = func.start_addr + i * 4;
        uint32_t offset = pc - m_text_base;
        uint32_t word = read_u32_le(m_text_data + offset);

        // Decode instruction
        rabbitizer::InstructionCpu instr(word, pc);

        // Emit a label for every instruction address (branch targets use these)
        file << label(pc) << ": ;\n";

        // [STREAM-RPC-HLE] The sceSifCallRpc import thunk at IOP 0x128a8 (the
        // STREAM.IRX -> EE CD-read RPC, called only from FUN_0000e278) is an
        // empty `jr ra` on stock, so the stream never signals completion and the
        // cutscene state machine stalls -> null-actor crash. Give it a real body
        // by calling the host HLE, which fills the reply buffer at IOP 0x15310
        // from the request at 0x14a00. Runs BEFORE the jr, so the caller sees the
        // synthesized reply on return. Self-gated: FUN_0000e278 only reaches this
        // when the RPC is armed (cutscene streaming), not the initial load.
        if (m_opts.legacy_title_stubs && pc == 0x000128a8) {
            file << "    iop_stubs::hle_stream_cd_read_rpc(iop_ctx);\n";
        }

        // Check if this instruction has a delay slot (branch or jump)
        if (instr.hasDelaySlot() && (i + 1 < num_instrs)) {
            // Read the delay slot instruction
            uint32_t ds_pc = pc + 4;
            uint32_t ds_offset = ds_pc - m_text_base;
            uint32_t ds_word = read_u32_le(m_text_data + ds_offset);
            rabbitizer::InstructionCpu ds_instr(ds_word, ds_pc);

            // Emit the branch/jump with its delay slot handled inline.
            // The strategy: emit the delay slot instruction first,
            // then the branch/goto. This matches MIPS semantics where
            // the delay slot executes before the branch takes effect.

            // For conditional branches: we need to evaluate the condition
            // BEFORE the delay slot modifies registers, so we save the
            // condition result first.
            auto uid = instr.getUniqueId();

            bool is_conditional_branch =
                (uid == rabbitizer::InstrId::UniqueId::cpu_beq) ||
                (uid == rabbitizer::InstrId::UniqueId::cpu_bne) ||
                (uid == rabbitizer::InstrId::UniqueId::cpu_beqz) ||
                (uid == rabbitizer::InstrId::UniqueId::cpu_bnez) ||
                (uid == rabbitizer::InstrId::UniqueId::cpu_blez) ||
                (uid == rabbitizer::InstrId::UniqueId::cpu_bgtz) ||
                (uid == rabbitizer::InstrId::UniqueId::cpu_bltz) ||
                (uid == rabbitizer::InstrId::UniqueId::cpu_bgez) ||
                (uid == rabbitizer::InstrId::UniqueId::cpu_bltzal) ||
                (uid == rabbitizer::InstrId::UniqueId::cpu_bgezal);

            if (is_conditional_branch) {
                // Evaluate condition before delay slot
                uint8_t rs = static_cast<uint8_t>(instr.GetO32_rs());
                uint8_t rt = static_cast<uint8_t>(instr.GetO32_rt());
                int16_t imm = static_cast<int16_t>(instr.Get_immediate());
                uint32_t target = pc + 4 + (imm << 2);

                file << "    { // branch + delay slot at 0x" << std::hex << pc << "\n";

                // Evaluate condition
                switch (uid) {
                    case rabbitizer::InstrId::UniqueId::cpu_beq:
                        file << "    bool _cond = (" << gpr(rs) << " == " << gpr(rt) << ");\n";
                        break;
                    case rabbitizer::InstrId::UniqueId::cpu_bne:
                        file << "    bool _cond = (" << gpr(rs) << " != " << gpr(rt) << ");\n";
                        break;
                    case rabbitizer::InstrId::UniqueId::cpu_beqz:
                        file << "    bool _cond = (" << gpr(rs) << " == 0);\n";
                        break;
                    case rabbitizer::InstrId::UniqueId::cpu_bnez:
                        file << "    bool _cond = (" << gpr(rs) << " != 0);\n";
                        break;
                    case rabbitizer::InstrId::UniqueId::cpu_blez:
                        file << "    bool _cond = (static_cast<int32_t>(" << gpr(rs) << ") <= 0);\n";
                        break;
                    case rabbitizer::InstrId::UniqueId::cpu_bgtz:
                        file << "    bool _cond = (static_cast<int32_t>(" << gpr(rs) << ") > 0);\n";
                        break;
                    case rabbitizer::InstrId::UniqueId::cpu_bltz:
                        file << "    bool _cond = (static_cast<int32_t>(" << gpr(rs) << ") < 0);\n";
                        break;
                    case rabbitizer::InstrId::UniqueId::cpu_bgez:
                        file << "    bool _cond = (static_cast<int32_t>(" << gpr(rs) << ") >= 0);\n";
                        break;
                    case rabbitizer::InstrId::UniqueId::cpu_bltzal:
                        file << "    bool _cond = (static_cast<int32_t>(" << gpr(rs) << ") < 0);\n";
                        file << "    " << gpr(31) << " = 0x" << std::hex << (pc + 8) << ";\n";
                        file << "    iop_fix_zero(iop_ctx);\n";
                        break;
                    case rabbitizer::InstrId::UniqueId::cpu_bgezal:
                        file << "    bool _cond = (static_cast<int32_t>(" << gpr(rs) << ") >= 0);\n";
                        file << "    " << gpr(31) << " = 0x" << std::hex << (pc + 8) << ";\n";
                        file << "    iop_fix_zero(iop_ctx);\n";
                        break;
                    default:
                        break;
                }

                // Emit delay slot instruction
                file << "    // delay slot:\n";
                translate_instruction(ds_instr, ds_pc, file);

                // Emit the conditional goto
                file << "    if (_cond) goto " << label(target) << ";\n";
                file << "    } // end branch\n";
            }
            // `b` (beq $zero,$zero) and `bal` (bgezal $zero): rabbitizer's pseudo
            // ids for the unconditional forms -- the frozen tool never met them
            // (its one title's TU has none); a module built by another compiler
            // does. `b` is a goto; `bal` links ra and calls (a target inside the
            // function is a local goto, as the bgezal form above).
            else if (uid == rabbitizer::InstrId::UniqueId::cpu_b ||
                     uid == rabbitizer::InstrId::UniqueId::cpu_bal) {
                int16_t imm = static_cast<int16_t>(instr.Get_immediate());
                uint32_t target = pc + 4 + (imm << 2);
                const bool link = uid == rabbitizer::InstrId::UniqueId::cpu_bal;
                file << "    { // " << (link ? "bal" : "b") << " + delay slot at 0x" << std::hex << pc << "\n";
                if (link) {
                    file << "    " << gpr(31) << " = 0x" << std::hex << (pc + 8) << ";\n";
                    file << "    iop_fix_zero(iop_ctx);\n";
                }
                file << "    // delay slot:\n";
                translate_instruction(ds_instr, ds_pc, file);
                if (target >= func.start_addr && target < func.end_addr) {
                    file << "    goto " << label(target) << ";\n";
                } else if (link && m_functions.count(target)) {
                    file << "    " << m_functions.at(target).name << "(iop_ctx);\n";
                } else if (link) {
                    m_census.unresolved_jal++;
                    emit_indirect_call(file, "    ", "0x" + hex(target).substr(2),
                                       "std::printf(\"[IOP-RECOMP] Unresolved BAL to 0x%08X\\n\", 0x" + hex(target).substr(2) + ");",
                                       "");
                } else {
                    m_census.tail_j_sites++;
                    emit_indirect_call(file, "    ", "0x" + hex(target).substr(2), "", "return;");
                    file << "    std::printf(\"[IOP-RECOMP] Unresolved B to 0x%08X\\n\", 0x"
                         << std::hex << target << ");\n";
                    file << "    return;\n";
                }
                file << "    } // end " << (link ? "bal" : "b") << "\n";
            }
            // Unconditional jumps: J, JAL, JR, JALR
            else if (uid == rabbitizer::InstrId::UniqueId::cpu_j) {
                uint32_t target = (pc & 0xF0000000) | (instr.Get_instr_index() << 2);
                file << "    { // j + delay slot at 0x" << std::hex << pc << "\n";
                file << "    // delay slot:\n";
                translate_instruction(ds_instr, ds_pc, file);
                // If target is within this function, use goto
                if (target >= func.start_addr && target < func.end_addr) {
                    file << "    goto " << label(target) << ";\n";
                } else {
                    // Jump to another function — shouldn't normally happen
                    // but handle it as a tail call
                    file << "    // tail jump to 0x" << std::hex << target << "\n";
                    m_census.tail_j_sites++;
                    if (m_functions.count(target) && !m_opts.ns.empty()) {
                        file << "    " << m_functions.at(target).name << "(iop_ctx);\n";
                        file << "    return;\n";
                    } else {
                        emit_indirect_call(file, "    ", "0x" + hex(target).substr(2),
                                           "", "return;");
                        file << "    std::printf(\"[IOP-RECOMP] Unresolved J to 0x%08X\\n\", 0x"
                             << std::hex << target << ");\n";
                        file << "    return;\n";
                    }
                }
                file << "    } // end j\n";
            }
            else if (uid == rabbitizer::InstrId::UniqueId::cpu_jal) {
                uint32_t target = (pc & 0xF0000000) | (instr.Get_instr_index() << 2);
                file << "    { // jal + delay slot at 0x" << std::hex << pc << "\n";
                // Set return address
                file << "    " << gpr(31) << " = 0x" << std::hex << (pc + 8) << ";\n";
                file << "    iop_fix_zero(iop_ctx);\n";
                // Emit delay slot
                file << "    // delay slot:\n";
                translate_instruction(ds_instr, ds_pc, file);

                // Check if target is an import stub
                auto stub_it = m_import_stubs.find(target);
                if (stub_it != m_import_stubs.end()) {
                    // Emit HLE stub call
                    file << "    " << import_call_expr(stub_it->second) << ";\n";
                }
                // Check if target is a known recompiled function
                else if (m_functions.count(target)) {
                    file << "    " << m_functions.at(target).name << "(iop_ctx);\n";
                }
                // Unresolved JAL — emit runtime lookup with error logging
                else {
                    m_census.unresolved_jal++;
                    file << "    // unresolved JAL target 0x" << std::hex << target << "\n";
                    emit_indirect_call(file, "    ", "0x" + hex(target).substr(2),
                                       "std::printf(\"[IOP-RECOMP] Unresolved JAL to 0x%08X\\n\", 0x" + hex(target).substr(2) + ");",
                                       "");
                }
                file << "    } // end jal\n";
            }
            else if (uid == rabbitizer::InstrId::UniqueId::cpu_jr) {
                uint8_t rs = static_cast<uint8_t>(instr.GetO32_rs());
                file << "    { // jr + delay slot at 0x" << std::hex << pc << "\n";
                // Capture target before delay slot might modify it
                file << "    uint32_t _jr_target = " << gpr(rs) << ";\n";
                file << "    // delay slot:\n";
                translate_instruction(ds_instr, ds_pc, file);

                if (rs == 31) {
                    // JR $ra — function return
                    file << "    return;\n";
                } else {
                    // JR to computed address (jump table / switch statement).
                    // Emit a switch mapping every instruction address in this
                    // function to its goto label.  This handles jump tables
                    // whose targets are interior labels, not function entries.
                    file << "    // jump-table dispatch within "
                         << func.name << "\n";
                    file << "    switch (_jr_target) {\n";
                    for (uint32_t a = func.start_addr; a < func.end_addr;
                         a += 4) {
                        file << "    case 0x" << std::hex << a
                             << "u: goto " << label(a) << ";\n";
                    }
                    file << "    default:\n";
                    file << "        // Target outside this function — try "
                            "function map\n";
                    m_census.jr_switch_sites++;
                    if (m_opts.ns.empty()) {
                        file << "        if (iop_recompiled_functions.count("
                                "_jr_target)) {\n";
                        file << "            iop_recompiled_functions[_jr_target]"
                                "(iop_ctx);\n";
                        // After calling the external function, continue at the
                        // next instruction (pc+8) instead of returning. This
                        // handles jump tables used for command dispatch where
                        // the handler returns to the caller's loop, not exits.
                        file << "            goto " << label(pc + 8) << ";\n";
                        file << "        }\n";
                        file << "        std::printf(\"[IOP-RECOMP] Unresolved JR "
                                "to 0x%08X at 0x" << hex(pc).substr(2) << "\\n\", _jr_target);\n";
                        file << "        goto " << label(pc + 8) << ";\n";
                    } else {
                        // A jr to another function is a TAIL CALL (the callee returns
                        // to this function's caller): run it and return.
                        emit_indirect_call(file, "        ", "_jr_target", "", "return;");
                        file << "        std::printf(\"[IOP-RECOMP] Unresolved JR "
                                "to 0x%08X at 0x" << hex(pc).substr(2) << "\\n\", _jr_target);\n";
                        file << "        return;\n";
                    }
                    file << "    }\n";
                }
                file << "    } // end jr\n";
            }
            else if (uid == rabbitizer::InstrId::UniqueId::cpu_jalr) {
                uint8_t rs = static_cast<uint8_t>(instr.GetO32_rs());
                uint8_t rd = static_cast<uint8_t>(instr.GetO32_rd());
                file << "    { // jalr + delay slot at 0x" << std::hex << pc << "\n";
                // Capture target before delay slot
                file << "    uint32_t _jalr_target = " << gpr(rs) << ";\n";
                // Set link register
                file << "    " << gpr(rd) << " = 0x" << std::hex << (pc + 8) << ";\n";
                file << "    iop_fix_zero(iop_ctx);\n";
                file << "    // delay slot:\n";
                translate_instruction(ds_instr, ds_pc, file);
                // Call target
                m_census.jalr_sites++;
                emit_indirect_call(file, "    ", "_jalr_target",
                                   "std::printf(\"[IOP-RECOMP] Unresolved JALR to 0x%08X at 0x" + hex(pc).substr(2) + "\\n\", _jalr_target);", "");
                file << "    } // end jalr\n";
            }
            else {
                // Unknown delay-slot instruction — emit both normally
                translate_instruction(instr, pc, file);
                // delay slot will be emitted on next iteration
                continue;
            }

            // Skip the delay slot instruction (already emitted above)
            // Emit a label for the delay slot address so jump-table
            // switches can reference it if needed.
            file << label(pc + 4) << ": ;\n";
            ++i;
        } else {
            // Normal instruction (no delay slot)
            translate_instruction(instr, pc, file);
        }
    }

    file << "}\n\n";
}

// ---------------------------------------------------------------------------
// Instruction translation — emits C++ for a single R3000A instruction.
// All GPR accesses use iop_ctx.gpr[N] (32-bit).
// Memory accesses use iop_memory::read<T> / iop_memory::write<T>.
// After any GPR write, emits iop_fix_zero(iop_ctx) to enforce gpr[0]=0.
// Branch/jump instructions are NOT handled here — they are handled in
// recompile_function with delay slot logic.
// ---------------------------------------------------------------------------
void IopRecompiler::translate_instruction(
        const rabbitizer::InstructionCpu& instr, uint32_t pc,
        std::ofstream& file) {
    auto uid = instr.getUniqueId();
    uint8_t rs = static_cast<uint8_t>(instr.GetO32_rs());
    uint8_t rt = static_cast<uint8_t>(instr.GetO32_rt());
    uint8_t rd = static_cast<uint8_t>(instr.GetO32_rd());
    int16_t imm = static_cast<int16_t>(instr.Get_immediate());
    uint8_t sa = instr.Get_sa();

    switch (uid) {

    // =====================================================================
    // NOP / pseudo-NOP (sll $zero, $zero, 0)
    // =====================================================================
    case rabbitizer::InstrId::UniqueId::cpu_nop:
        file << "    // nop\n";
        break;

    // =====================================================================
    // ALU R-type: add, addu, sub, subu, and, or, xor, nor, slt, sltu
    // =====================================================================
    case rabbitizer::InstrId::UniqueId::cpu_add:
    case rabbitizer::InstrId::UniqueId::cpu_addu:
        file << "    " << gpr(rd) << " = static_cast<uint32_t>("
             << "static_cast<int32_t>(" << gpr(rs) << ") + "
             << "static_cast<int32_t>(" << gpr(rt) << "));\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_sub:
    case rabbitizer::InstrId::UniqueId::cpu_subu:
        file << "    " << gpr(rd) << " = static_cast<uint32_t>("
             << "static_cast<int32_t>(" << gpr(rs) << ") - "
             << "static_cast<int32_t>(" << gpr(rt) << "));\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_and:
        file << "    " << gpr(rd) << " = " << gpr(rs) << " & " << gpr(rt) << ";\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_or:
        file << "    " << gpr(rd) << " = " << gpr(rs) << " | " << gpr(rt) << ";\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_xor:
        file << "    " << gpr(rd) << " = " << gpr(rs) << " ^ " << gpr(rt) << ";\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_nor:
        file << "    " << gpr(rd) << " = ~(" << gpr(rs) << " | " << gpr(rt) << ");\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_slt:
        file << "    " << gpr(rd) << " = (static_cast<int32_t>(" << gpr(rs)
             << ") < static_cast<int32_t>(" << gpr(rt) << ")) ? 1u : 0u;\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_sltu:
        file << "    " << gpr(rd) << " = (" << gpr(rs) << " < " << gpr(rt)
             << ") ? 1u : 0u;\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    // =====================================================================
    // ALU immediate: addiu, andi, ori, xori, slti, sltiu, lui
    // =====================================================================
    case rabbitizer::InstrId::UniqueId::cpu_addi:
    case rabbitizer::InstrId::UniqueId::cpu_addiu:
        file << "    " << gpr(rt) << " = static_cast<uint32_t>("
             << "static_cast<int32_t>(" << gpr(rs) << ") + " << simm(imm) << ");\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_andi:
        file << "    " << gpr(rt) << " = " << gpr(rs) << " & "
             << hex(static_cast<uint16_t>(imm)) << "u;\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_ori:
        file << "    " << gpr(rt) << " = " << gpr(rs) << " | "
             << hex(static_cast<uint16_t>(imm)) << "u;\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_xori:
        file << "    " << gpr(rt) << " = " << gpr(rs) << " ^ "
             << hex(static_cast<uint16_t>(imm)) << "u;\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_slti:
        file << "    " << gpr(rt) << " = (static_cast<int32_t>(" << gpr(rs)
             << ") < static_cast<int32_t>(" << simm(imm) << ")) ? 1u : 0u;\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_sltiu:
        file << "    " << gpr(rt) << " = (" << gpr(rs) << " < static_cast<uint32_t>("
             << simm(imm) << ")) ? 1u : 0u;\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_lui:
        file << "    " << gpr(rt) << " = "
             << hex(static_cast<uint32_t>(static_cast<uint16_t>(imm)) << 16)
             << "u;\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    // =====================================================================
    // Shift: sll, srl, sra, sllv, srlv, srav
    // =====================================================================
    case rabbitizer::InstrId::UniqueId::cpu_sll:
        file << "    " << gpr(rd) << " = " << gpr(rt) << " << "
             << std::dec << static_cast<int>(sa) << ";\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_srl:
        file << "    " << gpr(rd) << " = " << gpr(rt) << " >> "
             << std::dec << static_cast<int>(sa) << ";\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_sra:
        file << "    " << gpr(rd) << " = static_cast<uint32_t>("
             << "static_cast<int32_t>(" << gpr(rt) << ") >> "
             << std::dec << static_cast<int>(sa) << ");\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_sllv:
        file << "    " << gpr(rd) << " = " << gpr(rt) << " << ("
             << gpr(rs) << " & 0x1Fu);\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_srlv:
        file << "    " << gpr(rd) << " = " << gpr(rt) << " >> ("
             << gpr(rs) << " & 0x1Fu);\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_srav:
        file << "    " << gpr(rd) << " = static_cast<uint32_t>("
             << "static_cast<int32_t>(" << gpr(rt) << ") >> ("
             << gpr(rs) << " & 0x1Fu));\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    // =====================================================================
    // Multiply/divide: mult, multu, div, divu, mfhi, mflo, mthi, mtlo
    // =====================================================================
    case rabbitizer::InstrId::UniqueId::cpu_mult:
        file << "    {\n";
        file << "        int64_t _result = static_cast<int64_t>(static_cast<int32_t>("
             << gpr(rs) << ")) * static_cast<int64_t>(static_cast<int32_t>("
             << gpr(rt) << "));\n";
        file << "        iop_ctx.lo = static_cast<uint32_t>(_result);\n";
        file << "        iop_ctx.hi = static_cast<uint32_t>(static_cast<uint64_t>(_result) >> 32);\n";
        file << "    }\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_multu:
        file << "    {\n";
        file << "        uint64_t _result = static_cast<uint64_t>("
             << gpr(rs) << ") * static_cast<uint64_t>(" << gpr(rt) << ");\n";
        file << "        iop_ctx.lo = static_cast<uint32_t>(_result);\n";
        file << "        iop_ctx.hi = static_cast<uint32_t>(_result >> 32);\n";
        file << "    }\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_div:
        file << "    if (" << gpr(rt) << " != 0) {\n";
        file << "        iop_ctx.lo = static_cast<uint32_t>("
             << "static_cast<int32_t>(" << gpr(rs) << ") / "
             << "static_cast<int32_t>(" << gpr(rt) << "));\n";
        file << "        iop_ctx.hi = static_cast<uint32_t>("
             << "static_cast<int32_t>(" << gpr(rs) << ") % "
             << "static_cast<int32_t>(" << gpr(rt) << "));\n";
        file << "    }\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_divu:
        file << "    if (" << gpr(rt) << " != 0) {\n";
        file << "        iop_ctx.lo = " << gpr(rs) << " / " << gpr(rt) << ";\n";
        file << "        iop_ctx.hi = " << gpr(rs) << " % " << gpr(rt) << ";\n";
        file << "    }\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_mfhi:
        file << "    " << gpr(rd) << " = iop_ctx.hi;\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_mflo:
        file << "    " << gpr(rd) << " = iop_ctx.lo;\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_mthi:
        file << "    iop_ctx.hi = " << gpr(rs) << ";\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_mtlo:
        file << "    iop_ctx.lo = " << gpr(rs) << ";\n";
        break;

    // =====================================================================
    // Load: lb, lbu, lh, lhu, lw, lwl, lwr
    // =====================================================================
    case rabbitizer::InstrId::UniqueId::cpu_lb:
        file << "    " << gpr(rt) << " = static_cast<uint32_t>("
             << "static_cast<int32_t>(static_cast<int8_t>("
             << "iop_memory::read<uint8_t>(" << gpr(rs) << " + " << simm(imm) << "))));\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_lbu:
        file << "    " << gpr(rt) << " = static_cast<uint32_t>("
             << "iop_memory::read<uint8_t>(" << gpr(rs) << " + " << simm(imm) << "));\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_lh:
        file << "    " << gpr(rt) << " = static_cast<uint32_t>("
             << "static_cast<int32_t>(static_cast<int16_t>("
             << "iop_memory::read<uint16_t>(" << gpr(rs) << " + " << simm(imm) << "))));\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_lhu:
        file << "    " << gpr(rt) << " = static_cast<uint32_t>("
             << "iop_memory::read<uint16_t>(" << gpr(rs) << " + " << simm(imm) << "));\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_lw:
        file << "    " << gpr(rt) << " = "
             << "iop_memory::read<uint32_t>(" << gpr(rs) << " + " << simm(imm) << ");\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_lwl:
        // Load Word Left — loads the most-significant bytes of a word
        // from an unaligned address into the upper bytes of the register.
        file << "    {\n";
        file << "        uint32_t _addr = " << gpr(rs) << " + " << simm(imm) << ";\n";
        file << "        uint32_t _aligned = _addr & ~3u;\n";
        file << "        uint32_t _word = iop_memory::read<uint32_t>(_aligned);\n";
        file << "        uint32_t _shift = (_addr & 3u) * 8;\n";
        file << "        uint32_t _mask = 0xFFFFFFFFu << (24 - _shift);\n";
        file << "        " << gpr(rt) << " = (" << gpr(rt) << " & ~_mask) | (_word << (24 - _shift));\n";
        file << "    }\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_lwr:
        // Load Word Right — loads the least-significant bytes of a word
        // from an unaligned address into the lower bytes of the register.
        file << "    {\n";
        file << "        uint32_t _addr = " << gpr(rs) << " + " << simm(imm) << ";\n";
        file << "        uint32_t _aligned = _addr & ~3u;\n";
        file << "        uint32_t _word = iop_memory::read<uint32_t>(_aligned);\n";
        file << "        uint32_t _shift = (_addr & 3u) * 8;\n";
        file << "        uint32_t _mask = 0xFFFFFFFFu >> _shift;\n";
        file << "        " << gpr(rt) << " = (" << gpr(rt) << " & ~_mask) | (_word >> _shift);\n";
        file << "    }\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    // =====================================================================
    // Store: sb, sh, sw, swl, swr
    // =====================================================================
    case rabbitizer::InstrId::UniqueId::cpu_sb:
        file << "    iop_memory::write<uint8_t>(" << gpr(rs) << " + " << simm(imm)
             << ", static_cast<uint8_t>(" << gpr(rt) << "));\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_sh:
        file << "    iop_memory::write<uint16_t>(" << gpr(rs) << " + " << simm(imm)
             << ", static_cast<uint16_t>(" << gpr(rt) << "));\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_sw:
        file << "    iop_memory::write<uint32_t>(" << gpr(rs) << " + " << simm(imm)
             << ", " << gpr(rt) << ");\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_swl:
        // Store Word Left — stores the most-significant bytes of a register
        // to an unaligned address.
        file << "    {\n";
        file << "        uint32_t _addr = " << gpr(rs) << " + " << simm(imm) << ";\n";
        file << "        uint32_t _aligned = _addr & ~3u;\n";
        file << "        uint32_t _word = iop_memory::read<uint32_t>(_aligned);\n";
        file << "        uint32_t _shift = (_addr & 3u) * 8;\n";
        file << "        uint32_t _mask = 0xFFFFFFFFu >> (24 - _shift);\n";
        file << "        _word = (_word & ~_mask) | (" << gpr(rt) << " >> (24 - _shift));\n";
        file << "        iop_memory::write<uint32_t>(_aligned, _word);\n";
        file << "    }\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_swr:
        // Store Word Right — stores the least-significant bytes of a register
        // to an unaligned address.
        file << "    {\n";
        file << "        uint32_t _addr = " << gpr(rs) << " + " << simm(imm) << ";\n";
        file << "        uint32_t _aligned = _addr & ~3u;\n";
        file << "        uint32_t _word = iop_memory::read<uint32_t>(_aligned);\n";
        file << "        uint32_t _shift = (_addr & 3u) * 8;\n";
        file << "        uint32_t _mask = 0xFFFFFFFFu << _shift;\n";
        file << "        _word = (_word & ~_mask) | (" << gpr(rt) << " << _shift);\n";
        file << "        iop_memory::write<uint32_t>(_aligned, _word);\n";
        file << "    }\n";
        break;

    // =====================================================================
    // Branch/jump instructions — these should NOT be reached here because
    // recompile_function handles them with delay slot logic. But if they
    // appear as delay slot instructions of other branches (which is illegal
    // on R3000A but we handle gracefully), emit a comment.
    // =====================================================================
    case rabbitizer::InstrId::UniqueId::cpu_beq:
    case rabbitizer::InstrId::UniqueId::cpu_bne:
    case rabbitizer::InstrId::UniqueId::cpu_beqz:
    case rabbitizer::InstrId::UniqueId::cpu_bnez:
    case rabbitizer::InstrId::UniqueId::cpu_blez:
    case rabbitizer::InstrId::UniqueId::cpu_bgtz:
    case rabbitizer::InstrId::UniqueId::cpu_bltz:
    case rabbitizer::InstrId::UniqueId::cpu_bgez:
    case rabbitizer::InstrId::UniqueId::cpu_bltzal:
    case rabbitizer::InstrId::UniqueId::cpu_bgezal:
    case rabbitizer::InstrId::UniqueId::cpu_j:
    case rabbitizer::InstrId::UniqueId::cpu_jal:
    case rabbitizer::InstrId::UniqueId::cpu_jr:
    case rabbitizer::InstrId::UniqueId::cpu_jalr:
        file << "    // WARNING: branch/jump in delay slot at 0x"
             << std::hex << pc << " (" << instr.getOpcodeName() << ")\n";
        m_census.branch_in_delay_slot++;
        break;

    // =====================================================================
    // Special: syscall, break
    // =====================================================================
    case rabbitizer::InstrId::UniqueId::cpu_syscall:
        file << "    std::printf(\"[IOP-RECOMP] SYSCALL at 0x" << std::hex << pc << "\\n\");\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_break:
        file << "    std::printf(\"[IOP-RECOMP] BREAK at 0x" << std::hex << pc << "\\n\");\n";
        file << "    return;\n";
        break;

    // =====================================================================
    // Move pseudo-instructions (rabbitizer may decode these)
    // =====================================================================
    case rabbitizer::InstrId::UniqueId::cpu_move:
        // move rd, rs  →  or rd, rs, $zero (or addu rd, rs, $zero)
        file << "    " << gpr(rd) << " = " << gpr(rs) << ";\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_negu:
        // negu rd, rt  →  subu rd, $zero, rt
        file << "    " << gpr(rd) << " = static_cast<uint32_t>(-static_cast<int32_t>(" << gpr(rt) << "));\n";
        file << "    iop_fix_zero(iop_ctx);\n";
        break;

    case rabbitizer::InstrId::UniqueId::cpu_b:
        // unconditional branch — handled by branch logic above, but if it
        // appears here it means it was in a delay slot
        file << "    // WARNING: unconditional branch in delay slot at 0x"
             << std::hex << pc << "\n";
        m_census.branch_in_delay_slot++;
        break;

    // =====================================================================
    // Default: unimplemented instruction
    // =====================================================================
    default:
        m_census.unimplemented++;
        m_census.unimplemented_opcodes[instr.getOpcodeName()]++;
        file << "    // UNIMPLEMENTED: " << instr.getOpcodeName()
             << " (0x" << std::hex << instr.getRaw() << ") at 0x" << pc << "\n";
        file << "    std::printf(\"[IOP-RECOMP] UNIMPLEMENTED: "
             << instr.getOpcodeName() << " at 0x%08X\\n\", 0x"
             << std::hex << pc << ");\n";
        break;
    }
}
