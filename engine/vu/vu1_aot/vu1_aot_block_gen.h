#pragma once
// Vu1AotBlockGen -- the register-allocating half of the VU1 code generator:
// the C++ text of ONE REGION function for Vu1AotEmitter.
//
// A region is the block the function is named for (its head) plus the blocks
// reachable from it over fall-through and STATIC branch targets, up to an op
// budget: a vertex loop (a block whose branch targets its own head) runs
// inside one function, its registers resident across the iterations, and a
// chain of fall-through blocks runs without returning to the dispatcher. Every
// reachable block still gets its own function (its own region), so the block
// table stays the dispatcher: it calls the function of the block it is at and,
// on return, re-syncs to the block whose END the function reached
// (Vu1State::native_exit_pc; `taken` / `target` are that block's verdict) or
// whose HEAD it stopped at when the op budget (Vu1State::native_budget, the
// loop's cycle cap) would not hold the next block (native_resume: that block
// has not run). Inside the function each block is `L_<pc>: { ... }` and an
// edge is `goto`; a block's end writes TPC as the loop does and either jumps,
// falls into the next block, or exits (E bit, JR / JALR, a branch whose delay
// slot wrapped, a target outside the region, the budget).
//
// The VU register file is RESIDENT IN LOCALS for the whole function: one
// `VR_reg vN` per VF the region touches (loaded at entry, stored back at every
// exit when written), one `uint16_t iN` per VI, `acc`; VF00 is the constant
// {0,0,0,1} and VI00 is 0 (no fast body writes them; a GENERIC word runs
// against the unit's memory with a flush / reload around it and the
// hard-wired pin after). The host compiler does the SSA and the register
// allocation; the generator does what the compiler cannot prove: the CLAMP
// CACHE -- every FMAC source is read clamped (ClampBits1 is idempotent), so
// `vNc` holds the clamped view of vN, recomputed only when vN changes and is
// not known clamped (a full-mask FMAC store leaves the register clamped; a
// partial one keeps it clamped when it was), and the ACC is re-clamped ONCE
// per block at its first upper word. A label is a fresh entry: nothing is
// assumed about the caches, and the stall accounting starts over (the
// incoming edge materialised its last <= 3 ops' writes into the model, the
// first three ops after the label consult it, later ops fold the in-block
// read-after-write stalls from the issue-cycle locals).
//
// The text of an op is the body of ExecUpperK / ExecLowerK (vu_cached_tier_
// exec.inl) with the operands named: the VuFloat lane kernels, vu_cached_exec's
// helpers (WriteMaskClampD<D>, QwAddr, Le32, StoreLe32, IlwOffset), the
// FlagPipeline entries, XgKick::Arm, the interpreter's inline judgements for
// CLIP (ClipJudge) and DIV (DivQuotient), and -- for the GENERIC words the
// decoder has no fast kind for -- either a native body of the interpreter's
// expression on the resident registers (the Q / I FMAC and ACC forms, OPMULA /
// OPMSUB, ABS, MAXi; MFP, WAITP and the lane-arithmetic EFU ops) or the
// interpreter's body itself with a flush / reload around it (ClassifyGeneric*
// in the .cpp). The same-bundle rules are kept op for op (the lower sees the
// pre-upper VF as `pre`, the I bit loads I after the upper read it, a
// discarded lower does not issue). No lane
// rule is restated here: the corpus (regs, data, clock, bundles, kicked bytes
// vs the interpreter) is the oracle and the runtime gate re-verifies every
// library before install.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct VuBlock;
struct VuBlockOp;
struct VuBlockProgram;
struct VuDecodedSlot;

