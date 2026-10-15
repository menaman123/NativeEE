#pragma once
// LoaderModel: the completion-visibility model of the EE loader's asynchronous
// reads -- the single point at which a completed disc read becomes VISIBLE to
// the loader's poll, and the sync-wait rule of its poll spin.
//
// A read the loader enqueues (an fs object, lba, sectors) is scheduled on the
// DeliveryMeter (this class is the meter's producer): at the boundary the
// meter picks, the DeliveryBudget is charged with the read's bytes -- admitted,
// the payload is applied and the read is DELIVERED and RELEASED (consumable);
// denied, it stays in flight and is re-scheduled to the next boundary (a read
// is the atomic completion unit; the FIFO head of a fresh epoch is always
// admitted). The loader's poll then CONSUMES a released read (Progress) or
// finds it still in flight (HeldInflight) -- the outcome is noted for the
// spin rule below. Per read the order is always enq < dlv <= rel < cns.
//
// Poll visibility (cdvd.delivery_visibility = poll): every Poll first asks the
// meter to complete the reads whose modelled drive end lies inside the current
// frame (DeliveryMeter::CompleteNow(now_tick) -- the tick comes from the
// NowTick the owner binds); those reads are delivered and released AT THE POLL,
// as the EE observes a finished DVD read mid-frame. The DeliveryBudget is not
// consulted in that mode: the drive model's throughput is the pacing (a
// contiguous 2 KB-sector read costs 1/1.08 ms, so about 18 such reads fit a
// 16.667 ms frame; the title's loader windows average 2-3 sectors, about 6-9
// reads per frame). In boundary mode nothing here changes.
//
// The blocking wait under poll visibility (CompleteNow(fs)): the title's wait
// body spins until the drive is done, so the EE's instant inside the frame is
// the read's drive end. A blocking wait SPANS FRAMES: while the drive's end
// lies at or past the current frame's edge the EE stays blocked and vblanks
// pass -- the wait PARKS one whole frame through the owner's ParkFrame (the
// boundary protocol's wait: T0 advances one field, the vblank is raised; no
// sub-tick) and re-checks, until the end lies inside the frame; then the read
// is delivered (data + status) and its meter entry is settled (no phantom pop
// later). Issue order holds at the wait as at every other observation point:
// before the waited read lands, every read issued earlier (the one drive
// finished each no later than it) is delivered in schedule order
// (DeliveryMeter::CompleteBefore; WaitOrderDelivered()) -- the console's
// cdvdman completes reads in issue order and the EE never sees a later read
// complete ahead of an earlier one. Parked frames are counted (WaitParks()). Nothing is forced (Forced()
// counts only boundary mode's pull-forward, where the drive is not charged).
// Without a ParkFrame bound, or when the park cannot advance time, a wait whose
// drive end lies past the frame's edge is an OVERRUN (WaitOverruns(): the
// frame edge was crossed inside the wait); the drive timeline keeps that time
// either way, so the next issues start on the busy drive. The deliveries
// census is split by the site that landed the bytes: PollDelivered() (at a
// Poll), BoundaryDelivered() (slot 4), WaitDelivered() (inside the wait).
//
// Poll-spin sync-wait (the title's load-wait loop polls the one in-flight
// read against a frame-headroom query and spins until the headroom runs out):
// while the only blocker is an in-flight read nothing inside the spin can
// change the exit condition, so at the spin's headroom query the model
// answers "no headroom" -- the loop exits through the title's own path and
// the next lap polls again. ShouldYieldPollSlice engages only when ALL hold:
//   the query's return address is the engaged spin site the caller names (a title
//   parameter -- the engine knows no address), the caller attests the poll whose
//   note it holds ran (the title's lap flags), the last poll on this loader was HeldInflight,
//   and the read is STILL in flight now. Anything else declines (counted) and
//   the recompiled spin runs as written. Completion status is never faked.
//
// Trace (SetTraceSink; each line "seq kind fs=0x… [arm=|src=|blk=]"): enq
// (lba= sec= end=<drive end ms> under poll), prk (one per parked frame: end=
// edge=), dlv, rel arm=B, cns, yld blk=inflt, dfr (budget-deferred).
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "engine/services/read_timing.h"

class DeliveryMeter;
class DeliveryBudget;

class LoaderModel {
public:
    enum class PollOutcome : uint8_t { None = 0, Progress, HeldInflight };
    using Apply = std::function<void()>;
    using TraceSink = std::function<void(const std::string&)>;
    using NowTick = std::function<uint64_t()>;
    using ParkFrame = std::function<void()>;

    LoaderModel(DeliveryMeter& meter, DeliveryBudget& budget) : meter_(meter), budget_(budget) {}

