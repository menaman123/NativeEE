#pragma once
// The ABI between the engine and a GENERATED VU1 image library (the output of
// Vu1AotEmitter, compiled by Vu1AotBuilder at run time, loaded with the
// platform's dynamic loader). PODs only; the entry point of a library is
//
//     extern "C" const Vu1AotImageExports* vu1_aot_exports_<image hash, 16 hex>();
//
// and the table it returns names, per generated block, the block's entry pc
// and the function that runs it (VuBlock::NativeFn: the block tier's RunOps
// for that one block with every slot a compile-time constant). The block
// table stays the dispatcher: a function runs ONE block and returns the ops it
// executed; the loop reads the branch verdict and picks the successor.
//
// A library is accepted only when every field below agrees with the running
// engine (abi version, slot layout size, image hash, engine build id) AND its
// functions replay the image's whole corpus identically to the interpreter
// (Vu1AotVerifier with the exports installed) -- the same gate the
// specialised tier passes, run again on the loaded code.
#include <cstdint>

#include "engine/vu/tiers/vu_block_tier.h"

constexpr uint32_t kVu1AotAbiVersion = 2;  // 2: flags_lazy (the MAC ring arm the pushes were generated for)
constexpr char kVu1AotExportsPrefix[] = "vu1_aot_exports_";

struct Vu1AotBlockExport {
    uint32_t pc = 0;                      // the block's entry byte address (8-aligned)
    uint32_t n_ops = 0;                   // ops the function runs (the block's ops.size(): the installer's structural check)
    VuBlock::NativeFn fn = nullptr;
};

struct Vu1AotImageExports {
    uint32_t abi_version = 0;             // kVu1AotAbiVersion
    uint32_t slot_size = 0;               // sizeof(VuDecodedSlot) as the generated TU saw it
    uint64_t image_hash = 0;              // VuImageHash::Fast of the image the TU was generated from
    const char* engine_build = nullptr;   // the engine build id the TU was generated for
    const char* compiler = nullptr;       // the compiler that built the TU (its __VERSION__)
    uint32_t block_count = 0;
    const Vu1AotBlockExport* blocks = nullptr;
    // The FlagPipeline MAC-ring representation the generated pushes name
    // (FlagPipeline::kDefaultLazy of the generating build): 1 = lazy, 0 =
    // eager. The installer refuses a unit whose pipeline runs the other arm.
    uint32_t flags_lazy = 0;
};
