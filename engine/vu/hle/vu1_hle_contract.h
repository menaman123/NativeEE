#pragma once
// The VU1 HLE contract: what a native replacement of one VU1 microprogram run
// (one MSCAL/MSCNT entry to E-bit exit) receives and what it produces.
//
// PS2 rule: a microprogram reads the 16 KB data memory, the VIF1 TOP/TOPS/
// ITOP/ITOPS registers and the register file at entry, XGKICKs GIF PATH1
// packets in program order and leaves data memory / registers behind for the
// next program. A native program reproduces exactly those observables: the
// raw packet bytes (so one decoder lowers both the HLE and the interpreter
// output), the final data-memory writes and, when it claims `has_regs`, the
// whole register file. `false` from a program = DECLINE: the caller falls
// back to the microcode tier with nothing changed. Zero primary types (PODs
// and a function-pointer alias); no title constant lives here.
#include <cstdint>
#include <vector>

#include "engine/ee/cpu/cpu_state.h"

constexpr uint32_t kVu1HleMicroSize = 16 * 1024;
constexpr uint32_t kVu1HleDataSize = 16 * 1024;

struct Vu1ImageInfo;

struct Vu1HleInput {
    const uint8_t* micro;  // 16 KB, read-only (dispatch-table walks)
    const uint8_t* data;   // 16 KB VU1 data memory at entry
    uint32_t start_pc;     // MSCAL/MSCNT entry pc (byte address)
    uint32_t top, tops, itop, itops;  // VIF1 double-buffer registers
    const VUregs* regs;    // architectural VU1 registers at entry
    uint64_t image_hash;   // VuImageHash::Fast over the whole micro memory
    // The analysis of this image (the loaded-program layout a program keys its
    // sub bodies on); null when the caller has none (image pins apply then).
    const Vu1ImageInfo* info = nullptr;
};

struct Vu1HleOutput {
    // One complete GIF packet (GIFtag chain ending in EOP) per XGKICK, in order.
    std::vector<std::vector<uint8_t>> kicks;
    // Final data-memory writes a LATER program depends on (complete diffs).
    struct DataWrite {
        uint32_t addr;
        std::vector<uint8_t> bytes;
    };
    std::vector<DataWrite> data_writes;
    // true: the full post-run register file (TPC included) is reproduced and
    // installed; false: registers are not reproduced and the runtime parks the
    // unit at the family's exit TPC.
    bool has_regs = false;
    VUregs regs{};
};

// false = DECLINE (unexpected layout) -> fallback tier, state untouched.
using Vu1HleFn = bool (*)(const Vu1HleInput&, Vu1HleOutput&);

// One loadable program found in an image: where it starts and which catalog
// row (Vu1ProgramLibrary::programs index) it is.
struct Vu1LoadedAt {
    uint16_t pc;       // byte address of the program's first bundle
    uint16_t program;  // index into the library's loaded-program catalog
};

// The image analysis every lookup and the catalog share.
struct Vu1ImageInfo {
    uint64_t image_hash;        // whole 16 KB
    uint64_t prefix_hash;       // micro[0 .. variant_base)
    uint64_t variant_hash;      // micro[variant_base .. extent), 0 when the image ends before it
    uint32_t extent;            // last non-zero byte + 8, rounded to a bundle
    bool prefix_is_family;      // prefix equals the first family dispatcher image analysed
    std::vector<uint16_t> dispatch_chain;  // byte pcs, in call order
    bool dispatch_valid;        // the chain reached an exit stub
    // The loaded-program layout: the catalog programs found back to back from
    // variant_base (the title loads its material programs contiguously there;
    // what follows the last one is stale memory from earlier uploads).
    // layout_hash = VuImageHash::Fast over micro[variant_base .. loaded_end),
    // 0 when no catalog program starts at variant_base -- a key that does not
    // change with the stale tail, unlike variant_hash.
    std::vector<Vu1LoadedAt> layout;
    uint32_t loaded_end;        // byte address past the last identified program (variant_base when none)
    uint64_t layout_hash;
};
