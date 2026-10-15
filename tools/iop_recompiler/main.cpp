#include "iop_recompiler.h"

#include <elfio/elfio.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Import table magic word used by IOP modules to declare library imports.
// Each import table starts with this 32-bit value, followed by a version
// word, then the null-terminated library name, then stub entries.
// ---------------------------------------------------------------------------
static constexpr uint32_t IOP_IMPORT_MAGIC = 0x41E00000;

// STREAM.IRX import stub address range (known from analysis)
static constexpr uint32_t IMPORT_STUB_START = 0x127E0;
static constexpr uint32_t IMPORT_STUB_END   = 0x12B70;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static std::string format_hex8(uint32_t addr) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(8) << addr;
    return ss.str();
}

static std::string make_func_name(uint32_t addr) {
    return "iop_func_" + format_hex8(addr);
}

// Read a little-endian 32-bit word from a byte buffer.
static uint32_t read_u32_le(const uint8_t* data) {
    return static_cast<uint32_t>(data[0])
         | (static_cast<uint32_t>(data[1]) << 8)
         | (static_cast<uint32_t>(data[2]) << 16)
         | (static_cast<uint32_t>(data[3]) << 24);
}

// ---------------------------------------------------------------------------
// Option A: Load Ghidra-exported CSV function boundaries
// ---------------------------------------------------------------------------
// CSV format: start_addr,end_addr,name  (hex addresses with 0x prefix)
// First line is a header and is skipped.
// ---------------------------------------------------------------------------
static std::map<uint32_t, IopFunction> load_ghidra_csv(
        const std::string& csv_path) {
    std::map<uint32_t, IopFunction> functions;

    std::ifstream file(csv_path);
    if (!file.is_open()) {
        std::cerr << "[IOP-RECOMP] Could not open Ghidra CSV: "
                  << csv_path << std::endl;
        return functions;
    }

    std::string line;
    int line_num = 0;

    while (std::getline(file, line)) {
        ++line_num;

        // Skip header line
        if (line_num == 1) {
            // Check if it looks like a header (starts with a letter)
            if (!line.empty() && !std::isdigit(line[0]) && line[0] != '0') {
                continue;
            }
            // If it starts with a digit/0x, it's data — fall through
        }

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Parse: start_addr,end_addr,name
        std::stringstream ss(line);
        std::string start_str, end_str, name;

        if (!std::getline(ss, start_str, ',')) continue;
        if (!std::getline(ss, end_str, ',')) continue;
        if (!std::getline(ss, name, ',')) continue;

        // Trim whitespace
        auto trim = [](std::string& s) {
            while (!s.empty() && std::isspace(s.front())) s.erase(s.begin());
            while (!s.empty() && std::isspace(s.back())) s.pop_back();
        };
        trim(start_str);
        trim(end_str);
        trim(name);

        uint32_t start_addr = 0, end_addr = 0;
        try {
            start_addr = static_cast<uint32_t>(std::stoul(start_str, nullptr, 0));
            end_addr   = static_cast<uint32_t>(std::stoul(end_str, nullptr, 0));
        } catch (...) {
            std::cerr << "[IOP-RECOMP] CSV parse error at line " << line_num
                      << ": " << line << std::endl;
            continue;
        }

        if (end_addr <= start_addr) {
            std::cerr << "[IOP-RECOMP] CSV: invalid range at line " << line_num
                      << " (start=0x" << std::hex << start_addr
                      << " end=0x" << end_addr << ")" << std::dec << std::endl;
            continue;
        }

        IopFunction func;
        func.start_addr = start_addr;
        func.end_addr   = end_addr;
        func.name        = name.empty() ? make_func_name(start_addr) : name;
        functions[start_addr] = func;
    }

    std::cout << "[IOP-RECOMP] Loaded " << functions.size()
              << " functions from Ghidra CSV: " << csv_path << std::endl;
    return functions;
}

