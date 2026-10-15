#pragma once
// DeliveryBudget: how many completed-read BYTES may become visible to the EE
// per refill epoch (one tick edge). A real drive paces DELIVERY at a few
// MB/s, so a loader that parses "whatever arrived" each frame parses a small
// slice; an instant model that surfaces a whole backlog at one boundary makes
// one iteration parse hundreds of ms of data. The meter keeps the completion
// LATENCY of the DeliveryMeter and caps only the per-epoch byte volume.
//
// Rules (the original's accounting core, ps2_vblank.h CdvdBudgetMeter):
//   * refill: remaining resets to the budget when the epoch STRICTLY
//     increases (a transient lower epoch read is absorbed); no carryover of
//     unused allowance, no carried debt;
//   * admit: a charge is allowed while remaining > 0 and saturates to 0 --
//     the ">0, not >=bytes" rule keeps liveness: a fresh epoch always admits
//     the FIFO head even when that one read exceeds the whole budget (a read
//     is the atomic completion unit), so per-epoch delivery is bounded by
//     budget + (largest single read - 1) and no read is deferred forever;
//   * a budget of 0 = unmetered: always allowed, never refills.
// The default budget is DerivedClocks::kCdvdKbPerTick (256 KB per tick).
// The refill epoch is the tick count MINUS any ticks a delivery itself fired
// (EpochOf): a delivery must never pace its own budget.
#include <cstdint>
#include <memory>
#include "engine/services/read_timing.h"

class DeliveryBudget {
public:
    static constexpr uint64_t kDefaultBudgetBytes = 256ull * 1024ull;

    struct Result {
        bool allowed;
        bool refilled;
    };

    explicit DeliveryBudget(uint64_t budget_bytes = kDefaultBudgetBytes)
        : state_(std::make_shared<ReadTiming::Budget>()) { state_->bytes = budget_bytes; }

    // Charges `bytes` against the epoch's allowance (see the rules above).
    Result Charge(uint64_t now_epoch, uint64_t bytes);

    // The refill epoch from the tick count and the ticks released by deliveries.
    static uint64_t EpochOf(uint64_t ticks, uint64_t delivery_fired_ticks) {
        return ticks > delivery_fired_ticks ? ticks - delivery_fired_ticks : 0;
    }
    // A Config value in KB: > 0 metered, otherwise unmetered (0).
    static uint64_t BytesFromKb(long kb) { return kb > 0 ? static_cast<uint64_t>(kb) * 1024ull : 0ull; }

    uint64_t BudgetBytes() const { return state_->bytes; }
    uint64_t Remaining() const { return state_->remaining; }
    uint64_t Epoch() const { return state_->epoch; }
    bool Primed() const { return state_->primed; }
    std::shared_ptr<ReadTiming::Budget> TimingBudget() const { return state_; }

private:
    std::shared_ptr<ReadTiming::Budget> state_;
};
