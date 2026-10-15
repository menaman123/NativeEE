// LoaderModel implementation; the model is described in the header.
#include "engine/cdvd/hle/loader_model.h"

#include <cstdio>

#include "engine/cdvd/delivery_budget.h"
#include "engine/cdvd/delivery_meter.h"
#include "engine/cdvd/iso_fs.h"

void LoaderModel::Trace(const char* kind, uint32_t fs, const char* extra) {
    if (!sink_) return;
    char line[128];
    std::snprintf(line, sizeof line, "%llu %s fs=0x%x%s%s", static_cast<unsigned long long>(++seq_), kind, fs,
                  extra ? " " : "", extra ? extra : "");
    sink_(line);
}

// One read in flight per fs object: the title's async reader issues the next
// read on an object only after it has consumed the previous completion, so a
// second Enqueue while the first is still in flight is a caller error --
// refused (0) and counted, never silently replacing the queued delivery.
uint64_t LoaderModel::Enqueue(uint32_t fs, uint32_t lba, uint32_t sectors, uint64_t issue_tick, Apply apply) {
    if (InFlight(fs)) {
        ++rejected_;
        Trace("rej", fs, "blk=inflt");
        return 0;
    }
    Read& r = reads_[fs];
    r.lba = lba;
    r.sectors = sectors;
    r.apply = std::move(apply);
    r.released = false;
    r.gen = ++gen_;
    ++enqueued_;
    const uint64_t gen = r.gen;
    r.due = meter_.Schedule(issue_tick, lba, sectors, [this, fs, gen] { Deliver(fs, gen); });
    r.meter_id = meter_.LastScheduledId();
    r.end_ms = meter_.LastEndMs();
    // The drive timeline is auditable from the trace: the read's extent and,
    // under poll visibility, the modelled end (0 = boundary mode, no drive time).
    char extra[80];
    std::snprintf(extra, sizeof extra, "src=ee lba=%u sec=%u end=%.3f", lba, sectors, r.end_ms);
    Trace("enq", fs, extra);
    return r.due;
}

bool LoaderModel::CompleteNow(uint32_t fs) {
    auto it = reads_.find(fs);
    if (it == reads_.end()) return false;
    Read& r = it->second;
    const bool poll_visibility = meter_.Visibility() == ps2r::CdvdDeliveryVisibility::Poll;
    if (!r.released) {
        bool overrun = false;
        if (poll_visibility && now_tick_) {
            // The EE is blocked while the drive works: every frame edge the drive's
            // end lies beyond is a whole frame the wait parks through (the vblank
            // passes, T0 advances one field), until the end is inside the frame.
            // A park that cannot advance time (no guest thread, shutdown) stops the
            // loop and the delivery is an overrun.
            while (park_ && r.end_ms >= DeliveryMeter::FrameEndMs(now_tick_())) {
                const uint64_t before = now_tick_();
                char prk[64];
                std::snprintf(prk, sizeof prk, "end=%.3f edge=%.3f", r.end_ms, DeliveryMeter::FrameEndMs(before));
                Trace("prk", fs, prk);
                park_();
                if (now_tick_() == before) break;
                ++wait_parks_;
            }
            overrun = r.end_ms >= DeliveryMeter::FrameEndMs(now_tick_());
        }
        if (poll_visibility) {
            // In issue order: every read the one drive finished before the waited
            // one is visible when the wait returns (the console's cdvdman completes
            // reads in issue order; the EE never observes a later read complete
            // ahead of an earlier one). Landed inside the wait: counted as wait
            // deliveries, traced src=wait-order.
            waiting_ = true;
            const uint32_t ahead = meter_.CompleteBefore(r.meter_id);
            waiting_ = false;
            wait_order_delivered_ += ahead;
        }
        if (r.apply) r.apply();
        ++delivered_;
        if (poll_visibility) {
            // Delivered at the drive's end, inside the frame the EE is blocked in.
            ++wait_delivered_;
            meter_.Settle(r.meter_id);
            if (overrun) ++wait_overruns_;
            char extra[64];
            std::snprintf(extra, sizeof extra, "src=wait end=%.3f%s", r.end_ms, overrun ? " overrun" : "");
            Trace("dlv", fs, extra);
        } else {
            Trace("dlv", fs, "src=wait");
        }
        Trace("rel", fs, "arm=B");
    }
    if (!poll_visibility) ++forced_;
    ++consumed_;
    Trace("cns", fs, "src=wait");
    reads_.erase(it);
    note_ = PollOutcome::Progress;
    note_fs_ = fs;
    return true;
}

