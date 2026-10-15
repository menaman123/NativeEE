#pragma once
// DeliveryMeter: WHEN a completed disc read becomes visible to the EE.
//
// The ReadEngine moves bytes inside the call; a real drive completes later and
// the EE observes the completion when it looks. Two visibility rules
// (cdvd.delivery_visibility, Config):
//
//   Boundary -- every completion scheduled during tick k is delivered at
//               boundary slot 4 (MaintDuties) in tick order, first in the
//               slot's duty order (CD delivery -> RPC end-callbacks -> pad
//               stamp), so the arrival clock is a pure function of the tick
//               domain, never of host I/O time. The due boundary follows
//               cdvd.latency_model (below), never earlier than the next
//               boundary.
//   Poll     -- the drive has a timeline in milliseconds: a read issued during
//               tick k starts at max(drive free, k x 16.667 ms) -- every poll
//               of a slice happens at the frame's emulated instant -- and ends
//               at start + seek + transfer (the drive model below). A read that
//               ends before (k + 1) x 16.667 ms is COMPLETE inside the frame:
//               the next poll that asks (CompleteNow(k)) delivers it there and
//               then, as the EE observes a finished DVD read mid-frame; a read
//               that ends later is due at the boundary of the tick the drive
//               finishes in (slot 4, as Boundary). The drive's time is the only
//               sub-frame quantity and it is modelled, never host time.
//
// cdvd.latency_model (Config, a SETTING; Boundary mode's due rule):
//   Instant  -- a completion is visible at the NEXT boundary after its issue
//               tick (the original's hold-to-edge delivery: a CD completion
//               scheduled 3000 us after issue lands before the tick advances).
//   Modelled -- the original's DVD model, in ticks: one virtual drive, reads
//               serialized on it; seek cost by sector distance from the head
//               (<= 16 sectors: 0 ms; < 14764: 30 + 12 ms rotation; else
//               100 + 12 ms), transfer at 1.08 sectors per ms (4x DVD class),
//               the sum scaled by cdvd.latency_scale (0 = instant); the due
//               boundary is the first tick edge at or after the drive's busy
//               time, never earlier than the next boundary.
// Poll mode always runs the drive model (the drive's time IS the visibility
// question), scaled by cdvd.poll_latency_scale (> 0, default 1.0 = the DVD
// model as calibrated) -- never by cdvd.latency_scale, whose 0 under the
// deterministic profile is Boundary mode's "no drive time" pin.
//
// The blocking wait (Settle). When the title blocks on a read (its wait body
// spins until the drive is done), the EE's time inside the frame IS the drive's
// end: the owner delivers the read itself and settles its entry here, so the
// queue never holds a closure whose read has already landed (an honest census:
// every CompleteNow / DeliverDue pop is a delivery). The drive timeline is not
// rewritten by a settle -- the drive did the read; a wait whose end lies past
// the frame's edge (an overrun: the hardware's EE would have missed the vblank)
// carries that time into the next frame's issues.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>

#include "engine/core/config/config.h"
#include "engine/services/read_timing.h"

class DeliveryMeter {
public:
    using Deliver = std::function<void()>;
    static constexpr double kTickMs = 16667.0 / 1000.0;   // NTSC field
    static constexpr uint32_t kContiguousSectors = 16;
    static constexpr uint32_t kShortSeekSectors = 14764;
    static constexpr double kShortSeekMs = 30.0 + 12.0;
    static constexpr double kLongSeekMs = 100.0 + 12.0;
    static constexpr double kSectorsPerMs = 1.08;

    explicit DeliveryMeter(const ps2r::CdvdConfig& cfg)
        : timing_(std::make_shared<ReadTiming>(cfg)) {}