// The region policy -- a parameter of the GENERATION, not of the ABI (a
// library built under any policy is binary-compatible with the engine and
// passes the same corpus gate; the pack tool, the run-time builder and the
// corpus test take it as an option).
struct Vu1AotRegionPolicy {
    int block_cap = 1;          // blocks a region holds at most (the head always)
    int op_cap = 48;            // ops a region holds at most (the head block always)
    bool taken_targets = true;  // grow over static branch targets too (not only fall-through)
    // Block CHAINING: a block end whose successor is a block of the TU but
    // not of this region hands over to that block's own function with a
    // tail call (`VU1_AOT_MUSTTAIL return Blk_<pc>(..., done)`: the
    // registers stored back, the stall model made whole, `done` carried)
    // instead of returning to the dispatcher. The successor's head check
    // is the budget check the dispatcher would have made (it exits with
    // native_resume when the cap will not hold it); a NOP-pair block, a
    // wrapped delay slot, an E-bit end, a JR / JALR and a block outside the
    // TU still return to the loop. Nothing is duplicated: every block has
    // one function, the chain is a jump between them.
    bool chain = false;
    // MAC flag ACCUMULATION in registers: a result the block retires for sure
    // at every point that could observe it is folded into a block-local
    // FlagPipeline::LazyAcc as it is produced (its lanes die at its op) and
    // the accumulator is delivered once per observation point
    // (FoldLazyBatch), instead of every result staying live to the block's
    // edge for its own FoldLazyRetired (a load / OR / store round trip per
    // result and the spills of keeping them). The lazy ring only; the eager
    // one keeps the per-result form. Same sticky bits, same last-retired word.
    // Default ON since STAGE PERF-1 (LazyAcc's OR / MAX / MIN form: six NEON
    // ops per result, no per-result load / OR / store round trip).
    bool mac_acc = true;
    // STAGE PERF-1 (the register-resident shape; both default on, the A/B
    // knobs of the title's generator):
    // store_back_inline: the store-back of the written registers is emitted
    // IN PLACE at every exit / chain instead of through a `[&]` lambda. The
    // lambda captured every local by reference, so the compiler materialised
    // a capture table of their addresses at function entry and kept every
    // VF / VI / ACC local IN MEMORY for the whole function (an escaped local
    // cannot live in a register across a call it may be read by): the hot op
    // of the main renderer's vertex loop was a stack round trip per operand.
    bool store_back_inline = true;
    // cycle_local: the VU clock is a LOCAL `uint64_t cycle` (the function's
    // own copy of FlagPipeline::cycle()), written to the pipeline before and
    // read back after every callee that reads or advances the clock (the
    // stall model, the EFU / FDIV / clip / Q / P pipes, the ring readers, a
    // GENERIC word) and at every exit. With the reference form every
    // `++cycle` was a load / add / store to the pipeline, re-done at every
    // op because the data-memory byte stores (SQ, the unpack) may alias it.
    bool cycle_local = true;
    // entry_clamp: a VF the region reads CLAMPED and never writes has its
    // clamped view computed ONCE at the function entry (`vNc`, valid at every
    // label and on every path) instead of at its first read in each block
    // (every label began with the caches dropped: the matrix rows of the
    // main renderer's vertex loop were re-clamped per vertex, ~9 NEON ops
    // each). Pure (ClampVec of an unchanged value); re-done after a GENERIC
    // word's reload (the interpreter may have written any register). OFF by
    // default: with block_cap 1 it only raises the register pressure (the
    // Paris corpus bench: 4.62 vs 4.46 ns/bundle); it pays with regions of
    // several blocks (the loop's invariants clamped once per entry).
    bool entry_clamp = false;
    // cluster_ops > 0 (STAGE PERF-1): ONE function per CFG CLUSTER instead of
    // one region function per block. The TU's blocks are partitioned (BFS over
    // fall-through and static branch targets, at most cluster_ops ops per
    // cluster), every block is emitted ONCE as a label of its cluster's
    // function, the function takes the entry block's pc and begins with a
    // switch to its label; the exports table's per-block functions are
    // thunks (a tail call into the cluster). Intra-cluster edges are gotos
    // with the register file resident -- no store-back, no reload, no second
    // prologue -- while the text stays the blocks=1 size (the region policy
    // duplicated every successor into each function that reached it: 2.4-8x
    // the text, slower in the game). block_cap / op_cap are ignored.
    int cluster_ops = 0;
    // clamp_mask (default on): the "known clamped" register mask persisted
    // ACROSS block functions through Vu1State::vf_clamped. A block's exits
    // store the mask (the entry mask for the registers the function never
    // writes, the generator's proof for the ones it wrote: a full-mask
    // clamped write = clamped), and a clamped read of a register the function
    // has not written tests the entry mask before clamping (a clamp that
    // changed nothing sets the register's bit for the blocks after). The
    // main renderer's vertex blocks clamped their 10-20 matrix / constant
    // inputs anew at every entry (~9 NEON ops each per vertex).
    bool clamp_mask = true;
};