// ---------------------------------------------------------------------------
// Import stub identification
// ---------------------------------------------------------------------------
// Scans .text for the import table magic word (0x41E00000). Each import
// table has the layout (IOP IRX standard):
//
//   +0x00: 0x41E00000  (magic word)
//   +0x04: 0x00000000  (padding)
//   +0x08: version      (u32, e.g. 0x00000102 = v1.2)
//   +0x0C: library name (8 bytes, null-padded ASCII)
//   +0x14: first stub   (8 bytes each):
//            word 0: JR $ra  (0x03E00008)
//            word 1: addiu $zero, $zero, N  (0x2400NNNN — kernel func index)
//   ...  : more stubs
//   +end:  0x00000000 0x00000000  (terminator pair)
//
// Header is 20 bytes (5 words). Stubs are 8 bytes each.
// We build a map: stub_address → "library_kernelindex"
// ---------------------------------------------------------------------------
static std::map<uint32_t, std::string> identify_import_stubs(
        const uint8_t* text_data,
        uint32_t text_size,
        uint32_t text_base) {
    std::map<uint32_t, std::string> stubs;

    if (!text_data || text_size < 20) return stubs;

    // Scan for import table magic words
    for (uint32_t offset = 0; offset + 20 <= text_size; offset += 4) {
        uint32_t word = read_u32_le(text_data + offset);
        if (word != IOP_IMPORT_MAGIC) continue;

        uint32_t table_addr = text_base + offset;

        // +0x04: padding (should be 0)
        // +0x08: version word
        // +0x0C: library name (8 bytes, null-padded)
        uint32_t name_start = offset + 12;
        if (name_start + 8 > text_size) continue;

        std::string lib_name;
        for (uint32_t i = 0; i < 8 && text_data[name_start + i] != '\0'; ++i) {
            lib_name += static_cast<char>(text_data[name_start + i]);
        }

        if (lib_name.empty()) {
            std::cout << "[IOP-RECOMP] Import table at 0x" << std::hex
                      << table_addr << ": empty library name — skipping"
                      << std::dec << std::endl;
            continue;
        }

        uint32_t version = read_u32_le(text_data + offset + 8);
        std::cout << "[IOP-RECOMP] Import table at 0x" << std::hex << table_addr
                  << ": library \"" << lib_name << "\" v"
                  << ((version >> 8) & 0xFF) << "." << (version & 0xFF)
                  << std::dec << std::endl;

        // Stubs start at offset + 20 (after the 5-word header)
        uint32_t stub_offset = offset + 20;
        int stub_count = 0;

        while (stub_offset + 8 <= text_size) {
            uint32_t w0 = read_u32_le(text_data + stub_offset);
            uint32_t w1 = read_u32_le(text_data + stub_offset + 4);

            // Terminator: zero pair or next magic word
            if (w0 == 0 && w1 == 0) break;
            if (w0 == IOP_IMPORT_MAGIC) break;

            // Verify this looks like a stub: w0 should be JR $ra (0x03E00008)
            if (w0 != 0x03E00008) {
                std::cout << "[IOP-RECOMP]   WARNING: unexpected stub word0=0x"
                          << std::hex << w0 << " at 0x"
                          << (text_base + stub_offset) << std::dec << std::endl;
                break;
            }

            // Extract the kernel function index from the delay slot.
            // Encoding: addiu $zero, $zero, N → 0x2400NNNN
            uint32_t kernel_index = w1 & 0xFFFF;
            uint32_t stub_addr = text_base + stub_offset;

            std::string stub_name = lib_name + "_" + std::to_string(kernel_index);
            stubs[stub_addr] = stub_name;

            std::cout << "[IOP-RECOMP]   stub 0x" << std::hex << stub_addr
                      << " -> " << stub_name << std::dec << std::endl;

            stub_count++;
            stub_offset += 8;
        }

        std::cout << "[IOP-RECOMP]   -> " << stub_count << " stubs registered"
                  << std::endl;

        // Advance the outer loop past this table to avoid re-scanning
        // the stubs as potential magic words.
        offset = stub_offset - 4; // -4 because the for loop does +4
    }

    std::cout << "[IOP-RECOMP] Total import stubs identified: " << stubs.size()
              << std::endl;
    return stubs;
}

