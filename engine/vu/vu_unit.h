#pragma once
// One vector unit: the register/memory record plus the pipelines that give
// it timing -- the flag pipeline (owner of the VU clock), the stall model and,
// for VU1, the XGKICK path into the GIF arbiter.
//
// PS2 rules: micro and data memory are addressed modulo their size (VU1 16 KB,
// VU0 4 KB) by every external writer (VIF MPG / UNPACK, the EE windows) and
// every internal access; a micro-memory store invalidates any decoded image
// (the epoch counter). XTOP / XITOP read the VIF1 TOP / ITOP registers, which
// the VIF hands the unit. Program execution goes through the unit's tier
// dispatch (`Execute`): HLE consult, MTVU redirect, then the cached or
// interpreter tier; the tiers drive the unit through the accessors below.
#include <cstddef>
#include <cstdint>
#include <memory>

#include "engine/vu/flag_pipeline.h"
#include "engine/vu/stall_model.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu1_state.h"
#include "engine/vu/xg_kick.h"

class GifArbiter;
class VuTierDispatch;

class VuUnit {
public:
    // `id` 0 or 1; VU1 takes the arbiter its XGKICK submits to (VU0 has no
    // GIF path and passes nullptr). `lazy_flags` selects the MAC ring
    // representation (the build default unless a test drives both).
    VuUnit(int id, GifArbiter* arbiter = nullptr, bool xgkick_paced_drain = true,
           bool lazy_flags = FlagPipeline::kDefaultLazy);
    ~VuUnit();

    int id() const { return id_; }
    uint32_t mem_mask() const { return state_->mem_mask; }

    // Disarm XGKICK, drop the stall rings and the P latch; registers,
    // memories, the clock and the CLIP/MAC/Q rings are untouched.
    void Reset();

    // Byte loops, every address masked (writers may straddle the wrap).
    void WriteDataMem(uint32_t addr, const uint8_t* src, size_t size);
    void WriteMicroMem(uint32_t addr, const uint8_t* src, size_t size);
    void ReadDataMem(uint32_t addr, uint8_t* dst, size_t size) const;
    // The EE-window store path invalidates the decoded image without a copy.
    void BumpMicroEpoch();

    // VIF1 TOP / ITOP (XTOP / XITOP sources).
    void SetVifTop(uint16_t top, uint16_t itop);
    uint16_t vif_top() const { return vif_top_; }
    uint16_t vif_itop() const { return vif_itop_; }

    // Restore / capture the whole unit from / into a trace record (registers,
    // both memories, VIF tops, the Q/P latches, the clock).
    void LoadCase(const Vu1Case& c);
    void SaveCase(Vu1Case& c, uint32_t start_pc) const;
    // Become a clone of `other` (same id): the record (registers, memories,
    // the per-run scratch, the micro epoch), the flag pipeline with its rings
    // and clock, the stall tables, the XGKICK drain state and the VIF tops --
    // everything a program's execution reads, so a run from here retires the
    // same bundles, cycles and kicks. The tier dispatch and the arbiter stay
    // this unit's (the lockstep A/B's shadow unit, VuLockstep).
    void CopyStateFrom(const VuUnit& other);

    // Run the microprogram from `start_pc` through the tier ladder; resume a
    // parked VU0 the same way. The dispatch carries the mode / HLE settings.
    void Execute(uint32_t start_pc);
    void ResumeIfWaiting();
    VuTierDispatch& dispatch() { return *dispatch_; }

    // KICK DROP (the clock contract, DT-14, for the MTVU worker): while set,
    // Execute runs NO program -- the kick is counted and returns. The VIF
    // stream around it is replayed unchanged (UNPACK / MPG / STCYCL / the
    // double-buffer swap, PATH2 DIRECT), so VU1 memory, the microprogram and
    // the GS register state stay coherent; only the kick's PATH1 output of the
    // dropped frame is missing. The owner (the worker) sets it per segment.
    void SetKickDrop(bool on) { kick_drop_ = on; }
    bool KickDrop() const { return kick_drop_; }
    uint64_t DroppedKicks() const { return dropped_kicks_; }

    Vu1State& state() { return *state_; }
    const Vu1State& state() const { return *state_; }
    VUregs& regs() { return state_->regs; }
    const VUregs& regs() const { return state_->regs; }
    FlagPipeline& flags() { return flags_; }
    const FlagPipeline& flags() const { return flags_; }
    StallModel& stalls() { return stalls_; }
    XgKick* xgkick() { return xgk_.get(); }
    const XgKick* xgkick() const { return xgk_.get(); }
    uint64_t cycle() const { return flags_.cycle(); }
    uint64_t micro_generation() const { return state_->micro_generation; }

private:
    int id_;
    std::unique_ptr<Vu1State> state_;  // 33 KB: heap-held so a unit may live anywhere
    FlagPipeline flags_;
    StallModel stalls_;
    std::unique_ptr<XgKick> xgk_;
    std::unique_ptr<VuTierDispatch> dispatch_;
    bool kick_drop_ = false;
    uint64_t dropped_kicks_ = 0;
    uint16_t vif_top_ = 0;
    uint16_t vif_itop_ = 0;
};
