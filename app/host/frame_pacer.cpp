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

bool FramePacer::Admit(uint64_t now_ns, bool pending) {
    const uint64_t period = PeriodNs();
    if (period == 0) {
        if (pending) ++n_.admitted;
        return pending;
    }
    if (next_edge_ == 0) next_edge_ = now_ns;   // the first pump anchors the train
    if (now_ns < next_edge_) return false;      // between two edges: the picture waits
    // At (or past) an edge: advance the train. A pump that fell a whole period behind
    // (a display swap, a stalled main) re-anchors from now instead of firing a burst
    // of catch-up edges.
    if (now_ns - next_edge_ >= period) {
        next_edge_ = now_ns + period;
        ++n_.late;
    } else {
        next_edge_ += period;
    }
    if (pending) ++n_.admitted;
    else ++n_.held;
    return pending;
}

uint64_t FramePacer::ToNextEdge(uint64_t now_ns) const {
    if (PeriodNs() == 0 || next_edge_ == 0 || now_ns >= next_edge_) return 0;
    return next_edge_ - now_ns;
}

std::string FramePacer::CensusRow() const {
    return std::string("cap=") + CapName(cap_) + ":admitted=" + std::to_string(n_.admitted) + ":held=" + std::to_string(n_.held) +
           ":superseded=" + std::to_string(n_.superseded) + ":late=" + std::to_string(n_.late);
}

}  // namespace ps2r::app
