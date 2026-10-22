// app/host/frame_pacer.cpp -- FramePacer (frame_pacer.h).
#include "app/host/frame_pacer.h"

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
        if (posted_ns) ++n_.admitted;
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
    } else if (behind >= period) {
        // The picture waited since before the edge and the PUMP fell a whole period
        // behind (a display swap, a stalled main): re-anchor from now, no catch-up burst.
        next_edge_ = now_ns + period;
        ++n_.late;
    } else {
        next_edge_ += period;   // on time: the train stays
    }
    ++n_.admitted;
    return true;
}

uint64_t FramePacer::ToNextEdge(uint64_t now_ns) const {
    if (PeriodNs() == 0 || next_edge_ == 0 || now_ns >= next_edge_) return 0;
    return next_edge_ - now_ns;
}

std::string FramePacer::CensusRow() const {
    return std::string("cap=") + CapName(cap_) + ":admitted=" + std::to_string(n_.admitted) + ":held=" + std::to_string(n_.held) +
           ":superseded=" + std::to_string(n_.superseded) + ":late=" + std::to_string(n_.late) +
           ":slipped=" + std::to_string(n_.slipped) + ":slip_ms=" + std::to_string(n_.slip_ns / 1000000);
}

}  // namespace ps2r::app
