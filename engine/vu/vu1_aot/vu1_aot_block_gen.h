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
#include <string>
#include <vector>

struct VuBlock;
struct VuBlockOp;
struct VuBlockProgram;
struct VuDecodedSlot;

class Vu1AotBlockGen {
public:
    static constexpr int kRegionOpCap = 48;     // ops a region holds at most (the head block always)
    static constexpr int kRegionBlockCap = 1;   // blocks a region holds at most
    static constexpr bool kRegionTakenTargets = true;  // grow over static branch targets too (not only fall-through)

    // The region of `head`: the head first, then its fall-through chain (built
    // if absent) within the caps and among `emitted` (the pcs the TU emits slots
    // for; a block outside is an exit); a taken static branch into the chain is
    // an edge, any other taken branch, a NOP-pair block, a wrapped delay slot
    // and an E-bit end are exits.
    static std::vector<const VuBlock*> Region(VuBlockProgram& prog, const VuBlock& head, const std::vector<uint32_t>& emitted);

    // The function text of `head`'s region named `fn_name`; the op slots are
    // the `static constexpr VuDecodedSlot S_<pc4>_<k>` objects the caller
    // emitted for every block of the TU (SlotName).
    static std::string EmitFunction(VuBlockProgram& prog, const VuBlock& head, const std::string& fn_name,
                                    const std::vector<uint32_t>& emitted);
    static std::string SlotName(uint32_t block_pc, int k);

private:
    Vu1AotBlockGen(std::vector<const VuBlock*> blocks, const std::string& fn_name);
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
    void EmitUpper(const VuBlockOp& op, const VuDecodedSlot& s);
    void EmitLower(const VuBlockOp& op, const VuDecodedSlot& s, uint32_t pc);
    std::string Prologue() const;
    std::string StoreBack() const;
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
    void FlushAll();
    void ReloadAll();
    void Line(const std::string& s);

    std::vector<const VuBlock*> blocks_;  // the region, head first
    std::string fn_;
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
    bool acc_used_ = false;
    // The emission state (reset between the passes and at every label).
    bool vfc_valid_[32] = {};
    bool acc_clean_ = false;
    int hide_reg_ = -1;  // the lower of the current bundle reads this VF as `pre`
    int last_writer_[32][4];
    std::vector<uint8_t> folded_;
    int cur_op_ = 0;
    std::vector<std::pair<int, int>> pending_;  // (op, mask) MAC results not yet delivered
};
