#pragma once
// DmacDeferredVif1: the "DMA completes when observed" model of a VIF1 chain
// kick, and the END-slot tail chase.
//
// PS2 rule modelled: real DMA is slow and tag-chases -- the CPU appends
// display-list tags behind the read pointer, commonly rewriting the END tag in
// place. An instant walk would strand that tail, so a VIF1 chain kick is
// PARKED with STR set (the channel reads busy) and walked at the first point
// the game observes completion: any VIF1 register read or write, a PCR write
// with CPC for VIF1, a GIF kick (PATH2 must land before PATH3), an HLE sync,
// a blocking kernel wait of the EE (Dmac::ObserveAtKernelWait: the CPU sleeps,
// the DMA runs), or the frame sync. END slots with QWC 0 are re-read at the
// frame sync and a sane rewrite resumes the walk from that slot.
//
// The blocking kernel wait is also where the race ENDS: the kernel reschedules
// the EE (microseconds) while the chain (microseconds) runs to its END, so a
// rewrite of that END slot after the wait is the game building a NEW list in the
// buffer (a double-buffered display list rebuilt after its FINISH arrived), not
// an append the DMA could still read. A KernelWait flush therefore records no
// tail and drops the records of earlier walks; only a chain flushed by a
// register access, a GIF kick or the frame sync keeps its END slot chaseable.
//
// The walk itself is the owner's (the DMAC runs it over the channel registers
// and completes the channel); it is bound through the runner hook.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/dmac/chain_walker.h"
#include "engine/dmac/dmac_channel.h"

class GifArbiter;

// One VIF1 END-slot record: where the chain ended (QWC 0 END only), what the
// terminator read as, and the kick-time CHCR (mode / TTE / ASP) to resume with.
struct DmacVif1TailRecord {
    uint32_t end_tadr = 0;
    bool from_spr = false;
    uint64_t end_tag_lo = 0;
    uint32_t chcr = 0;
};

class DmacDeferredVif1 {
public:
    enum class FlushReason { ChcrRead, RegWrite, PcrCpc, GifKick, ReKick, FrameSync, Explicit, KernelWait };
    // Runs the VIF1 chain walk over the channel registers as they stand and completes the channel.
    using RunChain = void (*)(void* user);
    static constexpr uint32_t kMaxSaneQwc = 0x4000;

    DmacDeferredVif1(ChainWalker& walker, GifArbiter& arbiter);

    void SetRunner(RunChain fn, void* user);

    // Park the kick-time registers; the caller leaves STR set in the channel.
    void Park(const DmacChannelRegs& regs);
    bool Active() const { return active_; }
    const DmacChannelRegs& Snapshot() const { return snapshot_; }
    // Savestate restore: no parked kick, no tail records.
    void Clear();

    // Restore the parked registers into `vif1`, run the walk, clear STR, drain
    // the arbiter. No effect when nothing is parked -- except that a KernelWait
    // drops every tail record (header: the race is over once the EE blocked).
    void FlushDeferredVif1(DmacChannelRegs& vif1, FlushReason reason);
    // Frame sync: flush, then re-read every recorded END slot and resume the
    // chain from each slot whose tag was rewritten to a sane non-terminator.
    void ChaseVif1Tail(DmacChannelRegs& vif1);

    // After a VIF1 walk: an END with QWC 0 records (or refreshes) its slot.
    void RecordTail(const DmacVifWalkResult& res, uint32_t chcr_before_str_clear);
    size_t TailCount() const { return tails_.size(); }
    const std::vector<DmacVif1TailRecord>& Tails() const { return tails_; }

    // The resume gate: changed, not 0 / all-ones, QWC <= 0x4000, and an
    // address-bearing id points into RAM or the scratchpad.
    static bool IsSaneRewrite(uint64_t now_lo, uint64_t recorded_lo);

private:
    ChainWalker& walker_;
    GifArbiter& arbiter_;
    RunChain run_ = nullptr;
    void* run_user_ = nullptr;
    bool active_ = false;
    DmacChannelRegs snapshot_{};
    std::vector<DmacVif1TailRecord> tails_;
};