    // An asynchronous read for loader object `fs` completed by the engine during
    // `issue_tick`; `apply` lands the payload when the read is delivered.
    // Returns the boundary tick the meter picked.
    uint64_t Enqueue(uint32_t fs, uint32_t lba, uint32_t sectors, uint64_t issue_tick, Apply apply);
    // The loader's completion poll for `fs`.
    PollOutcome Poll(uint32_t fs);
    // The loader's blocking wait: an in-flight read is delivered and consumed
    // NOW, so the object is free for its next Enqueue. Boundary mode: the
    // drive's work is done, only its visibility is pulled forward (counted in
    // Forced). Poll mode: the wait parks whole frames (ParkFrame) until the
    // drive's end lies inside the current frame, then delivers at that end
    // (WaitDelivered, WaitParks). The boundary delivery already scheduled for
    // it becomes a no-op. Returns whether a read was completed this way.
    bool CompleteNow(uint32_t fs);
    // The spin rule (above). `ra` is the headroom query's return address,
    // `spin_site_ra` the engaged spin site (title data), `poll_ran_this_lap` the caller's attestation that the noted poll ran.
    bool ShouldYieldPollSlice(uint32_t ra, uint32_t spin_site_ra, bool poll_ran_this_lap);

    bool InFlight(uint32_t fs) const;
    bool Released(uint32_t fs) const;
    // Whether the meter runs poll visibility (cdvd.delivery_visibility = poll).
    bool PollVisibility() const;
    std::shared_ptr<ReadTiming> Timing() const;
    std::shared_ptr<ReadTiming::Budget> TimingBudget() const;
    void SetTraceSink(TraceSink sink) { sink_ = std::move(sink); }
    // The tick domain's current tick, read at every Poll (poll visibility).
    void SetNowTick(NowTick now_tick) { now_tick_ = std::move(now_tick); }
    // Parks the blocked EE for ONE whole frame of emulated time (the next tick
    // of NowTick): the blocking wait's frame-spanning step under poll visibility.
    // Unbound = a wait never parks (an end past the edge is an overrun).
    void SetParkFrame(ParkFrame park) { park_ = std::move(park); }

    uint64_t Enqueued() const { return enqueued_; }
    uint64_t Delivered() const { return delivered_; }
    uint64_t Deferred() const { return deferred_; }
    uint64_t Rejected() const { return rejected_; }   // Enqueue on an fs already in flight
    uint64_t Consumed() const { return consumed_; }
    uint64_t Forced() const { return forced_; }
    uint64_t PollDelivered() const { return poll_delivered_; }
    uint64_t BoundaryDelivered() const { return boundary_delivered_; }
    uint64_t WaitDelivered() const { return wait_delivered_; }
    uint64_t WaitOverruns() const { return wait_overruns_; }
    uint64_t WaitParks() const { return wait_parks_; }   // whole frames blocking waits parked through (poll mode)
    uint64_t WaitOrderDelivered() const { return wait_order_delivered_; }   // predecessors a blocking wait made visible in issue order (poll mode)
    uint64_t Yields() const { return yields_; }
    uint64_t Declines() const { return declines_; }
    PollOutcome LastPoll() const { return note_; }

private:
    struct Read {
        uint32_t lba = 0;
        uint32_t sectors = 0;
        Apply apply;
        uint64_t due = 0;   // the boundary the meter picked (the budget epoch)
        uint64_t gen = 0;   // this read's generation: a stale boundary closure never delivers a later read
        uint64_t meter_id = 0;   // the meter entry (settled by a poll-mode wait)
        double end_ms = 0.0;     // the drive's end (poll mode), else 0
        bool released = false;
    };
    void Deliver(uint32_t fs, uint64_t gen);
    void Trace(const char* kind, uint32_t fs, const char* extra = nullptr);

    DeliveryMeter& meter_;
    DeliveryBudget& budget_;
    std::map<uint32_t, Read> reads_;   // in flight or released, by fs
    TraceSink sink_;
    NowTick now_tick_;
    ParkFrame park_;
    bool polling_ = false;   // inside Poll's CompleteNow drain (the trace's src=poll)
    bool waiting_ = false;   // inside a blocking wait's in-order drain (the trace's src=wait-order)
    uint64_t seq_ = 0;
    PollOutcome note_ = PollOutcome::None;
    uint32_t note_fs_ = 0;
    uint64_t enqueued_ = 0, delivered_ = 0, deferred_ = 0, consumed_ = 0, yields_ = 0, declines_ = 0;
    uint64_t rejected_ = 0;
    uint64_t forced_ = 0;
    uint64_t poll_delivered_ = 0, boundary_delivered_ = 0, wait_delivered_ = 0, wait_overruns_ = 0, wait_parks_ = 0;
    uint64_t wait_order_delivered_ = 0;
    uint64_t gen_ = 0;
};
