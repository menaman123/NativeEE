#pragma once
// BoundaryProtocol: how a guest EE thread WAITS for emulated time, written
// once. Two waits exist and both are consumption-driven -- no host sleep
// ever stands in for a field (the WAITFIX rule: condition variables and
// absolute deadlines, never poll+sleep quanta):
//
//  * The frame boundary (sceGsSyncV / WaitForVblank) is the sequencer's
//    fixed order: NoteArrival -> pace park (rendezvous ticket) -> the field
//    tick (the quantum) -> OnBoundary settle -> timer pump -> ISR dispatch.
//    That order lives in BoundarySequencer; this class supplies the pieces
//    the sequencer does not own.
//  * The timer wait (KernlTimer's TimerWaitMicroseconds busy-wait on T0):
//    the waiter polls the derived clock without reaching a boundary, so
//    under the deterministic policy the wait hook DEMANDS one field tick
//    in-line (TickSync Demand; a demand from a non-guest thread is deferred
//    to the next boundary). EmuDomain absorbs at most kIdleReleaseTicks
//    demanded ticks under an exhausted quantum, then rides 1:1, so the wait
//    always terminates in a bounded number of demands. Under Play the hook
//    parks on the tick condition variable until the pacer's next tick
//    (NotifyTick), bounded by one field period -- a deadline, not a poll --
//    then fires the ticks the pacer noted (TickSync pending) on the waiter,
//    the guest fixed point; the pacer itself never runs a tick body.
//  * The wall-locked boundary (Play with a host pacer bound, DP-13 (a)): the
//    sequencer's TickAdvance slot does not fire a Boundary tick of its own; it
//    parks here (ParkForPacerTick) until the pacer's next noted tick and then
//    fires it as the field -- one T0 field per wall vblank the EE waited for,
//    the reference's paced vblank wait. A late EE (frame work > period) finds
//    its tick already noted and does not park (counted as late); the further
//    ticks noted during the work are the fields that elapsed meanwhile and the
//    sequencer delivers them 1:1 (the clock contract, rule 3); the next
//    boundary parks for the next edge again -- never faster than the wall.
//    Deterministic / Freerun: never consulted.
//
// Pace predicate (OLD ee_pace, the depth park PLAY keeps -- the rendezvous
// is the bound under the deterministic profile): the EE parks at a boundary
// while declared > processed + runahead (Config engine.ee_runahead, default
// 2, clamped [kRunaheadMin, kRunaheadMax]); un-processed EE frames never
// exceed runahead + 1.
#include <condition_variable>
#include <cstdint>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "engine/ee/timing/tick_source.h"
#include "engine/ee/timing/tick_sync.h"

class BoundaryProtocol {
public:
    static constexpr uint32_t kRunaheadDefault = 2;
    static constexpr uint32_t kRunaheadMin = 1;
    static constexpr uint32_t kRunaheadMax = 4;

    // Pure: park while declared > processed + lag.
    static bool ShouldPark(uint32_t declared, uint32_t processed, uint32_t lag) { return declared > processed + lag; }
    // Runahead depth sample at boundary entry: clamped at 0 (a declare
    // interleave can make processed transiently exceed a captured declare).
    static uint64_t Depth(uint32_t declared, uint32_t processed) {
        return declared > processed ? static_cast<uint64_t>(declared - processed) : 0u;
    }

    BoundaryProtocol(TickSource& source, TickSync& ticks);

    void SetRunahead(uint32_t lag);
    uint32_t Runahead() const { return runahead_; }
    bool ShouldPark(uint32_t declared, uint32_t processed) const { return ShouldPark(declared, processed, runahead_); }

