#pragma once
// The VU block tier: the cached tier's bundle semantics over PRE-RESOLVED
// straight-line blocks. A block is the run of bundles from an entry address to
// the next control boundary (a branch, the E bit, a NOP pair, the image end,
// or the block cap) plus the terminal's delay slot; each op of a block carries the
// resolved upper and lower bodies (the kind -> body tables of the cached
// tier), the resolved stall-model entry for its static class, the same-bundle
// hazard bits, and the block's STATIC STALL SCHEDULE: the read-after-write
// stall each op takes under the stall model with no pre-block writeback in
// flight. The loop charges the clock, paces a pending XGKICK, executes the two
// words and pins VF00/VI00 per op; the branch / delay-slot / E-bit / yield
// bookkeeping runs once per block at its terminal.
//
// Stall model: consulted per op (dynamic mode) until it has agreed with the
// static schedule on two consecutive ops, the next op being at least the
// fourth of the block; from there the schedule is applied by addition (static
// mode) until a dynamic op (one whose stall the model alone can know: an FDIV
// / EFU wait, a branch's IALU read, an unclassed bundle), the block end, or a
// cycle-cap cut. Exactness: an op's FMAC stall depends only on the stalls of
// the two ops before it and on the writes of the three ops before it
// (writeback latency 4), and a pre-block writeback is never in flight past
// the third op; so two consecutive agreements make every later non-dynamic
// op agree. Leaving static mode rebuilds the FMAC ring from the last three
// ops' recorded issue cycles (the drain's flush and the next consult see what
// the model would hold); ILW / ILWR writebacks are queued live in both modes.
//
// MAC / STATUS / CLIP flags: no bookkeeping is skipped. Under the VU rules
// the STATUS sticky bits accumulate every retired FMAC result until FSSET
// rewrites them and the registers are observable across programs (and every
// program of the corpus reads them), so a per-block "no reader" fact never
// makes a result's flags dead; the lazy ring (FlagPipeline) is already the
// deferred representation.
//
// The interpreter tier is the reference and the cached tier the second
// oracle; identity is proven by the differential tests and the corpus
// compare, never assumed. VU0 does not run here (the dispatch keeps it on the
// cached tier: the self-spin park and the libvu0 substitution live there); a
// misaligned entry pc runs on the interpreter.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "engine/vu/stall_model.h"
#include "engine/vu/tiers/vu_cached_tier.h"
#include "engine/vu/vu_decoder.h"

class VuUnit;
class VuInterpreterTier;

// One pre-resolved bundle of a block.
struct VuBlockOp {
    enum : uint16_t {
        kIBit = 1,       // the lower word is an immediate (upper reads the old I, then I = lower)
        kHideUpper = 2,  // the lower reads the VF the upper writes: it sees the old value
        kTouchHw = 4,    // the bundle may write VF0 / VI0: re-pin after it
        kIaluWr = 8,     // ILW / ILWR: queue the VI writeback (static mode)
        kXtop = 16,      // XTOP: the yield rule is checked after the op
        kDynamic = 32,   // the stall model alone knows this op's stall
        kNopPair = 64,   // both words zero (the NOP-sled rule)
        kEBit = 128,     // the E bit: the block's terminal ends the program after one more bundle
        kXgkick = 256    // XGKICK: the kick's pacing deadline is re-read after the op
    };
    // What the stall model does for the bundle in dynamic mode.
    enum : uint8_t { kStallNone = 0, kStallPush = 1, kStallModel = 2 };
    using StallFn = void (*)(StallModel&, const VuDecodedSlot&, VUregs&, FlagPipeline&);

