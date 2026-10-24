// app/host/frame_pacer.cpp -- FramePacer (frame_pacer.h).
#include "app/host/frame_pacer.h"

#include <algorithm>

namespace ps2r::app {

const char* FramePacer::CapName(Cap cap) {
    switch (cap) {
        case Cap::Off: return "off";
        case Cap::Hz30: return "30";
        case Cap::Hz60: return "60";
    }
    return "?";
}

bool FramePacer::CapOf(int value, Cap& out) {
    if (value < 0 || value > static_cast<int>(Cap::Hz60)) return false;
    out = static_cast<Cap>(value);
    return true;
}

void FramePacer::SetCap(Cap cap) {
    cap_ = cap;
    next_edge_ = 0;
}

uint64_t FramePacer::PeriodNs() const {
    switch (cap_) {
        case Cap::Off: return 0;
        case Cap::Hz30: return 2 * kFieldNs;
        case Cap::Hz60: return kFieldNs;
    }
    return kFieldNs;
}

bool FramePacer::Admit(uint64_t now_ns, uint64_t posted_ns) {
    const uint64_t period = PeriodNs();
    if (period == 0) {
        if (posted_ns) NoteShown(now_ns);
        return posted_ns != 0;
    }
    // Nothing waiting: nothing to decide. An edge already passed stays OPEN (the train
    // is not advanced): the next picture is shown the moment it arrives.
    if (posted_ns == 0) return false;
    if (next_edge_ == 0) next_edge_ = now_ns;   // the first picture anchors the train
    if (now_ns < next_edge_) return false;      // posted before the edge: waits for it
    const uint64_t behind = now_ns - next_edge_;
    if (posted_ns > next_edge_) {
        // The edge stood open and this picture came after it: show it now and re-anchor
        // the train here. Whole periods that passed empty are the production's misses.
        n_.held += behind / period;
        ++n_.slipped;
        n_.slip_ns += behind % period;
        next_edge_ = now_ns + period;
        relax_count_ = 0;                 // the train moved later: the relax window restarts
        relax_min_wait_ = UINT64_MAX;
    } else if (behind >= period) {
        // The picture waited since before the edge and the PUMP fell a whole period
        // behind (a display swap, a stalled main): re-anchor from now, no catch-up burst.
        next_edge_ = now_ns + period;
        ++n_.late;
        relax_count_ = 0;
        relax_min_wait_ = UINT64_MAX;
    } else {
        const uint64_t edge = next_edge_;
        next_edge_ += period;   // on time: the train stays
        if (relax_) NoteWait(edge - posted_ns);
    }
    NoteShown(now_ns);
    return true;
}

void FramePacer::SetRelax(bool on) {
    relax_ = on;
    relax_count_ = 0;
    relax_min_wait_ = UINT64_MAX;
}

void FramePacer::NoteWait(uint64_t wait_ns) {
    if (wait_ns < relax_min_wait_) relax_min_wait_ = wait_ns;
    if (++relax_count_ < kRelaxWindow) return;
    // Every picture of the window waited at least relax_min_wait_: pull the train earlier
    // by the excess over the margin, at most a step (one shorter gap, never a longer one).
    if (relax_min_wait_ != UINT64_MAX && relax_min_wait_ > kRelaxMarginNs) {
        const uint64_t pull = std::min(relax_min_wait_ - kRelaxMarginNs, kRelaxStepNs);
        next_edge_ -= std::min(pull, next_edge_);
        ++n_.relaxed;
        n_.relax_ns += pull;
    }
    relax_count_ = 0;
    relax_min_wait_ = UINT64_MAX;
}

void FramePacer::NoteShown(uint64_t now_ns) {
    ++n_.admitted;
    // A doubled frame: this picture came more than 1.2 periods after the previous shown one
    // (Hz60 / Off: 20 ms, the judge's gate; Hz30: 40 ms, its own cadence is not a double) --
    // the previous picture stood a period too long.
    const uint64_t period = PeriodNs() ? PeriodNs() : kFieldNs;
    if (last_shown_ns_ != 0 && now_ns > last_shown_ns_ && now_ns - last_shown_ns_ > period + period / 5) ++n_.doubled;
    last_shown_ns_ = now_ns;
}

uint64_t FramePacer::ToNextEdge(uint64_t now_ns) const {
    if (PeriodNs() == 0 || next_edge_ == 0 || now_ns >= next_edge_) return 0;
    return next_edge_ - now_ns;
}

std::string FramePacer::CensusRow() const {
    return std::string("cap=") + CapName(cap_) + ":admitted=" + std::to_string(n_.admitted) + ":held=" + std::to_string(n_.held) +
           ":superseded=" + std::to_string(n_.superseded) + ":late=" + std::to_string(n_.late) +
           ":slipped=" + std::to_string(n_.slipped) + ":slip_ms=" + std::to_string(n_.slip_ns / 1000000) +
           ":doubled=" + std::to_string(n_.doubled) + ":relaxed=" + std::to_string(n_.relaxed) + ":relax_ms=" + std::to_string(n_.relax_ns / 1000000);
}

}  // namespace ps2r::app
