#include "engine/ee/timing/emu_domain.h"

EmuDomain::Snapshot EmuDomain::Capture() const {
    return {emu_count_, emu_base_, boundary_raw_, arrival_raw_, boundaries_, lead_, regime_, pin_,
        have_boundary_, bypass_, pacer_release_, det_.Capture()};
}

bool EmuDomain::Validate(const Snapshot& state, uint64_t raw_tick) {
    // Leave headroom for the existing signed settle arithmetic and the
    // largest derived counter (4,915,200 per tick). This is a file-input
    // bound, not a change to live scheduling.
    constexpr uint64_t max_tick = UINT64_MAX / 4915200 - 16;
    return raw_tick <= max_tick && state.tick <= max_tick && state.boundaries <= max_tick &&
        state.lead >= -static_cast<int64_t>(max_tick) && state.lead <= static_cast<int64_t>(max_tick) &&
        state.base <= state.tick && state.boundary_raw <= raw_tick && state.arrival_raw <= raw_tick &&
        state.regime >= 1 && state.regime <= 5 && (state.pin == -1 || (state.pin >= 1 && state.pin <= 5)) &&
        RegimeDetector::Validate(state.detector) &&
        state.regime == static_cast<uint32_t>(state.pin > 0 ? state.pin : state.detector.n);
}

bool EmuDomain::Restore(const Snapshot& state, uint64_t raw_tick) {
    if (!Validate(state, raw_tick)) return false;
    emu_count_ = state.tick; emu_base_ = state.base; boundary_raw_ = state.boundary_raw;
    arrival_raw_ = state.arrival_raw; boundaries_ = state.boundaries; lead_ = state.lead;
    regime_ = state.regime; pin_ = state.pin; have_boundary_ = state.have_boundary;
    bypass_ = state.bypass; pacer_release_ = state.pacer_release;
    return det_.Restore(state.detector);
}

uint64_t EmuDomain::BoundaryAdvance(uint64_t quantum, uint64_t carried, uint64_t final_gap,
                                    int64_t lead_before, int64_t* lead_after) {
    int64_t allowed = static_cast<int64_t>(final_gap) + (kLeadSlackTicks - lead_before);
    if (allowed < 0) allowed = 0;
    uint64_t adv = quantum < static_cast<uint64_t>(allowed) ? quantum : static_cast<uint64_t>(allowed);
    if (adv < carried) adv = carried;  // never regress the counter
    *lead_after = lead_before + static_cast<int64_t>(adv) - static_cast<int64_t>(final_gap);
    return adv;
}

void EmuDomain::SetRegimePin(int n) {
    pin_ = (n >= RegimeDetector::kMinN && n <= RegimeDetector::kMaxN) ? n : -1;
    // Pinned: the quantum is the pin. Unpinned: resync to the detector's
    // current estimate at once (it kept sampling while the pin was held).
    regime_ = static_cast<uint32_t>(pin_ > 0 ? pin_ : det_.n());
}

bool EmuDomain::InLoop(uint64_t raw_count) const {
    return have_boundary_ && raw_count - boundary_raw_ <= kIdleReleaseTicks;
}

void EmuDomain::OnRawTick(uint64_t raw_count) {
    if (InLoop(raw_count) && !bypass_) {
        const uint64_t cap = emu_base_ + regime_;
        if (emu_count_ >= cap) {        // frame quantum exhausted: absorb the tick
            ++absorbed_;
            return;
        }
    }
    ++emu_count_;
}

void EmuDomain::OnDeferredTick(uint64_t raw_count) {
    if (!pacer_release_) {
        OnRawTick(raw_count);
        return;
    }
    ++emu_count_;   // the pacer's wall vblank: one field, never absorbed
}

void EmuDomain::NoteArrival(uint64_t raw_count) { arrival_raw_ = raw_count; }

void EmuDomain::Reseed(uint64_t raw_count, bool in_loop) {
    det_.Seed();
    regime_ = static_cast<uint32_t>(pin_ > 0 ? pin_ : det_.n());
    lead_ = 0;
    emu_base_ = emu_count_;
    boundary_raw_ = raw_count;
    have_boundary_ = in_loop;
}

void EmuDomain::OnBoundary(uint64_t raw_count) {
    ++boundaries_;
    const bool was_in_loop = InLoop(raw_count);
    if (!was_in_loop || bypass_) {
        // First boundary, return from an idle phase, or a bypassed generator:
        // the counter was riding 1:1 and stays continuous; only the quantum
        // bookkeeping and the governor restart.
        Reseed(raw_count, true);
        return;
    }
    if (pacer_release_) {
        // Wall-locked Play (the clock contract): the wall is the authority. The
        // fields the boundary fired 1:1 ARE the frame's time -- no quantum top-up
        // and no regime estimate may add a field the wall did not produce (rule 1),
        // nor withhold one it did (rule 3). The bookkeeping alone moves.
        lead_ = 0;
        emu_base_ = emu_count_;
        boundary_raw_ = raw_count;
        return;
    }
    const uint64_t arrival_gap = arrival_raw_ > boundary_raw_ ? arrival_raw_ - boundary_raw_ : 0;
    const uint64_t final_gap = raw_count > boundary_raw_ ? raw_count - boundary_raw_ : 0;
    // The quantum settled now is the regime GRANTED at the previous boundary.
    const uint32_t old_n = regime_;
    if (pin_ <= 0 && det_.OnBoundary(arrival_gap, final_gap))
        regime_ = static_cast<uint32_t>(det_.n());
    const uint64_t carried = emu_count_ > emu_base_ ? emu_count_ - emu_base_ : 0;
    int64_t lead_after = 0;
    const uint64_t adv = BoundaryAdvance(old_n, carried, final_gap, lead_, &lead_after);
    const uint64_t settled = emu_base_ + adv;
    if (settled > emu_count_) emu_count_ = settled;
    lead_ = lead_after;
    emu_base_ = emu_count_;
    boundary_raw_ = raw_count;
}

void EmuDomain::ReAnchor(uint64_t raw_count) {
    boundaries_ = 0;
    Reseed(raw_count, false);  // not in the loop until the next boundary
}
