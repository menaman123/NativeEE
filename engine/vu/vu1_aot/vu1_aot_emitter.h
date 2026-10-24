#pragma once
// Vu1AotEmitter -- the VU1 code generator: C++ for a VERIFIED image, one
// function per block of the block tier's table. Each function is the block
// tier's RunOps over that block's ops with EVERY slot a `static constexpr
// VuDecodedSlot` and every body the engine's own template instantiation
// (VuCachedTier::ExecUpperK<K, D> / ExecLowerK<K, D>, StallModel::ApplySpec<M>,
// the FlagPipeline ops) called with the constant slot, so the compiler folds
// the fields -- registers, immediates, lane masks, hazard arms, the stall class
// -- into straight-line code with no per-op dispatch and no indirect call. No
// op semantics are re-implemented here: the emitted text names the bodies the
// block tier would call and the arm it would take (I bit / hide-upper /
// plain), op by op, with the same bookkeeping (clock, stall entry, kick pacing,
// hard-wired pin, kick re-read, XTOP yield). The block table stays the
// dispatcher: a function runs ONE block and returns the ops it executed; the
// loop reads the branch verdict and chooses the successor (JR / JALR targets,
// the E bit, the delay slot and the cycle cap are the loop's as before), so
// there is no giant switch and no full-extent seeding -- only the blocks named
// are emitted (ReachableBlocks: the closure of the corpus entry pcs and the
// blocks the verifier's native legs built, over fall-through, static targets
// and the wrapped delay slot).
//
// The TU exports ONE `extern "C"` entry, `vu1_aot_exports_<hash>()`, returning
// the Vu1AotImageExports table (abi version, slot size, image hash, engine
// build, compiler, the blocks). It includes the prelude only
// (engine/vu/vu1_aot/vu1_aot_prelude.h) and compiles outside the tree against a
// copy of the engine headers the prelude pulls (the builder stages them next to
// the cache). Pure: strings in, string out; no files, no process.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/vu/vu1_aot/vu1_aot_block_gen.h"

struct VuBlockProgram;
struct VuBlock;
struct VuDecodedSlot;

class Vu1AotEmitter {
public:
    struct Options {
        std::string engine_build;   // the g_build_id string the table carries
        std::string note;           // free text for the TU's header comment (the corpus, the toolchain)
        Vu1AotBlockGen::Policy region;  // the region / chaining policy of every function of the TU
    };
    struct Output {
        std::string source;         // the TU
        std::string entry_symbol;   // vu1_aot_exports_<hash16>
        uint32_t blocks = 0;        // functions emitted
        uint64_t ops = 0;           // slots emitted (bodies inlined)
    };

    // The blocks to emit for `prog` (a decoded program; the ones named are
    // built if absent): the closure of `entry_pcs` over each block's
    // successors (fall-through, static target, the delay slot wrapped to 0),
    // at most `cap` blocks, sorted by pc; NOP-pair blocks are walked through
    // but not emitted (the loop's sled rule owns them). JR / JALR targets are
    // not inferred: pass the pcs the verifier's native legs built as entries.
    static std::vector<uint32_t> ReachableBlocks(VuBlockProgram& prog, const std::vector<uint32_t>& entry_pcs, size_t cap);

    // The TU for the given blocks of `prog` (each built if absent).
    static Output Emit(VuBlockProgram& prog, const std::vector<uint32_t>& block_pcs, const Options& opt);

    static std::string EntrySymbol(uint64_t image_hash);
    static std::string Hex16(uint64_t v);

    // One block's REGION function text (Vu1AotBlockGen; `emitted` = the sorted
    // pcs the TU holds slots for -- public for the structural test / the
    // notes' excerpt), and the block's slots as constexpr objects named by
    // its pc (one set per block of the TU; a region names its blocks' slots).
    static std::string EmitBlock(VuBlockProgram& prog, const VuBlock& blk, const std::string& fn_name,
                                 const std::vector<uint32_t>& emitted, const Vu1AotBlockGen::Policy& policy = Vu1AotBlockGen::Policy{});
    // The policy as the TU's header comment / the census name it: "blocks=<n>:ops=<n>:taken=<0|1>:chain=<0|1>".
    static std::string PolicyName(const Vu1AotBlockGen::Policy& policy);
    static std::string EmitBlockSlots(const VuBlock& blk);
    // One slot as a constexpr initialiser (every field by name).
    static std::string EmitSlot(const VuDecodedSlot& s, const std::string& name);
};