// ---------------------------------------------------------------------------
// Option B: Built-in function boundary analysis
// ---------------------------------------------------------------------------
// When no Ghidra CSV is provided, we discover function boundaries by:
//   1. Scanning .text for JAL instructions → call targets are function starts
//   2. Scanning for JR $ra (0x03E00008) → function ends (after delay slot)
//   3. Using import stub addresses as known boundaries to exclude
//   4. The ELF entry point is always a function start
// ---------------------------------------------------------------------------
static std::map<uint32_t, IopFunction> builtin_analysis(
        const uint8_t* text_data,
        uint32_t text_size,
        uint32_t text_base,
        uint32_t entry_point,
        const std::map<uint32_t, std::string>& import_stubs) {

    std::map<uint32_t, IopFunction> functions;

    if (!text_data || text_size < 4) return functions;

    // Collect all known import stub addresses into a set for fast lookup
    std::set<uint32_t> stub_addrs;
    for (const auto& [addr, name] : import_stubs) {
        stub_addrs.insert(addr);
    }

    // --- Pass 1: Find function starts from JAL targets ---
    std::set<uint32_t> func_starts;

    // The entry point is always a function
    func_starts.insert(entry_point);

    // The first instruction in .text is a function start
    func_starts.insert(text_base);

    // Known thread entry points that are only reachable via CreateThread
    // (not via JAL, so they're missed by the JAL target scan)
    func_starts.insert(0x30d4);  // CD read worker thread entry
    func_starts.insert(0x64f4);  // RPC server thread entry
    // [S41-SND] SdSetTransIntrHandler callback: only reachable via function
    // pointer (registered with libsd ord 26 at init, FUN_0000029c passes
    // &0x13a8). Without this boundary the handler body is swallowed as
    // unreachable tail code of the function at 0x1348 and the host cannot
    // invoke it on VoiceTrans completion.
    func_starts.insert(0x13a8);  // SPU2 transfer-completion callback

    // [S43] CD-job-engine functions reachable ONLY via function pointer
    // (la + jalr / host-side registration). The generated jalr guard skips
    // unresolved targets ("Unresolved JALR"), so a missing entry here means
    // the engine's completion continuation silently never runs — the
    // DAT_00012F30 handshake stays pinned at 1 and the CD worker's whole
    // slot lifecycle freezes (the S42 boot-wedge f30=1 signature).
    func_starts.insert(0x136c);  // 60Hz hardware-timer tick handler
                                 // (SetTimerHandler target from FUN_000012c0)
    func_starts.insert(0x15e4);  // sceCdCallback CD-completion handler
                                 // (registered by FUN_0000b6d0, cmd 42)
    func_starts.insert(0x5904);  // job-done thunk (13d4 callback, stream path)
    func_starts.insert(0x592c);  // job-done thunk (13d4 callback, no-stream path)
    func_starts.insert(0x2698);  // slot-op continuation (9cdc disp-state 5)
    func_starts.insert(0x2734);  // slot-op continuation (2698 chain)
    func_starts.insert(0x27d4);  // slot-op continuation (9cdc disp-state 5, alt)
    func_starts.insert(0x2784);  // slot-op continuation (9cdc disp-state 6)
    func_starts.insert(0x2874);  // slot-op continuation (9cdc state 0)
    func_starts.insert(0x28c0);  // slot-op continuation (9cdc state 1)
    func_starts.insert(0x581c);  // CD-stream-op continuation (kind>=2)
    func_starts.insert(0x57a8);  // CD-stream-op continuation (state 0)
    func_starts.insert(0x5890);  // CD-stream-op continuation (state 1)

    // [S43] Generic guard for the same class: any `la` pair (lui hi=0/1 +
    // addiu) that materializes a 4-aligned .text address is a potential
    // function pointer. Candidates are accepted ONLY when they directly
    // follow a `jr $ra` + delay slot (word[target-8] == jr $ra) — the
    // property every genuine function start in this module has — so a false
    // positive can never split a live function (starts are also function
    // range boundaries in pass 3). A missed pointer target would silently
    // skip an engine continuation at the generated jalr guard instead.
    for (uint32_t offset = 0; offset + 8 <= text_size; offset += 4) {
        uint32_t w1 = read_u32_le(text_data + offset);
        if ((w1 >> 26) != 0x0F) continue;               // LUI
        uint32_t hi = w1 & 0xFFFF;
        if (hi > 1) continue;                            // module spans 0x0-0x12bc7
        uint32_t rt = (w1 >> 16) & 0x1F;
        // Look at the next few instructions for ADDIU rD, rt, lo16
        for (uint32_t j = 4; j <= 16 && offset + j + 4 <= text_size; j += 4) {
            uint32_t w2 = read_u32_le(text_data + offset + j);
            if ((w2 >> 26) == 0x09 && ((w2 >> 21) & 0x1F) == rt) {  // ADDIU rs==rt
                uint32_t target = (hi << 16) +
                    static_cast<uint32_t>(static_cast<int16_t>(w2 & 0xFFFF));
                if (target >= 0x100 && target + 4 < text_base + text_size &&
                    (target & 3) == 0 && target >= text_base + 8 &&
                    stub_addrs.find(target) == stub_addrs.end() &&
                    read_u32_le(text_data + (target - text_base) - 8) == 0x03E00008) {
                    func_starts.insert(target);
                }
                break;
            }
            if ((w2 >> 26) == 0x0F && ((w2 >> 16) & 0x1F) == rt) break; // rt clobbered
        }
    }

    for (uint32_t offset = 0; offset + 4 <= text_size; offset += 4) {
        uint32_t instr = read_u32_le(text_data + offset);

        // JAL instruction: opcode = 0x03 (bits 31:26)
        // Format: 000011 | 26-bit target
        uint32_t opcode = (instr >> 26) & 0x3F;
        if (opcode == 0x03) { // JAL
            // Target = (PC & 0xF0000000) | (target_field << 2)
            uint32_t target_field = instr & 0x03FFFFFF;
            uint32_t pc = text_base + offset;
            uint32_t target = (pc & 0xF0000000) | (target_field << 2);

            // Only add if target is within .text and not an import stub
            if (target >= text_base &&
                target < text_base + text_size &&
                stub_addrs.find(target) == stub_addrs.end()) {
                func_starts.insert(target);
            }
        }
    }

    std::cout << "[IOP-RECOMP] Built-in analysis: found " << func_starts.size()
              << " function start candidates from JAL targets" << std::endl;

    // --- Pass 2: Find function ends from JR $ra ---
    // JR $ra = 0x03E00008
    // The function end is the address after the delay slot (jr_addr + 8)
    std::set<uint32_t> func_ends; // addresses one-past the delay slot

    for (uint32_t offset = 0; offset + 4 <= text_size; offset += 4) {
        uint32_t instr = read_u32_le(text_data + offset);
        if (instr == 0x03E00008) { // JR $ra
            // Function end = address after the delay slot
            uint32_t end_addr = text_base + offset + 8;
            func_ends.insert(end_addr);
        }
    }

    std::cout << "[IOP-RECOMP] Built-in analysis: found " << func_ends.size()
              << " JR $ra instructions" << std::endl;

    // --- Pass 3: Build function map ---
    // For each function start, find the nearest function end that is
    // >= start and < next_function_start.
    std::vector<uint32_t> starts_vec(func_starts.begin(), func_starts.end());
    std::sort(starts_vec.begin(), starts_vec.end());

    // Remove any starts that fall within the import stub region
    std::vector<uint32_t> filtered_starts;
    for (uint32_t s : starts_vec) {
        if (s >= IMPORT_STUB_START && s < IMPORT_STUB_END) continue;
        if (stub_addrs.count(s)) continue;
        filtered_starts.push_back(s);
    }

    for (size_t i = 0; i < filtered_starts.size(); ++i) {
        uint32_t start = filtered_starts[i];
        uint32_t next_start = (i + 1 < filtered_starts.size())
                            ? filtered_starts[i + 1]
                            : text_base + text_size;

        // Find the last JR $ra end that falls within [start, next_start)
        uint32_t best_end = 0;
        for (uint32_t end : func_ends) {
            if (end > start && end <= next_start) {
                best_end = end;
            }
        }

        // If no JR $ra found, use next function start as the end
        if (best_end == 0) {
            best_end = next_start;
        }

        IopFunction func;
        func.start_addr = start;
        func.end_addr   = best_end;
        func.name        = make_func_name(start);
        functions[start] = func;
    }

    std::cout << "[IOP-RECOMP] Built-in analysis: " << functions.size()
              << " functions identified" << std::endl;

    return functions;
}

