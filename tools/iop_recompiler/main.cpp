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

#include "engine/iop/iop_relocator.h"

// ---------------------------------------------------------------------------
// Import table magic word used by IOP modules to declare library imports.
// Each import table starts with this 32-bit value, followed by a version
// word, then the null-terminated library name, then stub entries.
// ---------------------------------------------------------------------------
static constexpr uint32_t IOP_IMPORT_MAGIC = 0x41E00000;
// Export table magic word (RegisterLibraryEntries tables): same 20-byte header,
// then one function address per entry until a zero word.
static constexpr uint32_t IOP_EXPORT_MAGIC = 0x41C00000;

// --legacy-title-stubs: the first title's (STREAM.IRX) hardcoded facts -- its import stub
// address range, its 16 pointer-only function starts, the `la` guard's address bound, the
// PS2_STRMQ probe arms and the 0x128a8 RPC thunk. Default OFF: the generic discovery below
// derives every range from the image itself. With the flag the tool behaves exactly as the
// frozen copy did (byte-identical output for that title).
static constexpr uint32_t IMPORT_STUB_START = 0x127E0;
static constexpr uint32_t IMPORT_STUB_END   = 0x12B70;

struct ToolOptions {
    bool legacy_title_stubs = false;
    uint32_t base = 0;            // --base: load address the module is relocated to
    std::string ns;               // --namespace: wrap the TU in namespace <ns>, route indirect calls via iop_stubs::iop_resolve
    bool import_call = false;     // --import-call: every import -> iop_stubs::import_call("lib", idx, ctx)
    bool quiet = false;           // --quiet: no per-stub / per-label lines (the summary stays)
};
static ToolOptions g_opts;

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
// A table region of .text that is DATA, not code: [start, end) module addresses
// (an import table with its stubs and terminator, or an export table).
struct TableRange {
    uint32_t start;
    uint32_t end;
    std::string kind;   // "import <lib>" / "export <lib>"
};

static std::map<uint32_t, std::string> identify_import_stubs(
        const uint8_t* text_data,
        uint32_t text_size,
        uint32_t text_base,
        std::vector<TableRange>* ranges = nullptr) {
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

            if (!g_opts.quiet)
                std::cout << "[IOP-RECOMP]   stub 0x" << std::hex << stub_addr
                          << " -> " << stub_name << std::dec << std::endl;

            stub_count++;
            stub_offset += 8;
        }

        std::cout << "[IOP-RECOMP]   -> " << stub_count << " stubs registered"
                  << std::endl;
        if (ranges) {
            // The table: header + stubs + the zero terminator pair.
            uint32_t end_offset = stub_offset + 8;
            if (end_offset > text_size) end_offset = text_size;
            ranges->push_back(TableRange{table_addr, text_base + end_offset, "import " + lib_name});
        }

        // Advance the outer loop past this table to avoid re-scanning
        // the stubs as potential magic words.
        offset = stub_offset - 4; // -4 because the for loop does +4
    }

    std::cout << "[IOP-RECOMP] Total import stubs identified: " << stubs.size()
              << std::endl;
    return stubs;
}