// A block-run profile over a corpus replay (Policy::cluster_ops with a profile: the clusters are
// grown from the hottest edges, so a hot loop's blocks share one function and the cold blocks stay
// singletons): block pc -> runs, (from pc, to pc) -> taken edges within one kick.
struct Vu1AotBlockProfile {
    std::map<uint32_t, uint64_t> runs;
    std::map<std::pair<uint32_t, uint32_t>, uint64_t> edges;
};

class Vu1AotBlockGen {
public:
    using Policy = Vu1AotRegionPolicy;
    static constexpr int kRegionOpCap = 48;     // Policy{} defaults (the recorded round-2 shape)
    static constexpr int kRegionBlockCap = 1;
    static constexpr bool kRegionTakenTargets = true;

    // The region of `head`: the head first, then its fall-through chain (built
    // if absent) within the caps and among `emitted` (the pcs the TU emits slots
    // for; a block outside is an exit); a taken static branch into the chain is
    // an edge, any other taken branch, a NOP-pair block, a wrapped delay slot
    // and an E-bit end are exits.
    static std::vector<const VuBlock*> Region(VuBlockProgram& prog, const VuBlock& head, const std::vector<uint32_t>& emitted,
                                              const Policy& policy = Policy{});

    // The function text of `head`'s region named `fn_name`; the op slots are
    // the `static constexpr VuDecodedSlot S_<pc4>_<k>` objects the caller
    // emitted for every block of the TU (SlotName). The function's signature
    // is VuBlock::NativeFn; the function of block <pc> is `Blk_<pc4>` (the
    // chain names it; the TU declares every one before the bodies).
    static std::string EmitFunction(VuBlockProgram& prog, const VuBlock& head, const std::string& fn_name,
                                    const std::vector<uint32_t>& emitted, const Policy& policy = Policy{});
    // Policy::cluster_ops: the partition of `emitted` into clusters (each a vector of block pcs, the
    // head first; every emitted block in exactly one cluster), and one cluster's function text.
    // `cluster_fn` maps every emitted block pc to its cluster function's name (the chains target it).
    static std::vector<std::vector<uint32_t>> Clusters(VuBlockProgram& prog, const std::vector<uint32_t>& emitted,
                                                       const Policy& policy, const Vu1AotBlockProfile* profile = nullptr);
    static std::string EmitClusterFunction(VuBlockProgram& prog, const std::vector<uint32_t>& cluster, const std::string& fn_name,
                                           const std::vector<uint32_t>& emitted,
                                           const std::vector<std::pair<uint32_t, std::string>>& cluster_fn, const Policy& policy);
    static std::string ClusterFunctionName(uint32_t head_pc);
    static std::string SlotName(uint32_t block_pc, int k);
    static std::string FunctionName(uint32_t block_pc);
    // The signature of a block function after its name (NativeFn's parameter list).
    static const char* Signature();
    // A cluster function's signature: NativeFn's parameters plus the entry block's pc.
    static const char* ClusterSignature();

private:
    Vu1AotBlockGen(VuBlockProgram& prog, std::vector<const VuBlock*> blocks, const std::string& fn_name,
                   const std::vector<uint32_t>& emitted, const Policy& policy);
    void ResetPassState();
    // Two passes over the region: the first learns the touched / written sets
    // and the clamped-read counts (the prologue and the flush / reload sets
    // need the whole region), the second emits the body.
    void EmitBody();
    void EmitBlock(int bi);
    void EmitOp(int bi, int i, uint32_t pc);
    // The stall accounting of op i of the current block (StallModel::ApplyBody's
    // steps with the slot's facts folded): the model's read lanes for ops 0..2,
    // the in-block writer locals from op 3 on; the IALU branch stall and the
    // EFU / FDIV waits by the slot's flags.
    void EmitStall(int i, const VuBlockOp& op, const VuDecodedSlot& s);
    // QueueFmac of the writes of ops [first, last] of the current block (the
    // model made whole for an edge / an exit).
    void EmitMaterialize(int first, int last);
    // An exit after op `last` of the current block (its end or an XTOP yield).
    void EmitExitEnd(int last);
    // An exit at the head of block `bi` (the budget would not hold it).
    void EmitExitHead(int bi);
    // The successor at `pc` of the current block after op `last`, when the
    // policy chains and the block is one of the TU's (not a NOP pair, not in
    // the region): the tail call. False = not chainable (the caller exits).
    bool EmitChain(uint32_t pc, int last);
    bool Chainable(uint32_t pc) const;
    void EmitUpper(const VuBlockOp& op, const VuDecodedSlot& s);
    void EmitLower(const VuBlockOp& op, const VuDecodedSlot& s, uint32_t pc);
    std::string Prologue() const;
    std::string StoreBack() const;
    // The store-back at an exit / chain: `store_back();` (the lambda) or its
    // statements in place (Policy::store_back_inline).
    void EmitStoreBack();
    // Policy::cycle_local: a callee that reads or advances the clock sees the
    // local written to the pipeline first and the local re-read after.
    static bool TouchesClock(const std::string& line);
    void SyncClockIn();
    void SyncClockOut();
    int BlockIndex(uint32_t pc) const;  // -1 = not in the region