    // The KernlTimer wait hook: block until the emu clock may have advanced
    // past `deadline` (T0 counts; the caller re-reads its clock).
    void Wait(uint64_t deadline);
    std::function<void(uint64_t)> WaitHook();
    // Play pacer: a tick fired (wakes the timer waiters and the parked boundary).
    void NotifyTick();
    // The wall-locked boundary (Play policy with a host pacer): the boundary's
    // field tick IS the pacer's. With no pacer tick pending the boundary parks
    // on the tick condition variable until the pacer notes one (an absolute
    // deadline of kBoundaryParkPeriods field periods bounds a stopped pacer;
    // the reference parked until the NEXT vblank edge after the frame's
    // work). Returns true when it parked; a boundary that found a tick already
    // noted was LATE (frame work > period) and is counted, never burst-repaid.
    bool ParkForPacerTick();
    static constexpr uint32_t kBoundaryParkPeriods = 2;
    uint64_t PaceParks() const { return pace_parks_; }
    uint64_t PaceLate() const { return pace_late_; }
    uint64_t PaceTimeouts() const { return pace_timeouts_; }
    // Wall time the parked boundaries slept to their 60 Hz edge (the early EE's
    // slack; never converted into a tick).
    uint64_t PaceSleptUs() const { return pace_slept_us_; }
    // The EE's own frame work (the wall time from one ParkForPacerTick's return
    // to the next call): the longest seen, and the first kLateTraceMax boundaries
    // that found two or more ticks noted (a frame longer than one period) as
    // "ordinal:work_us:pending" -- the census the DONE band is read against.
    uint64_t WorkMaxUs() const { return work_max_us_; }
    // A second maximum over the boundaries since the last MarkWorkWindow() (the
    // guest thread's; 0 until marked): the census of a phase of the run -- e.g.
    // the longest frame work from the first scripted display change on.
    void MarkWorkWindow();
    bool WorkWindowMarked() const { return work_marked_; }
    uint64_t WorkMaxSinceMarkUs() const { return work_marked_max_us_; }
    static constexpr size_t kLateTraceMax = 64;
    const std::string& LateTrace() const { return late_trace_; }
    uint64_t LateTraceCount() const { return late_trace_count_; }
    // The LONG ticks (Charter 3 item 3): every boundary whose frame work exceeded
    // one field period, as "ordinal:work_us:pending:<TickProfile::Format>" -- the
    // per-tick wall breakdown by host site (loader, tex, mesh, shadow, sky, save,
    // alloc, gs, boundary, other = the title's own code); the first kLongTraceMax
    // are kept, every one is counted. The per-site totals over the run and the
    // longest tick's breakdown are kept beside it.
    static constexpr size_t kLongTraceMax = 160;
    const std::string& LongTrace() const { return long_trace_; }
    uint64_t LongTraceCount() const { return long_trace_count_; }
    const std::string& WorkMaxBreakdown() const { return work_max_breakdown_; }
    std::string SiteTotals() const;
    // Load windows in boundary ordinals ("first-last,first-last,..."): a tick whose
    // loader site moved bytes or waited a completion, OR on which the TITLE'S chunk
    // state machine stepped a loading/unloading/releasing chunk or ran a chunk's
    // FIRST shown update (TickProfile::NoteChunkEvent -- the cut), is a load tick;
    // load ticks closer than kLoadGapTicks (one second of boundaries) form one
    // window. The windows RootNoLongTick excludes; a long tick outside them is the
    // finding. The window is the title's own load phase: file windows + the chunk
    // transitions + the cut, so there is NO tail -- the tick after a window is a
    // play tick and is judged (kLoadTailTicks = 0, published for the judge).
    static constexpr uint64_t kLoadGapTicks = 60;
    static constexpr uint64_t kLoadTailTicks = 0;
    std::string LoadWindows() const;
    uint64_t LoadTicks() const { return load_ticks_; }
    // The cut ticks ("ord,ord,..."; the first kCutTraceMax): every boundary whose
    // tick ran a chunk's first Shown step. Chunk-event ticks by kind beside it.
    static constexpr size_t kCutTraceMax = 64;
    std::string ChunkCuts() const { return chunk_cuts_.empty() ? "none" : chunk_cuts_; }
    uint64_t ChunkTicks() const { return chunk_ticks_; }
    uint64_t ChunkCutCount() const { return chunk_cut_count_; }
    // Guest-thread identity for the demand path (non-guest demands defer).
    void SetGuestThreadPredicate(std::function<bool()> is_guest) { is_guest_ = std::move(is_guest); }

    uint64_t Waits() const { return waits_; }
    uint64_t DeferredDemands() const { return deferred_; }

private:
    TickSource& source_;
    TickSync& ticks_;
    std::function<bool()> is_guest_;
    uint32_t runahead_ = kRunaheadDefault;
    uint64_t waits_ = 0;
    uint64_t deferred_ = 0;
    uint64_t pace_parks_ = 0;
    uint64_t pace_late_ = 0;
    uint64_t pace_timeouts_ = 0;
    uint64_t pace_slept_us_ = 0;
    std::chrono::steady_clock::time_point work_from_{};
    bool work_from_valid_ = false;
    uint64_t work_max_us_ = 0;
    bool work_marked_ = false;
    uint64_t work_marked_max_us_ = 0;
    std::string late_trace_;
    uint64_t late_trace_count_ = 0;
    std::string long_trace_;
    uint64_t long_trace_count_ = 0;
    std::string work_max_breakdown_;
    uint64_t site_total_us_[16] = {};
    uint64_t site_total_calls_[16] = {};
    struct LoadWindow { uint64_t first, last; };
    std::vector<LoadWindow> load_windows_;
    uint64_t load_ticks_ = 0;
    uint64_t chunk_ticks_ = 0;
    uint64_t chunk_cut_count_ = 0;
    std::string chunk_cuts_;
    // Books the tick's work against the profile sample (the trace rows above).
    void NoteWork(uint64_t ordinal, uint64_t work_us, uint64_t pending, uint32_t period_us);
    std::mutex mu_;
    std::condition_variable cv_;
    uint64_t tick_generation_ = 0;
};
