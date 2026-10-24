// app/host/frame_pacer.h -- FramePacer: WHICH presenter pumps show a NEW picture
// (video.frame_cap). Pure: no clock, no window -- the caller passes the wall time.
//
// The GS consumer posts a picture whenever the game finishes a frame (an irregular
// train when the game runs under 60 Hz, a jittery one when it runs at 60 with the
// consumer's 11-20 ms per frame on top). The presenter re-draws the window on its
// own train (video.fps_cap) and, between two draws, a newly posted picture used to
// be uploaded at the pump it arrived -- so the field count a picture stayed on
// screen was 1, 2 or 3 at random, the judder a player reports as "not smooth".
// The pacer decides at every pump whether the OLDEST waiting picture is shown NOW:
//
//   Hz60  shown at a 60 Hz field EDGE: a picture posted BEFORE the edge waits for it
//         (the jitter of an early producer is absorbed; the train stays fixed); a
//         picture posted AFTER the edge -- the edge stood open, nothing to show --
//         is shown AT ONCE and the train re-anchors from that instant (a SLIP: the
//         late picture costs its lateness of latency, never a whole held field).
//         Round 3: the previous rule HELD the edge (advanced the train with nothing
//         shown) and showed the late picture at the NEXT edge -- every picture that
//         missed its edge by a little became a 33 ms doubled frame (16 % of the
//         Paris frames on the integrated build; 142 of 899 gaps, exactly bimodal).
//   Hz30  the same at every OTHER edge (a 33.3 ms train).
//   Off   shown at the pump it arrived (the consumer's own cadence).
//
// The caller keeps the waiting pictures in order (a two-deep FIFO: the pending one
// and the one behind it) and shows one per admission, so two pictures posted inside
// one period are shown at CONSECUTIVE edges -- the older is never thrown away for the
// newer (the previous rule superseded it: 36 pictures lost per 1200 in Paris). A third
// picture arriving with two already waiting supersedes the oldest (over-production;
// cannot happen at <= 60 pictures/s once the train follows the producer).
//
// RELAX (round 5, video.present_relax): the train above only ever moves LATER (a slip
// re-anchors to a late picture; an early picture waits), so its phase settles at the
// LATEST arrival it has seen -- in Paris the first, slow frames of a restored state
// anchored it ~15-24 ms after the steady arrivals and every picture then waited that
// long for its edge: a field of input latency bought nothing. With relax on, the
// pacer tracks the SMALLEST wait (edge - posted) of the last kRelaxWindow on-time
// admissions; when even that is above kRelaxMarginNs (3 ms) the train is pulled EARLIER by
// the excess (at most kRelaxStepNs per window: one shorter gap, never a longer one),
// so the edge converges to just after the latest typical arrival; a late picture
// still slips (the train moves later again) and the window restarts. Census
// relaxed= (pulls) / relax_ms (the summed pull).
//
// Census: admitted / held (whole periods that passed with no picture available: the
// production's own misses) / superseded / late (the PUMP fell a period behind with a
// picture already waiting: a display swap, a stalled main) / slipped (re-anchors to
// a late picture) / slip_ns (their summed lateness) / doubled (a shown picture more
// than 1.2 periods after the previous one: the previous stood a period too long -- the
// F1 menu's live doubled-frame counter; under Hz60 the judge's ">20 ms" gate in-process).
#pragma once

#include <cstdint>
#include <string>

namespace ps2r::app {

class FramePacer {
public:
    // The Config FrameCap order (video.frame_cap: off | 30 | 60).
    enum class Cap : uint8_t { Off = 0, Hz30 = 1, Hz60 = 2 };
    static constexpr uint64_t kFieldNs = 16666667;   // the 60 Hz field

    static const char* CapName(Cap cap);   // "off" | "30" | "60"
    static bool CapOf(int value, Cap& out);   // the Display row's / Config's index -> Cap

    // Any time (the pacer's owner thread): the cap in force; the edge train re-anchors
    // at the next pump.
    void SetCap(Cap cap);
    Cap cap() const { return cap_; }
    // The relax policy (above); off = the round-3 train.
    void SetRelax(bool on);
    bool Relax() const { return relax_; }
    static constexpr uint32_t kRelaxWindow = 32;          // on-time admissions per decision (~0.5 s at 60)
    static constexpr uint64_t kRelaxMarginNs = 3000000;   // the wait kept above the latest arrival (3 ms: at 2 the GL Paris train slipped ~1 % of pictures)
    static constexpr uint64_t kRelaxStepNs = 4000000;     // the largest pull per window (4 ms)
    uint64_t PeriodNs() const;   // 0 under Off

    // One pump at wall `now_ns`; `posted_ns` = the wall instant the OLDEST waiting
    // picture was posted (0 = none waiting). True = show it now (the caller uploads and
    // draws, then moves the next waiting picture up). A waiting picture that is not
    // admitted keeps waiting; `Supersede()` is the caller's note that it dropped the
    // oldest waiting picture because a third one arrived.
    bool Admit(uint64_t now_ns, uint64_t posted_ns);
    void Supersede() { ++n_.superseded; }
    // ns from `now_ns` to the next admission edge (the wait bound): 0 = at once
    // (Off, or an edge already passed); the caller clamps it to its pump period.
    uint64_t ToNextEdge(uint64_t now_ns) const;
    // The next edge of the train (0 = unanchored); for tests and the session menu.
    uint64_t NextEdgeNs() const { return next_edge_; }

    struct Census {
        uint64_t admitted = 0;     // pictures shown
        uint64_t held = 0;         // whole periods that passed with no picture available
        uint64_t superseded = 0;   // pictures dropped unshown (a third one arrived)
        uint64_t late = 0;         // pump fell a period behind with a picture waiting (re-anchored)
        uint64_t slipped = 0;      // train re-anchored to a picture posted after its edge
        uint64_t slip_ns = 0;      // the summed lateness of the slips
        uint64_t doubled = 0;      // shown pictures > 1.2 periods after the previous (a doubled frame; Off: fields)
        uint64_t relaxed = 0;      // train pulls earlier (the relax policy)
        uint64_t relax_ns = 0;     // ... their summed pull
    };
    const Census& Count() const { return n_; }
    // "cap=60:admitted=n:held=n:superseded=n:late=n:slipped=n:slip_ms=n:doubled=n:relaxed=n:relax_ms=n"
    std::string CensusRow() const;

private:
    void NoteShown(uint64_t now_ns);
    void NoteWait(uint64_t wait_ns);   // an on-time admission's wait (the relax policy)
    Cap cap_ = Cap::Hz60;
    bool relax_ = false;
    uint32_t relax_count_ = 0;
    uint64_t relax_min_wait_ = UINT64_MAX;
    uint64_t next_edge_ = 0;       // 0 = unanchored
    uint64_t last_shown_ns_ = 0;   // the previous admission (the doubled-frame census)
    Census n_;
};

}  // namespace ps2r::app