    // Register naming: a raw read expression, the clamped view (emits the
    // clamp when the cache is stale), the clamped broadcast of one lane, a VI.
    std::string Vf(int n);
    std::string VfC(int n);
    std::string VfBcC(int n, int bc);
    std::string Vi(int n);
    void WroteVf(int n, bool clamped_full, bool clamped_lanes);
    void WroteVi(int n);
    void EnsureAccClean();
    void WriteLanes(const std::string& dst, const std::string& src, int mask);
    void SetVi(int n, const std::string& value_u32);
    // The MAC result of op `i` (`m<i>`, mask) joins the pending list; Deliver
    // hands the pending results to the pipeline in issue order before anything
    // that observes the ring (a STATUS / MAC reader, a GENERIC word) and at the
    // block's edges and exits: a result retired for sure by `ref` (issued >= 4
    // ops earlier) is folded, the others fold or push on the clock at run time.
    void NoteMac(int i, int mask);
    void Deliver(int ref);
    // Policy::mac_acc on the lazy ring: the per-op fold is emitted when the
    // result is folded (never pushed) at EVERY delivery point that can take it
    // -- pass 1's Deliver calls mark the pushed ones (mac_pushed_[block][op]).
    bool MacAccumulates() const;
    void FlushAll();
    void ReloadAll();
    void Line(const std::string& s);

    // Policy::cluster_ops: this function is a cluster (an entry switch; every edge to a block of
    // another cluster chains into that cluster's function with the target pc).
    bool cluster_ = false;
    const std::vector<std::pair<uint32_t, std::string>>* cluster_fn_ = nullptr;  // pc -> cluster function (sorted by pc)
    const std::string* ClusterFnOf(uint32_t pc) const;

    VuBlockProgram& prog_;
    std::vector<const VuBlock*> blocks_;  // the region, head first
    std::string fn_;
    const std::vector<uint32_t>& emitted_;  // the TU's block pcs, sorted
    Policy policy_;
    std::string body_;
    int indent_ = 1;
    bool analysing_ = false;
    int cur_ = 0;  // the block being emitted
    // The whole-region facts (both passes accumulate the same sets).
    bool vf_touched_[32] = {};
    bool vf_written_[32] = {};
    bool vi_touched_[16] = {};
    bool vi_written_[16] = {};
    int clamped_reads_[32] = {};
    bool vfc_declared_[32] = {};
    bool entry_clamped_[32] = {};  // Policy::entry_clamp: vNc computed at entry, valid at every label
    // Policy::clamp_mask: vf_known_[n] = the generator's proof that vN holds clamped bits at this
    // point (path-sensitive, reset at labels); written_here_[n] = vN may have been written on some
    // path to this point (reset to vf_written_ at labels).
    bool vf_known_[32] = {};
    bool written_here_[32] = {};
    bool acc_used_ = false;
    // The emission state (reset between the passes and at every label).
    bool vfc_valid_[32] = {};
    bool acc_clean_ = false;
    int hide_reg_ = -1;  // the lower of the current bundle reads this VF as `pre`
    int last_writer_[32][4];
    std::vector<uint8_t> folded_;
    int cur_op_ = 0;
    std::vector<std::pair<int, int>> pending_;  // (op, mask) MAC results not yet delivered
    std::vector<std::vector<uint8_t>> mac_pushed_;  // [block][op]: pushed (not folded) at some delivery point
};