// Runs inside the meter's boundary drain (slot 4) or, under poll visibility,
// inside a Poll's CompleteNow drain. The budget epoch is the delivery boundary;
// a denied read keeps its in-flight status and comes back at the next boundary
// (due + 1, so the epoch advances) without re-running the latency model -- the
// drive's work is done, only the visibility moves. Under poll visibility the
// budget is not consulted: the drive model is the throughput.
void LoaderModel::Deliver(uint32_t fs, uint64_t gen) {
    auto it = reads_.find(fs);
    if (it == reads_.end() || it->second.released || it->second.gen != gen) return;
    Read& r = it->second;
    const uint64_t bytes = static_cast<uint64_t>(r.sectors) * IsoFs::kSectorSize;
    const bool poll_visibility = meter_.Visibility() == ps2r::CdvdDeliveryVisibility::Poll;
    if (!poll_visibility && !budget_.Charge(r.due, bytes).allowed) {
        ++deferred_;
        Trace("dfr", fs, "blk=budget");
        r.due = meter_.Reschedule(r.due, [this, fs, gen] { Deliver(fs, gen); });
        return;
    }
    if (r.apply) r.apply();
    ++delivered_;
    if (waiting_) ++wait_delivered_; else if (polling_) ++poll_delivered_; else ++boundary_delivered_;
    if (waiting_ || polling_) {
        char extra[48];
        std::snprintf(extra, sizeof extra, "src=%s end=%.3f", waiting_ ? "wait-order" : "poll", r.end_ms);
        Trace("dlv", fs, extra);
    } else {
        Trace("dlv", fs, nullptr);
    }
    r.released = true;   // no completion gate in this engine: release follows delivery in the same duty
    Trace("rel", fs, "arm=B");
}

LoaderModel::PollOutcome LoaderModel::Poll(uint32_t fs) {
    // Poll visibility: the reads the modelled drive finished inside this frame
    // land now, at the poll that asks (a no-op under boundary visibility).
    if (now_tick_) {
        polling_ = true;
        meter_.CompleteNow(now_tick_());
        polling_ = false;
    }
    auto it = reads_.find(fs);
    if (it == reads_.end()) {
        note_ = PollOutcome::Progress;   // nothing outstanding: the recompiled path proceeds
        note_fs_ = fs;
        return note_;
    }
    if (it->second.released) {
        reads_.erase(it);
        ++consumed_;
        Trace("cns", fs);
        note_ = PollOutcome::Progress;
    } else {
        note_ = PollOutcome::HeldInflight;
    }
    note_fs_ = fs;
    return note_;
}

bool LoaderModel::InFlight(uint32_t fs) const {
    auto it = reads_.find(fs);
    return it != reads_.end() && !it->second.released;
}

bool LoaderModel::PollVisibility() const { return meter_.Visibility() == ps2r::CdvdDeliveryVisibility::Poll; }
std::shared_ptr<ReadTiming> LoaderModel::Timing() const { return meter_.Timing(); }
std::shared_ptr<ReadTiming::Budget> LoaderModel::TimingBudget() const { return budget_.TimingBudget(); }

bool LoaderModel::Released(uint32_t fs) const {
    auto it = reads_.find(fs);
    return it != reads_.end() && it->second.released;
}

bool LoaderModel::ShouldYieldPollSlice(uint32_t ra, uint32_t spin_site_ra, bool poll_ran_this_lap) {
    if (ra != spin_site_ra) return false;   // not the spin site: not a decision at all
    if (!poll_ran_this_lap || note_ != PollOutcome::HeldInflight) {
        ++declines_;
        return false;
    }
    if (!InFlight(note_fs_)) {   // re-verify: the read landed since the poll -- the next iteration consumes it
        ++declines_;
        return false;
    }
    note_ = PollOutcome::None;   // the note is consumed by the yield
    ++yields_;
    Trace("yld", note_fs_, "blk=inflt");
    return true;
}