// ---------------------------------------------------------------------------
// Export table identification (generic mode): 0x41C00000 header (20 bytes:
// magic, next, version, name[8]) then one function address per entry until a
// zero word. On a relocated image the entries are absolute addresses, so the
// function at module offset 0 (a genuine entry in some modules) is not confused
// with the terminator as long as the base is non-zero. Returns the entries (in
// index order) and records the table's range.
// ---------------------------------------------------------------------------
static std::vector<uint32_t> identify_export_tables(
        const uint8_t* text_data,
        uint32_t text_size,
        uint32_t text_base,
        std::vector<TableRange>* ranges) {
    std::vector<uint32_t> entries;
    if (!text_data || text_size < 20) return entries;
    for (uint32_t offset = 0; offset + 20 <= text_size; offset += 4) {
        if (read_u32_le(text_data + offset) != IOP_EXPORT_MAGIC) continue;
        std::string lib_name;
        for (uint32_t i = 0; i < 8 && text_data[offset + 12 + i] != '\0'; ++i)
            lib_name += static_cast<char>(text_data[offset + 12 + i]);
        const uint32_t version = read_u32_le(text_data + offset + 8);
        uint32_t e = offset + 20;
        int count = 0;
        while (e + 4 <= text_size) {
            const uint32_t fn = read_u32_le(text_data + e);
            if (fn == 0) break;
            entries.push_back(fn);
            ++count;
            e += 4;
        }
        const uint32_t end_offset = std::min(e + 4, text_size);   // + the zero terminator
        std::cout << "[IOP-RECOMP] Export table at 0x" << std::hex << (text_base + offset)
                  << ": library \"" << lib_name << "\" v" << ((version >> 8) & 0xFF) << "."
                  << (version & 0xFF) << std::dec << " -> " << count << " entries" << std::endl;
        if (ranges) ranges->push_back(TableRange{text_base + offset, text_base + end_offset, "export " + lib_name});
        offset = end_offset - 4;
    }
    return entries;
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
        const std::map<uint32_t, std::string>& import_stubs,
        const std::vector<TableRange>& tables = {},
        const std::vector<uint32_t>& export_entries = {},
        const std::vector<ps2r::iop::IopRelocator::Pointer>& pointers = {}) {

    std::map<uint32_t, IopFunction> functions;

    if (!text_data || text_size < 4) return functions;

    // Collect all known import stub addresses into a set for fast lookup
    std::set<uint32_t> stub_addrs;
    for (const auto& [addr, name] : import_stubs) {
        stub_addrs.insert(addr);
    }
    auto in_table = [&](uint32_t a) {
        for (const TableRange& t : tables)
            if (a >= t.start && a < t.end) return true;
        return false;
    };

    // --- Pass 1: Find function starts from JAL targets ---
    std::set<uint32_t> func_starts;
    std::set<uint32_t> weak_starts;   // pointer-derived starts (generic mode): removable by the repair pass

    // The entry point is always a function
    func_starts.insert(entry_point);

    // The first instruction in .text is a function start
    func_starts.insert(text_base);

    if (g_opts.legacy_title_stubs) {
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
    } else {
        // Generic discovery of pointer-only entries (thread entries, RPC
        // handlers, timer / completion callbacks, exported functions): the
        // relocation tables are the exact oracle for every address the module
        // materialises (R_MIPS_32 words, lui/addiu HI16+LO16 pairs). A pointer
        // into .text is accepted as a function start when it is an export-table
        // entry, or when it directly follows a `jr $ra` + delay slot (the
        // property of every genuine function start); a pointer landing elsewhere
        // is a switch-case label (the jr-rX switch over the function's labels
        // covers it) and must NOT split its function.
        // A pointer target is a plausible entry when the previous function ended just
        // before it (`jr $ra` or an unconditional `j` two words back) or it opens a stack
        // frame (`addiu $sp, $sp, -N`); a switch-case label rarely has either shape, and
        // the repair pass below drops the ones that do.
        auto follows_return = [&](uint32_t target) {
            if (target < text_base + 8 || target + 4 > text_base + text_size || (target & 3)) return false;
            const uint32_t before = read_u32_le(text_data + (target - text_base) - 8);
            const uint32_t at = read_u32_le(text_data + (target - text_base));
            if (before == 0x03E00008 || (before >> 26) == 0x02) return true;
            return (at >> 16) == 0x27BD && (at & 0x8000) != 0;   // addiu $sp, $sp, -N
        };
        int from_exports = 0, from_pointers = 0;
        for (uint32_t e : export_entries) {
            if (e >= text_base && e < text_base + text_size && !(e & 3) && !stub_addrs.count(e) && !in_table(e)) {
                if (func_starts.insert(e).second) ++from_exports;
            }
        }
        for (const auto& p : pointers) {
            const uint32_t t = p.target;
            if (t < text_base || t >= text_base + text_size || (t & 3) || stub_addrs.count(t) || in_table(t)) continue;
            if (t == text_base || follows_return(t)) {
                if (func_starts.insert(t).second) {
                    ++from_pointers;
                    weak_starts.insert(t);   // a switch-case label after an early return looks the same: repairable
                }
            }
        }
        std::cout << "[IOP-RECOMP] Built-in analysis: " << from_exports << " function starts from export tables, "
                  << from_pointers << " from relocated pointers after a jr $ra" << std::endl;
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
                if (g_opts.legacy_title_stubs || !in_table(target)) {
                    func_starts.insert(target);
                    weak_starts.erase(target);
                }
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
        if (g_opts.legacy_title_stubs) {
            if (s >= IMPORT_STUB_START && s < IMPORT_STUB_END) continue;
        } else {
            if (in_table(s)) continue;                       // inside an import/export table: data
            if (s >= text_base + text_size) continue;        // outside .text (an entry the image cannot have)
        }
        if (stub_addrs.count(s)) continue;
        filtered_starts.push_back(s);
    }

    // Generic mode: a table region caps the function that precedes it (the
    // table words are data, never emitted as instructions), and the .text tail
    // after the last table is not code unless something starts there.
    std::vector<uint32_t> boundaries;
    if (!g_opts.legacy_title_stubs)
        for (const TableRange& t : tables) boundaries.push_back(t.start);
    std::sort(boundaries.begin(), boundaries.end());

    // Branch / local-jump target of the word at pc (0 = not a branch).
    auto branch_target = [&](uint32_t pc) -> uint32_t {
        const uint32_t w = read_u32_le(text_data + (pc - text_base));
        const uint32_t op = w >> 26;
        if (op == 0x04 || op == 0x05 || op == 0x06 || op == 0x07 ||
            (op == 0x01 && (((w >> 16) & 0x1F) == 0 || ((w >> 16) & 0x1F) == 1 ||
                            ((w >> 16) & 0x1F) == 0x10 || ((w >> 16) & 0x1F) == 0x11)))
            return pc + 4 + (static_cast<uint32_t>(static_cast<int16_t>(w & 0xFFFF)) << 2);
        if (op == 0x02) return (pc & 0xF0000000u) | ((w & 0x03FFFFFFu) << 2);
        return 0;
    };

    // Repair loop (generic mode): a pointer-derived start is WEAK -- a switch-case
    // label placed after an early `jr $ra` is indistinguishable from a function
    // entry by shape. When a branch from the function before it lands inside the
    // weak function's span, the two are one function: drop the weak start and
    // rebuild. Strong starts (entry, JAL targets, export entries) are never dropped.
    for (int repair = 0; repair < 4096; ++repair) {
    functions.clear();
    int dropped = 0;
    for (size_t i = 0; i < filtered_starts.size(); ++i) {
        uint32_t start = filtered_starts[i];
        uint32_t next_start = (i + 1 < filtered_starts.size())
                            ? filtered_starts[i + 1]
                            : text_base + text_size;
        for (uint32_t b : boundaries) {
            if (b > start && b < next_start) { next_start = b; break; }
        }

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

        // Generic mode: a compiler may place blocks AFTER the function's return
        // (cold paths, shared epilogues) -- a branch or local jump from inside
        // [start, end) to [end, next_start) means the function continues there,
        // up to the next return at or past that target. Fixpoint over the body.
        if (!g_opts.legacy_title_stubs) {
            bool grew = true;
            while (grew && best_end < next_start) {
                grew = false;
                for (uint32_t pc = start; pc + 4 <= best_end; pc += 4) {
                    const uint32_t w = read_u32_le(text_data + (pc - text_base));
                    const uint32_t op = w >> 26;
                    uint32_t target = 0;
                    if (op == 0x04 || op == 0x05 || op == 0x06 || op == 0x07 ||
                        (op == 0x01 && (((w >> 16) & 0x1F) == 0 || ((w >> 16) & 0x1F) == 1 ||
                                        ((w >> 16) & 0x1F) == 0x10 || ((w >> 16) & 0x1F) == 0x11))) {
                        target = pc + 4 + (static_cast<uint32_t>(static_cast<int16_t>(w & 0xFFFF)) << 2);
                    } else if (op == 0x02) {   // j
                        target = (pc & 0xF0000000u) | ((w & 0x03FFFFFFu) << 2);
                    } else {
                        continue;
                    }
                    if (target < best_end || target >= next_start) continue;
                    uint32_t new_end = next_start;
                    for (uint32_t end : func_ends) {
                        if (end > target && end <= next_start) { new_end = end; break; }
                    }
                    if (new_end > best_end) {
                        best_end = new_end;
                        grew = true;
                    }
                }
            }
        }

        IopFunction func;
        func.start_addr = start;
        func.end_addr   = best_end;
        func.name        = make_func_name(start);
        functions[start] = func;

        if (!g_opts.legacy_title_stubs && i + 1 < filtered_starts.size() && weak_starts.count(filtered_starts[i + 1])) {
            // Does this function branch past its end into the weak starts that follow it?
            // Every weak start up to the one holding the target is a label of this function
            // -- unless a strong start lies in between (then the branch is not ours).
            uint32_t far_target = 0;
            for (uint32_t pc = start; pc + 4 <= best_end; pc += 4) {
                const uint32_t t = branch_target(pc);
                if (t >= next_start && t < text_base + text_size && t > far_target) far_target = t;
            }
            size_t j = i + 1;
            while (far_target && j < filtered_starts.size() && filtered_starts[j] <= far_target) {
                if (!weak_starts.count(filtered_starts[j])) { far_target = 0; break; }   // a strong start intervenes
                ++j;
            }
            if (far_target && j > i + 1) {
                for (size_t k = i + 1; k < j; ++k) {
                    if (!g_opts.quiet)
                        std::cout << "[IOP-RECOMP] Built-in analysis: 0x" << std::hex << filtered_starts[k]
                                  << " is a label of the function at 0x" << start << std::dec << " (weak start dropped)"
                                  << std::endl;
                    weak_starts.erase(filtered_starts[k]);
                }
                filtered_starts.erase(filtered_starts.begin() + static_cast<long>(i) + 1,
                                      filtered_starts.begin() + static_cast<long>(j));
                ++dropped;
                break;
            }
        }
    }
    if (dropped == 0) break;
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
    std::vector<uint8_t> text_copy;   // the (relocated) .text bytes the recompiler reads
    const uint8_t* text_data = nullptr;
    uint32_t text_size = 0;
    uint32_t text_base = 0;
    uint32_t entry = 0;
    std::vector<TableRange> tables;
    uint32_t load_extent = 0;         // highest allocated address (module-relative)
};

static IopElfData parse_iop_elf(const std::string& elf_path,
                                 const std::string& ghidra_csv_path) {
    IopElfData result;

    // The image bytes: relocated to --base before anything looks at them, so the
    // disassembly, the labels, the jump tables and the pointer discovery all see the
    // addresses the module has in IOP RAM (IopLoader applies the same relocation).
    std::vector<uint8_t> image;
    {
        std::ifstream in(elf_path, std::ios::binary);
        if (!in) {
            std::cerr << "[IOP-RECOMP] Failed to load ELF: " << elf_path << std::endl;
            return result;
        }
        image.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    const uint32_t base = g_opts.base;
    if (!g_opts.legacy_title_stubs) {
        const ps2r::iop::IopRelocator::Report rel = ps2r::iop::IopRelocator::Relocate(image, base);
        if (!rel.ok) {
            std::cerr << "[IOP-RECOMP] Relocation failed: " << rel.error << std::endl;
            return result;
        }
        std::cout << "[IOP-RECOMP] Relocated to base 0x" << std::hex << base << std::dec << ": R_MIPS_32 "
                  << rel.r32 << ", R_MIPS_26 " << rel.r26 << ", HI16/LO16 pairs " << rel.hi16 << std::endl;
        const ps2r::iop::IopRelocator::IopMod mod = ps2r::iop::IopRelocator::ReadIopMod(image.data(), image.size());
        if (mod.ok) {
            std::cout << "[IOP-RECOMP] .iopmod: name \"" << mod.name << "\" version 0x" << std::hex << mod.version
                      << " entry 0x" << mod.entry << " gp 0x" << mod.gp << std::dec << std::endl;
        }
        result.load_extent = ps2r::iop::IopRelocator::LoadExtent(image.data(), image.size());
    } else if (base != 0) {
        std::cerr << "[IOP-RECOMP] --base is not available with --legacy-title-stubs" << std::endl;
        return result;
    }

    ELFIO::elfio reader;
    std::string image_str(image.begin(), image.end());
    std::istringstream image_stream(image_str, std::ios::binary);
    if (!reader.load(image_stream)) {
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

    const uint8_t* text_ptr = reinterpret_cast<const uint8_t*>(text_sec->get_data());
    result.text_size = static_cast<uint32_t>(text_sec->get_size());
    result.text_copy.assign(text_ptr, text_ptr + result.text_size);
    result.text_data = result.text_copy.data();
    result.text_base = static_cast<uint32_t>(text_sec->get_address()) + base;
    result.entry = static_cast<uint32_t>(reader.get_entry()) + base;

    std::cout << "[IOP-RECOMP] .text at 0x" << std::hex << result.text_base
              << ", size 0x" << result.text_size << std::dec << " ("
              << (result.text_size / 4) << " instructions)" << std::endl;

    if (data_sec) {
        std::cout << "[IOP-RECOMP] .data at 0x" << std::hex
                  << (data_sec->get_address() + base) << ", size 0x"
                  << data_sec->get_size() << std::dec << std::endl;
    }
    if (rodata_sec) {
        std::cout << "[IOP-RECOMP] .rodata at 0x" << std::hex
                  << (rodata_sec->get_address() + base) << ", size 0x"
                  << rodata_sec->get_size() << std::dec << std::endl;
    }
    if (bss_sec) {
        std::cout << "[IOP-RECOMP] .bss at 0x" << std::hex
                  << (bss_sec->get_address() + base) << ", size 0x"
                  << bss_sec->get_size() << std::dec << std::endl;
    }

    // --- Import stub identification ---
    result.import_stubs = identify_import_stubs(
        result.text_data, result.text_size, result.text_base, &result.tables);

    // --- Function boundary analysis ---
    if (!ghidra_csv_path.empty()) {
        // Option A: Ghidra headless CSV
        std::cout << "[IOP-RECOMP] Using Ghidra CSV for function boundaries"
                  << std::endl;
        result.functions = load_ghidra_csv(ghidra_csv_path);
    } else if (g_opts.legacy_title_stubs) {
        // Option B: Built-in analysis (the frozen tool's: no export/pointer discovery)
        std::cout << "[IOP-RECOMP] Using built-in analysis for function boundaries"
                  << std::endl;
        result.functions = builtin_analysis(
            result.text_data, result.text_size, result.text_base,
            result.entry, result.import_stubs);
    } else {
        std::cout << "[IOP-RECOMP] Using built-in analysis for function boundaries (generic discovery)"
                  << std::endl;
        const std::vector<uint32_t> exports =
            identify_export_tables(result.text_data, result.text_size, result.text_base, &result.tables);
        const std::vector<ps2r::iop::IopRelocator::Pointer> pointers =
            ps2r::iop::IopRelocator::Pointers(image.data(), image.size());
        std::cout << "[IOP-RECOMP] Relocation tables materialise " << pointers.size() << " addresses" << std::endl;
        result.functions = builtin_analysis(
            result.text_data, result.text_size, result.text_base,
            result.entry, result.import_stubs, result.tables, exports, pointers);
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
        } else if (arg == "--legacy-title-stubs") {
            g_opts.legacy_title_stubs = true;
        } else if (arg == "--base" && i + 1 < argc) {
            g_opts.base = static_cast<uint32_t>(std::stoul(argv[++i], nullptr, 0));
        } else if (arg == "--namespace" && i + 1 < argc) {
            g_opts.ns = argv[++i];
        } else if (arg == "--import-call") {
            g_opts.import_call = true;
        } else if (arg == "--quiet") {
            g_opts.quiet = true;
        } else {
            std::cerr << "[IOP-RECOMP] Unknown argument: " << arg << std::endl;
            return 1;
        }
    }

    if (elf_path.empty()) {
        std::cerr << "Usage: " << argv[0]
                  << " --input <iop_elf>"
                  << " [--ghidra-input <csv>]"
                  << " [--output-header <header>]"
                  << " [--output-cpp <cpp>]"
                  << " [--base <addr>] [--namespace <id>] [--import-call] [--legacy-title-stubs]" << std::endl;
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
        std::cerr << "  --base <addr>    Relocate the IRX to this load address (its .rel.* tables applied) before\n"
                     "                   recompiling; every emitted address is the relocated one" << std::endl;
        std::cerr << "  --namespace <id> Wrap the output in namespace <id>; indirect calls (jalr, jr to another\n"
                     "                   function, J/JAL to an unknown target) go through iop_stubs::iop_resolve(addr)" << std::endl;
        std::cerr << "  --import-call    Every library import -> iop_stubs::import_call(\"lib\", index, ctx)" << std::endl;
        std::cerr << "  --quiet          No per-stub / per-label lines (the summary stays)" << std::endl;
        std::cerr << "  --legacy-title-stubs  The frozen tool's first-title facts (its stub range, pinned function\n"
                     "                   starts, probe arms, RPC thunk); byte-identical output for that title" << std::endl;
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
    IopRecompiler::Options ropts;
    ropts.legacy_title_stubs = g_opts.legacy_title_stubs;
    ropts.ns = g_opts.ns;
    ropts.import_call = g_opts.import_call;
    ropts.base = g_opts.base;
    ropts.entry = elf_data.entry;
    ropts.load_extent = elf_data.load_extent;
    ropts.input_name = std::filesystem::path(elf_path).filename().string();
    recompiler.set_options(ropts);

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
    if (!g_opts.legacy_title_stubs) {
        const IopRecompiler::Census& c = recompiler.census();
        std::cout << "[IOP-RECOMP] Base:          0x" << std::hex << g_opts.base << std::dec << std::endl;
        std::cout << "[IOP-RECOMP] Namespace:     " << (g_opts.ns.empty() ? "(global)" : g_opts.ns) << std::endl;
        std::cout << "[IOP-RECOMP] Import calls:  " << c.import_calls << " sites ("
                  << (g_opts.import_call ? "iop_stubs::import_call" : "named iop_stubs:: forwarders") << ")" << std::endl;
        std::cout << "[IOP-RECOMP] Unimplemented: " << c.unimplemented << " instruction(s)";
        if (!c.unimplemented_opcodes.empty()) {
            std::cout << " --";
            for (const auto& [name, n] : c.unimplemented_opcodes) std::cout << " " << name << "x" << n;
        }
        std::cout << std::endl;
        std::cout << "[IOP-RECOMP] Indirect:      jalr " << c.jalr_sites << ", jr-switch " << c.jr_switch_sites
                  << ", tail-J " << c.tail_j_sites << ", unresolved JAL " << c.unresolved_jal << std::endl;
        std::cout << "[IOP-RECOMP] Branch-in-delay-slot warnings: " << c.branch_in_delay_slot << std::endl;
        if (c.unimplemented > 0) {
            std::cerr << "[IOP-RECOMP] ERROR: " << c.unimplemented
                      << " instruction(s) the emitter does not implement (listed above) -- refusing the output"
                      << std::endl;
            return 1;
        }
        if (c.unresolved_jal > 0) {
            std::cerr << "[IOP-RECOMP] WARNING: " << c.unresolved_jal
                      << " JAL site(s) target no discovered function (resolved at run time through iop_resolve)"
                      << std::endl;
        }
    }
    std::cout << "[IOP-RECOMP] Output:        " << output_header << ", "
              << output_cpp << std::endl;
    std::cout << "[IOP-RECOMP] Done." << std::endl;
    return 0;
}