    // A read of `sectors` at `lba` completed by the engine during `issue_tick`;
    // `deliver` runs at the boundary the model picks (or, in Poll mode, at the
    // first CompleteNow whose tick contains the drive's end). Returns the due tick.
    uint64_t Schedule(uint64_t issue_tick, uint32_t lba, uint32_t sectors, Deliver deliver);
    // Re-queues a delivery the boundary at `due` could not make visible (a
    // budget denial) for the NEXT boundary, due + 1, without consulting the
    // latency model: the drive already did the work, only the visibility
    // moves, so the virtual head and busy time are untouched.
    uint64_t Reschedule(uint64_t due, Deliver deliver);
    // Boundary slot 4 at `tick`: runs every delivery due at or before it, in
    // schedule order. Returns the number delivered.
    uint32_t DeliverDue(uint64_t tick);
    // A poll during `tick` (Poll mode only; Boundary returns 0 and touches
    // nothing): runs every queued delivery whose drive end lies inside the
    // tick's frame, in schedule order. Returns the number delivered.
    uint32_t CompleteNow(uint64_t tick);
    // The blocking wait's order rule (Poll mode only; Boundary returns 0 and
    // touches nothing): runs every queued delivery AHEAD of entry `id` in
    // schedule order. One drive serialises the reads, so every read issued
    // before the waited one finished no later than it: when the EE's sync wait
    // returns, those completions are visible too (cdvdman completes reads in
    // issue order; a later read is never observed complete before an earlier
    // one). Returns the number delivered.
    uint32_t CompleteBefore(uint64_t id);
    // Removes the entry Schedule created (LastScheduledId after that Schedule)
    // without running it: the owner delivered the read inside a blocking wait.
    // Returns whether the entry was still queued.
    bool Settle(uint64_t id);
    uint64_t LastScheduledId() const { return last_id_; }
    double LastEndMs() const { return timing_->Capture().last_end_ms; }
    static double FrameEndMs(uint64_t tick) { return static_cast<double>(tick + 1) * kTickMs; }

    uint64_t DueTick(uint64_t issue_tick, uint32_t lba, uint32_t sectors);   // the model, no enqueue
    // One drive. A read issued by the drive's OTHER client -- the IOP-side
    // cdvdman serving the STREAM module's own sceCdRead -- at drive time
    // `now_ms` (the IOP clock's instant, on the same millisecond timeline as
    // the ticks): charges the timeline (seek from the head, transfer) at the
    // poll scale and returns the read's end. Poll mode only: a Boundary-mode
    // meter charges nothing and returns `now_ms` -- the root binds the module's
    // CompletionDue to this meter under poll visibility only, so a boundary-mode
    // module keeps its own fixed completion latency (unbound, bit for bit as
    // before). The read is not queued here: its completion is the module's
    // callback on the IOP kernel.
    double ChargeReadAtMs(double now_ms, uint32_t lba, uint32_t sectors);
    size_t Pending() const { return queue_.size(); }
    uint64_t Delivered() const { return delivered_; }            // every delivery (boundary + poll)
    uint64_t BoundaryDeliveries() const { return delivered_ - poll_delivered_; }
    uint64_t PollDeliveries() const { return poll_delivered_; }
    ps2r::CdvdLatencyModel Model() const { return timing_->Mode(); }
    ps2r::CdvdDeliveryVisibility Visibility() const { return timing_->Visibility(); }
    double Scale() const { return timing_->Scale(); }
    double PollScale() const { return timing_->PollScale(); }
    double BusyUntilMs() const { return timing_->Capture().busy_ms; }
    std::shared_ptr<ReadTiming> Timing() const { return timing_; }
    // The drive timeline's seek census (reads charged by DriveEndMs): contiguous
    // (<= kContiguousSectors from the head), short seek, long seek.
    uint64_t SeeksContiguous() const { return timing_->SeeksContiguous(); }
    uint64_t SeeksShort() const { return timing_->SeeksShort(); }
    uint64_t SeeksLong() const { return timing_->SeeksLong(); }

private:
    struct Entry {
        uint64_t due_tick;
        double end_ms;   // the drive's end of this read (Poll mode), else 0
        uint64_t id;     // Schedule's id (Settle); 0 for a Reschedule entry
        Deliver deliver;
    };
    std::shared_ptr<ReadTiming> timing_;
    uint64_t last_id_ = 0;
    std::deque<Entry> queue_;
    uint64_t delivered_ = 0;
    uint64_t poll_delivered_ = 0;
};