    const VuDecodedSlot* slot = nullptr;
    VuCachedTier::UpperFn upper = nullptr;  // null: upper NOP (the ACC re-clamp only)
    VuCachedTier::LowerFn lower = nullptr;  // null: no lower issues (NOP, I bit, discarded)
    StallFn stall = nullptr;                // the stall model entry for the bundle's static class
    uint8_t s_delta = 0;                    // static schedule: stall cycles before this op issues
    uint16_t flags = 0;
    uint8_t ialu_vi = 0;                    // VI of the ILW / ILWR writeback (kIaluWr)
    uint8_t upper_vf_write = 0;             // the VF the upper writes (kHideUpper)
    uint8_t stall_kind = kStallModel;       // kStallNone: nothing to do; kStallPush: queue its VF writes only
};

// One straight-line block: `n_body` bundles up to and including the terminal
// (a branch, the E bit, a NOP pair, the bundle before a NOP pair, the image's
// last bundle, or the op cap), then (has_ds) the terminal's delay-slot bundle
// unless it would wrap past the image end: it is the same bundle whether the
// branch is taken or not, its own branch / E bit are ignored and it does not
// advance TPC. `fall` / `jump` are the successor blocks, resolved on first
// use (the program owns every block).
struct VuBlock {
    uint32_t pc = 0;            // entry byte address
    int n_body = 0;             // ops up to and including the terminal
    int branch_idx = -1;        // op whose branch verdict counts (the terminal when it is a branch kind)
    bool has_ds = false;        // ops[n_body] is the merged delay slot
    bool e_bit = false;         // the terminal ends the program after its delay slot
    int32_t static_target = -1; // B / BAL / IBxx: the target address; -1 = JR / JALR (read at run time)
    VuBlock* fall = nullptr;    // the block after this one (fall-through / after the delay slot)
    VuBlock* jump = nullptr;    // the block at `static_target`
    std::vector<VuBlockOp> ops;
};

// How the builder resolves an op's two bodies from its decoded slot: the
// block tier's own ResolveOp (the dynamic kind -> body tables) unless a tier
// installs another for the program (the AOT tier's (kind, dest)-specialised
// bodies, engine/vu/vu1_aot/). A resolver fills every VuBlockOp field but
// `s_delta` (the schedule's).
using VuBlockOpResolver = void (*)(VuBlockOp& op, const VuDecodedSlot& s);

// One decoded image with its block table (built lazily per entry address).
struct VuBlockProgram {
    uint64_t hash = 0;
    uint64_t generation = 0;
    uint64_t exec_count = 0;
    uint64_t bundles = 0;
    VuBlockOpResolver resolve = nullptr;             // null = VuBlockTier::ResolveOp
    std::vector<VuDecodedSlot> slots;                // one per 8-byte bundle of the unit's micro memory
    std::vector<VuBlock*> block_at;                  // slot -> its block, null = none yet
    std::vector<std::unique_ptr<VuBlock>> blocks;
};

class VuBlockTier {
public:
    static constexpr size_t kTableCap = 64;
    static constexpr uint32_t kPcMask = 0x3FFF;
    static constexpr int kMaxBlockOps = 128;   // a longer run continues as the next block
    static constexpr int kWarmupOps = 3;       // ops before a pre-block writeback is provably retired
    static constexpr int kMatchStreak = 2;     // model/schedule agreements that make the schedule exact
    static constexpr int kRingWindow = 3;      // bundles whose writebacks can still be in flight
    static constexpr int kMinStaticOps = 3;    // ops left in the block for static mode to be worth its ring rebuild

    struct Stats {
        uint64_t blocks_run = 0;    // block executions (a delay slot counts one)
        uint64_t ops_static = 0;    // ops whose stall came from the schedule
        uint64_t ops_model = 0;     // ops that consulted the stall model
        uint64_t blocks_built = 0;
        uint64_t runs = 0;          // programs run (one steady-clock pair each: the census's ns per bundle)
        uint64_t run_ns = 0;        // wall time inside Run
    };

    // `generic` supplies the drain and the GENERIC bodies through the cached tier's tables.
    explicit VuBlockTier(VuInterpreterTier& generic, uint64_t cycle_cap = 1000000);

