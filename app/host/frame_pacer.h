// app/host/frame_pacer.h -- FramePacer: WHICH presenter pumps show a NEW picture
// (video.frame_cap). Pure: no clock, no window -- the caller passes the wall time.
//
// The GS consumer posts a picture whenever the game finishes a frame (an irregular
// train when the game runs under 60 Hz). The presenter re-draws the window on its
// own train (video.fps_cap) and, between two draws, a newly posted picture used to
// be uploaded at the pump it arrived -- so the field count a picture stayed on
// screen was 1, 2 or 3 at random, the judder a player reports as "not smooth".
// The pacer decides at every pump whether the pending picture is shown NOW:
//
//   Hz60  the picture is admitted at the next 60 Hz field EDGE (a fixed train
//         re-anchored only when the pump fell a whole period behind): the first
//         title's cadence, the default;
//   Hz30  admitted at every OTHER field edge (a 33.3 ms train): a game producing
//         30..60 pictures per second shows one per edge at a fixed cadence; two
//         pictures inside one period -> the first is superseded (the game's tick
//         is NOT dropped: the clock contract paces the EE, not this class);
//   Off   admitted at the pump it arrived (the consumer's own cadence).
//
// An edge with nothing pending is a HELD edge (the window keeps the last picture;
// the next picture waits for the next edge -- the train stays steady rather than
// re-anchoring to a late arrival). Census: admitted / held edges / superseded
// pictures / re-anchored (late) edges.
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
    uint64_t PeriodNs() const;   // 0 under Off

    // One pump at wall `now_ns`; `pending` = a new picture waits to be shown. True =
    // show it now (the caller uploads and draws). A pending picture that is not
    // admitted stays pending; `Supersede()` is the caller's note that a newer picture
    // replaced it before it was shown.
    bool Admit(uint64_t now_ns, bool pending);
    void Supersede() { ++n_.superseded; }
    // ns from `now_ns` to the next admission edge (the wait bound): 0 = at once
    // (Off, or an edge already passed); the caller clamps it to its pump period.
    uint64_t ToNextEdge(uint64_t now_ns) const;

    struct Census {
        uint64_t admitted = 0;     // pictures shown
        uint64_t held = 0;         // edges with no picture pending
        uint64_t superseded = 0;   // pictures replaced before they were shown
        uint64_t late = 0;         // edges re-anchored (the pump fell a period behind)
    };
    const Census& Count() const { return n_; }
    // "cap=30:admitted=n:held=n:superseded=n:late=n"
    std::string CensusRow() const;

private:
    Cap cap_ = Cap::Hz60;
    uint64_t next_edge_ = 0;   // 0 = unanchored
    Census n_;
};

}  // namespace ps2r::app
