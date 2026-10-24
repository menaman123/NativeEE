#pragma once
// The VU's read-after-write stall model: the in-flight FMAC and IALU
// writebacks and the pipe busy-waits that advance the VU clock before a
// bundle's instructions execute.
//
// PS2 rules: an FMAC VF result is not readable for 4 cycles after issue (a
// reader stalls to the writeback); an ILW/ILWR VI result retires 4 cycles
// later and only BRANCHES consume that hazard; a second FDIV-pipe op waits for
// the pending Q; a second EFU-pipe op waits for the pending P, released one
// cycle early; VF00 reads never stall; ACC is not a stall source. Every stall
// is `cycle = max(cycle, retire)`; the bundle's own writebacks are queued
// AFTER its stalls with `retire = cycle + 4`.
//
// Representation: the retire cycle of the LAST write to every VF lane
// (`fmac_retire_[reg][lane]`) and to every VI (`ialu_retire_[vi]`), plus the
// newest retire of each kind. A reader stalls to the largest retire among the
// lanes it reads that is still ahead of the clock; a lane whose retire the
// clock has passed is inert, so nothing is ever pruned. This is the writeback
// RING of the OLD model (depth 8 FMAC / 4 IALU, soonest-retiring dropped when
// full) collapsed onto what a reader can observe: with the clock advancing at
// least one cycle per bundle, at most two VF writes per bundle and the 4-cycle
// latency, the ring never held more than 8 live entries -- the drop rule was
// unreachable -- and a reader took the max retire over the entries covering
// its lanes, which is this array's value since later writes retire later.
// `VuStallModel` pins the stalls bundle by bundle; the corpus compares pin the
// tiers against the interpreter.
//
// ONE body serves every entry point: `Apply` evaluates every step at run time;
// `ApplySpec<M>` compiles out the steps a static stall class proves absent
// (the cached tier's callers); `ApplyClassed` maps a slot's class to the
// matching specialisation.
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"
#include "engine/vu/flag_pipeline.h"
#include "engine/vu/vu_decoder.h"

class StallModel {
public:
    static constexpr size_t kLineBytes = 128;  // the host cache line the hot tables are phased to
    static constexpr int kFmacWbLatency = 4;
    static constexpr int kIaluLwLatency = 4;

    // Static stall-class bits (the span classifier's class table decomposes
    // into these): which steps a bundle can need. kBranch = the IALU branch
    // stall (classes 15-18: a branch bundle with 0/1 VF write, with/without reads).
    enum : uint8_t { kRd = 1, kWr1 = 2, kWr2 = 4, kIaluWr = 8, kLower = 16, kBranch = 64 };

    // The dynamic form: every step guarded by the slot's decoded facts.
    void Apply(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes);
    // Dispatch on `s.stall_class` (0 / unknown -> Apply).
    void ApplyClassed(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes);
    // The statically pruned form for class mask M (no branch / FDIV / EFU waits).
    template <uint8_t M>
    void ApplySpec(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes);

    // The superblock's writer-fold form (fold class 16): a bundle statically
    // proven to read nothing in flight only queues its own writebacks (step 7).
    void ApplyPrunePush(const VuDecodedSlot& s, FlagPipeline& pipes);
    // Span close: the FMAC writebacks of the last min(executed, 3) bundles
    // before `close_idx` (slot index of the first unexecuted bundle), the
    // bundle k back having issued at `cycle + 1 - k`. Earlier writebacks are
    // inert by then (the lanes keep them). The IALU side is untouched.
    void MaterializeFmac(const VuDecodedSlot* slots, size_t close_idx, uint32_t executed, uint64_t cycle);
    // The block tier's close: the writebacks of `n` (<= 3) bundles with their
    // recorded issue cycles (oldest first; a bundle k back issued at issue[k],
    // its writebacks retiring at issue[k] + 4). The IALU side is untouched.
    void MaterializeFmacIssued(const VuDecodedSlot* const* bundles, const uint64_t* issue, int n);
    // Queue an ILW / ILWR VI writeback issued at `cycle` (step 8 of the body
    // on its own: the block tier's static schedule skips the body but keeps
    // the IALU side live for the branch stall).
    void QueueIalu(uint8_t vi, uint64_t cycle) { PushIalu(vi, cycle); }
    // Queue one VF writeback (lanes `mask` of `reg`) issued at `cycle`: step 7
    // of the body for one write, the generated blocks' materialise of a folded
    // op's write (MaterializeFmacIssued one write at a time, oldest first).
    void QueueFmac(uint8_t reg, uint8_t mask, uint64_t cycle) { PushFmac(reg, mask, cycle); }

    // Program end: the clock passes every in-flight writeback.
    void FlushFmacWb(FlagPipeline& pipes);
    void FlushIaluWb(FlagPipeline& pipes);
    // Forget every writeback (the unit's Reset); the clock is untouched.
    void Reset();

    // The retire cycle of the last write to a VF lane / a VI (0 = never
    // written since Reset); in flight while > the clock.
    uint64_t FmacRetire(int reg, int lane) const { return fmac_retire_[reg][lane]; }
    uint64_t IaluRetire(int vi) const { return ialu_retire_[vi]; }
    // The newest FMAC / IALU retire (nothing is in flight once the clock passes it).
    uint64_t fmac_newest_retire() const { return fmac_newest_; }
    uint64_t ialu_newest_retire() const { return ialu_newest_; }

private:
    // M's kLower/kRd/kWr1/kWr2/kIaluWr/kBranch select the compiled steps;
    // kDynamic adds the run-time guards and the branch / EFU / FDIV waits.
    static constexpr uint8_t kDynamic = 32;
    template <uint8_t M>
    void ApplyBody(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes);

    // The stall a read of `reg`'s lanes `mask` takes at `cycle` (0 = none).
    uint64_t ReadStall(uint8_t reg, uint8_t mask, uint64_t cycle) const;
    void PushFmac(uint8_t reg, uint8_t mask, uint64_t cycle);
    void PushIalu(uint8_t vi, uint64_t cycle);

    // Line-aligned: a VF row (4 x 8 B) must never straddle a cache line. With
    // the 16-byte alignment the unit's placement decided the row phase, and
    // one phase in eight (the table at 112 mod 128) ran the block tier at
    // 3x its cost (bench7 47 vs 14.6 ns/bundle; E181).
    alignas(kLineBytes) uint64_t fmac_retire_[32][4] = {};
    uint64_t fmac_newest_ = 0;
    uint64_t ialu_retire_[16] = {};
    uint64_t ialu_newest_ = 0;
};

#include "engine/vu/stall_model.inl"