// ---------------------------------------------------------------------------
// IOP ELF parser — extracts sections and performs function boundary analysis
// ---------------------------------------------------------------------------
struct IopElfData {
    std::map<uint32_t, IopFunction> functions;
    std::map<uint32_t, std::string> import_stubs;
    const uint8_t* text_data = nullptr;
    uint32_t text_size = 0;
    uint32_t text_base = 0;
};

static IopElfData parse_iop_elf(const std::string& elf_path,
                                 const std::string& ghidra_csv_path) {
    IopElfData result;

    ELFIO::elfio reader;
    if (!reader.load(elf_path)) {
        std::cerr << "[IOP-RECOMP] Failed to load ELF: " << elf_path
                  << std::endl;
        return result;
    }

    // Verify this is a MIPS R3000 ELF (EM_MIPS = 8, 32-bit little-endian).
    if (reader.get_machine() != ELFIO::EM_MIPS) {
        std::cerr << "[IOP-RECOMP] ELF is not MIPS (machine="
                  << reader.get_machine() << ")" << std::endl;
        return result;
    }
    if (reader.get_class() != ELFIO::ELFCLASS32) {
        std::cerr << "[IOP-RECOMP] ELF is not 32-bit" << std::endl;
        return result;
    }

    // Find sections
    const ELFIO::section* text_sec = nullptr;
    const ELFIO::section* data_sec = nullptr;
    const ELFIO::section* rodata_sec = nullptr;
    const ELFIO::section* bss_sec = nullptr;

    for (const auto& sec : reader.sections) {
        const std::string& name = sec->get_name();
        if (name == ".text")   text_sec   = sec.get();
        if (name == ".data")   data_sec   = sec.get();
        if (name == ".rodata") rodata_sec = sec.get();
        if (name == ".bss")    bss_sec    = sec.get();
    }

    if (!text_sec) {
        std::cerr << "[IOP-RECOMP] .text section not found" << std::endl;
        return result;
    }

    result.text_data = reinterpret_cast<const uint8_t*>(text_sec->get_data());
    result.text_size = static_cast<uint32_t>(text_sec->get_size());
    result.text_base = static_cast<uint32_t>(text_sec->get_address());

    std::cout << "[IOP-RECOMP] .text at 0x" << std::hex << result.text_base
              << ", size 0x" << result.text_size << std::dec << " ("
              << (result.text_size / 4) << " instructions)" << std::endl;

    if (data_sec) {
        std::cout << "[IOP-RECOMP] .data at 0x" << std::hex
                  << data_sec->get_address() << ", size 0x"
                  << data_sec->get_size() << std::dec << std::endl;
    }
    if (rodata_sec) {
        std::cout << "[IOP-RECOMP] .rodata at 0x" << std::hex
                  << rodata_sec->get_address() << ", size 0x"
                  << rodata_sec->get_size() << std::dec << std::endl;
    }
    if (bss_sec) {
        std::cout << "[IOP-RECOMP] .bss at 0x" << std::hex
                  << bss_sec->get_address() << ", size 0x"
                  << bss_sec->get_size() << std::dec << std::endl;
    }

    // --- Import stub identification ---
    result.import_stubs = identify_import_stubs(
        result.text_data, result.text_size, result.text_base);

    // --- Function boundary analysis ---
    if (!ghidra_csv_path.empty()) {
        // Option A: Ghidra headless CSV
        std::cout << "[IOP-RECOMP] Using Ghidra CSV for function boundaries"
                  << std::endl;
        result.functions = load_ghidra_csv(ghidra_csv_path);
    } else {
        // Option B: Built-in analysis
        std::cout << "[IOP-RECOMP] Using built-in analysis for function boundaries"
                  << std::endl;
        uint32_t entry = static_cast<uint32_t>(reader.get_entry());
        result.functions = builtin_analysis(
            result.text_data, result.text_size, result.text_base,
            entry, result.import_stubs);
    }

    return result;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    std::string elf_path;
    std::string ghidra_csv_path;
    std::string output_header = "iop_recompiled.h";
    std::string output_cpp    = "iop_recompiled.cpp";

    // Parse command-line arguments.
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) {
            elf_path = argv[++i];
        } else if (arg == "--ghidra-input" && i + 1 < argc) {
            ghidra_csv_path = argv[++i];
        } else if (arg == "--output-header" && i + 1 < argc) {
            output_header = argv[++i];
        } else if (arg == "--output-cpp" && i + 1 < argc) {
            output_cpp = argv[++i];
        }
    }

    if (elf_path.empty()) {
        std::cerr << "Usage: " << argv[0]
                  << " --input <iop_elf>"
                  << " [--ghidra-input <csv>]"
                  << " [--output-header <header>]"
                  << " [--output-cpp <cpp>]" << std::endl;
        std::cerr << "\nOptions:" << std::endl;
        std::cerr << "  --input          Path to IOP ELF (e.g. STREAM.IRX)"
                  << std::endl;
        std::cerr << "  --ghidra-input   Path to Ghidra-exported CSV "
                  << "(optional; uses built-in analysis if omitted)"
                  << std::endl;
        std::cerr << "  --output-header  Output header file "
                  << "(default: iop_recompiled.h)" << std::endl;
        std::cerr << "  --output-cpp     Output C++ file "
                  << "(default: iop_recompiled.cpp)" << std::endl;
        return 1;
    }

    // Parse the IOP ELF.
    IopElfData elf_data = parse_iop_elf(elf_path, ghidra_csv_path);

    if (elf_data.functions.empty()) {
        std::cerr << "[IOP-RECOMP] No functions found — aborting." << std::endl;
        return 1;
    }
    std::cout << "[IOP-RECOMP] Found " << elf_data.functions.size()
              << " function(s), " << elf_data.import_stubs.size()
              << " import stub(s)." << std::endl;

    // Recompile.
    IopRecompiler recompiler(elf_data.functions);
    recompiler.set_import_stubs(elf_data.import_stubs);
    recompiler.set_text_data(elf_data.text_data, elf_data.text_size, elf_data.text_base);

    if (!recompiler.recompile_to_files(output_header, output_cpp)) {
        std::cerr << "[IOP-RECOMP] Recompilation failed." << std::endl;
        return 1;
    }

    // Print summary
    std::cout << "\n[IOP-RECOMP] === Summary ===" << std::endl;
    std::cout << "[IOP-RECOMP] Functions:     " << elf_data.functions.size()
              << std::endl;
    std::cout << "[IOP-RECOMP] Import stubs:  " << elf_data.import_stubs.size()
              << std::endl;
    std::cout << "[IOP-RECOMP] Analysis mode: "
              << (ghidra_csv_path.empty() ? "built-in" : "Ghidra CSV")
              << std::endl;
    std::cout << "[IOP-RECOMP] Output:        " << output_header << ", "
              << output_cpp << std::endl;
    std::cout << "[IOP-RECOMP] Done." << std::endl;
    return 0;
}