    // Run the unit's microprogram from `start_pc` (byte address) to its end.
    void Execute(VuUnit& unit, uint32_t start_pc);
    void Execute(VuUnit& unit, uint32_t start_pc, uint64_t cycle_limit);
    // The same run on a program the caller Acquired for this unit (VU1, `pc`
    // 8-aligned and masked): the AOT tier's entry after its status read.
    void ExecuteAcquired(VuUnit& unit, uint32_t pc, VuBlockProgram& prog);

    // The program for the unit's current micro memory (epoch fast path, hash
    // lookup, decode on a miss; the least-executed entry evicted at cap).
    VuBlockProgram* Acquire(VuUnit& unit);
    // The block entered at `pc` (8-aligned) of that program, built on first use.
    VuBlock* BlockAt(VuBlockProgram& prog, uint32_t pc);
    // Install `resolve` (null = ResolveOp) on the program and re-resolve every
    // block built so far (the schedule is recomputed: it depends on the slots
    // only). Blocks built later use it too.
    void SetResolver(VuBlockProgram& prog, VuBlockOpResolver resolve);

    uint64_t cycle_cap() const { return cycle_cap_; }
    void set_cycle_cap(uint64_t cap) { cycle_cap_ = cap; }
    // Bundles retired across every run of this tier (the catalog's exec count).
    uint64_t bundles() const { return bundles_; }
    size_t table_size() const { return table_.size(); }
    const Stats& stats() const { return stats_; }

    // The per-op facts from a decoded slot (the block builder's step; public
    // for the structural tests). `s_delta` is left to Schedule.
    static void ResolveOp(VuBlockOp& op, const VuDecodedSlot& s);
    // The static stall schedule over a block's ops (s_delta), from an empty ring.
    static void Schedule(VuBlockOp* ops, size_t n);
    // The FMAC read-after-write stall an op takes given the three writer
    // bundles before it (null = none; back1 the nearest) and the stalls the
    // two ops before it took: the recurrence the schedule and the loop's
    // agreement test rest on.
    static uint32_t StaticStall(const VuDecodedSlot& s, const VuDecodedSlot* back1, const VuDecodedSlot* back2,
                                const VuDecodedSlot* back3, uint32_t stall_back1, uint32_t stall_back2);
    // Does the block end at this slot (the slot is its terminal)? A generic
    // lower word is not one: every branch opcode has a fast kind, so the
    // generic body never branches.
    static bool IsTerminal(const VuDecodedSlot& s);

private:
    VuBlockProgram* Lookup(VuUnit& unit);
    void DecodeProgram(const VuUnit& unit, VuBlockProgram& prog) const;
    VuBlock* Build(VuBlockProgram& prog, size_t slot_idx);
    // The run proper: the per-run unit state, the loop, the program's bundle count.
    void Run(VuUnit& unit, uint32_t pc, VuBlockProgram& prog, uint64_t cycle_limit);
    void Loop(VuUnit& unit, uint32_t pc, VuBlockProgram& prog, uint64_t cycle_limit);
    // `n` ops of a block from `pc`: clock, stalls, pacing, the two words, the
    // VF00/VI00 pin; op `branch_idx`'s verdict lands in `taken`/`target`.
    // Returns the number of ops executed (< n only on an XTOP yield, which
    // leaves `xtop_exit_requested` set for the caller). kSched carries the
    // static-schedule machinery; a block too short to ever enter static mode
    // runs without it.
    template <bool kSched>
    int RunOps(VuUnit& unit, const VuBlockOp* ops, int n, uint32_t pc, int branch_idx, bool& taken, uint32_t& target,
               Stats& acc);
    void DrainAll(VuUnit& unit);
    void FoldStats(const Stats& acc);

    VuInterpreterTier& generic_;
    uint64_t cycle_cap_;
    uint64_t bundles_ = 0;
    Stats stats_;
    std::vector<std::unique_ptr<VuBlockProgram>> table_;
    VuBlockProgram* current_ = nullptr;
};
