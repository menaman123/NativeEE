#pragma once
// The VU cached tier: the interpreter's bundle loop over a PREDECODED image,
// with three elisions that are each the identity on the observable state
// (registers, data memory, the clock, the kicks): fast instruction bodies
// keyed on the decoded kinds, the statically classed stall model, and the
// superblock run body that skips the stall model where the span classifier
// proved no read-after-write hazard and rebuilds the writeback ring at the
// span close. The interpreter tier is the reference; identity is proven by
// the differential tests and the corpus compare, never assumed.
//
// PS2 rules carried here (over the interpreter's): a decoded image is valid
// until a micro-memory store (the unit's epoch) or a different image under the
// same epoch; a misaligned pc executes the bundle decoded on the fly; the
// FMAC writeback rules are those of the stall model (issue + 4).
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/vu/vu_decoder.h"

class VuUnit;
class VuInterpreterTier;

// One decoded microprogram image (the cache entry). Keyed by the image hash;
// `generation` is the unit epoch it was last verified against.
struct VuCachedProgram {
    uint64_t hash = 0;
    uint64_t generation = 0;
    uint64_t exec_count = 0;  // eviction key (least executed goes first)
    uint64_t bundles = 0;     // bundles retired under this image
    std::vector<VuDecodedSlot> slots;  // one per 8-byte bundle of the unit's micro memory
    std::vector<uint8_t> span_rem;     // parallel: foldable bundles remaining from each slot
};

class VuCachedTier {
public:
    static constexpr size_t kTableCap = 64;
    static constexpr uint32_t kPcMask = 0x3FFF;
    static constexpr int kMinRunLen = 3;      // a run needs 2 elided bundles before its terminal
    static constexpr int kWarmupBundles = 4;  // run positions that always consult the stall model
    static constexpr uint32_t kMinSpan = 4;   // shortest span executed without the model

    // `generic` supplies the reference bodies for kinds without a fast form
    // and the program-end drain.
    explicit VuCachedTier(VuInterpreterTier& generic, uint64_t cycle_cap = 1000000);

    // Run the unit's microprogram from `start_pc` (byte address) to its end.
    void Execute(VuUnit& unit, uint32_t start_pc);
    void Execute(VuUnit& unit, uint32_t start_pc, uint64_t cycle_limit);
    // A parked VU0 (self-spin suspend) resumes at its recorded bundle.
    void ResumeIfWaiting(VuUnit& unit);

    // The decoded image for the unit's current micro memory (epoch fast path,
    // hash lookup, decode on a miss; the least-executed entry evicted at cap).
    const VuCachedProgram* Acquire(VuUnit& unit);

    uint64_t cycle_cap() const { return cycle_cap_; }
    void set_cycle_cap(uint64_t cap) { cycle_cap_ = cap; }
    // Bundles retired across every run of this tier (the catalog's exec count).
    uint64_t bundles() const { return bundles_; }
    size_t table_size() const { return table_.size(); }
    const VuCachedProgram* current() const { return current_; }

private:
    VuCachedProgram* Lookup(VuUnit& unit);
    void DecodeProgram(const VuUnit& unit, VuCachedProgram& prog) const;
    void Loop(VuUnit& unit, uint32_t pc, const VuCachedProgram& prog, uint64_t cycle_limit);
    // The two words of one bundle in issue order (the interpreter's hazards).
    void ExecBundle(VuUnit& unit, const VuDecodedSlot& s, uint32_t pc, bool& branch_taken, uint32_t& branch_target);
    void ExecUpperFast(VuUnit& unit, const VuDecodedSlot& s);
    // The two FMAC families of the upper word (fd-writing results; the ACC accumulators).
    static void ExecUpperFmac(VuUnit& unit, const VuDecodedSlot& s);
    static void ExecUpperAcc(VuUnit& unit, const VuDecodedSlot& s);
    void ExecLowerFast(VuUnit& unit, const VuDecodedSlot& s, uint32_t pc, bool& branch_taken, uint32_t& branch_target);
    void DrainAll(VuUnit& unit);

    VuInterpreterTier& generic_;
    uint64_t cycle_cap_;
    uint64_t bundles_ = 0;
    std::vector<std::unique_ptr<VuCachedProgram>> table_;
    VuCachedProgram* current_ = nullptr;
};
