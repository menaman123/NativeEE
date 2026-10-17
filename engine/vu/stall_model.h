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
    static constexpr int kFmacWbLatency = 4;
    static constexpr int kIaluLwLatency = 4;
    static constexpr int kFmacDepth = 8;
    static constexpr int kIaluDepth = 4;

    // Static stall-class bits (the span classifier's class table decomposes
    // into these): which steps a bundle can need. kBranch = the IALU branch
    // stall (classes 15-18: a branch bundle with 0/1 VF write, with/without reads).
    enum : uint8_t { kRd = 1, kWr1 = 2, kWr2 = 4, kIaluWr = 8, kLower = 16, kBranch = 64 };

    struct FmacWb {
        uint64_t retire;
        uint8_t reg;
        uint8_t mask;
    };
    struct IaluWb {
        uint64_t retire;
        uint8_t vi;
    };

    // The dynamic form: every step guarded by the slot's decoded facts.
    void Apply(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes);
    // Dispatch on `s.stall_class` (0 / unknown -> Apply).
    void ApplyClassed(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes);
    // The statically pruned form for class mask M (no branch / FDIV / EFU waits).
    template <uint8_t M>
    void ApplySpec(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes);

    // The superblock's writer-fold form (fold class 16): a bundle statically
    // proven to read nothing in flight only drops retired writebacks and queues
    // its own (steps 1 and 7 of the body).
    void ApplyPrunePush(const VuDecodedSlot& s, FlagPipeline& pipes);
    // Span close: rebuild the FMAC ring from the last min(executed, 3) bundles
    // before `close_idx` (slot index of the first unexecuted bundle), the
    // bundle k back having issued at `cycle + 1 - k`. The IALU ring is untouched.
    void MaterializeFmac(const VuDecodedSlot* slots, size_t close_idx, uint32_t executed, uint64_t cycle);
    // The block tier's close: rebuild the FMAC ring from `n` (<= 3) bundles
    // with their recorded issue cycles (oldest first; a bundle k back issued
    // at issue[k], its writebacks retiring at issue[k] + 4). The IALU ring is
    // untouched.
    void MaterializeFmacIssued(const VuDecodedSlot* const* bundles, const uint64_t* issue, int n);
    // Queue an ILW / ILWR VI writeback issued at `cycle` (step 8 of the body
    // on its own: the block tier's static schedule skips the body but keeps
    // the IALU ring live for the branch stall).
    void QueueIalu(uint8_t vi, uint64_t cycle) { PushIalu(vi, cycle); }

    // Program end: the clock passes every in-flight writeback, rings emptied.
    void FlushFmacWb(FlagPipeline& pipes);
    void FlushIaluWb(FlagPipeline& pipes);
    // Drop the rings (the unit's Reset); the clock is untouched.
    void Reset();

    int fmac_depth() const { return fmac_wb_n_; }
    int ialu_depth() const { return ialu_wb_n_; }
    const FmacWb& fmac(int i) const { return fmac_wb_[i]; }
    const IaluWb& ialu(int i) const { return ialu_wb_[i]; }
    uint32_t fmac_regs() const { return fmac_wb_regs_; }
    uint64_t fmac_min_retire() const { return fmac_wb_min_retire_; }

private:
    // M's kLower/kRd/kWr1/kWr2/kIaluWr/kBranch select the compiled steps;
    // kDynamic adds the run-time guards and the branch / EFU / FDIV waits.
    static constexpr uint8_t kDynamic = 32;
    template <uint8_t M>
    void ApplyBody(const VuDecodedSlot& s, VUregs& regs, FlagPipeline& pipes);

    void PruneFmac(uint64_t cycle);
    void PruneIalu(uint64_t cycle);
    void PushFmac(uint8_t reg, uint8_t mask, uint64_t cycle);
    void PushIalu(uint8_t vi, uint64_t cycle);

    FmacWb fmac_wb_[kFmacDepth] = {};
    int fmac_wb_n_ = 0;
    uint64_t fmac_wb_min_retire_ = 0;
    uint32_t fmac_wb_regs_ = 0;
    IaluWb ialu_wb_[kIaluDepth] = {};
    int ialu_wb_n_ = 0;
};

#include "engine/vu/stall_model.inl"
