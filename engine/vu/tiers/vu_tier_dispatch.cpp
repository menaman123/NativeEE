// VuTierDispatch -- the ladder. Rules in the header.
#include "engine/vu/tiers/vu_tier_dispatch.h"

#include "engine/vu/vu_unit.h"

VuTierDispatch::VuTierDispatch(VuSettings settings, uint64_t cycle_cap)
    : settings_(settings), interp_(cycle_cap), cached_(interp_, cycle_cap) {}

void VuTierDispatch::SetHleHook(HleHook fn, void* user) {
    hle_ = fn;
    hle_user_ = user;
}

void VuTierDispatch::SetMtvuHook(MtvuHook fn, void* user) {
    mtvu_ = fn;
    mtvu_user_ = user;
}

void VuTierDispatch::set_cycle_cap(uint64_t cap) {
    interp_.set_cycle_cap(cap);
    cached_.set_cycle_cap(cap);
}

void VuTierDispatch::Execute(VuUnit& unit, uint32_t start_pc) {
    if (unit.id() == 1) {
        if (settings_.hle_enabled && hle_ != nullptr && hle_(hle_user_, unit, start_pc, unit.micro_generation())) return;
        if (mtvu_ != nullptr && mtvu_(mtvu_user_, unit, start_pc)) return;
    }
    if (settings_.vu1_mode == VuTierMode::Cached) {
        cached_.Execute(unit, start_pc);
    } else {
        interp_.Run(unit, start_pc);
    }
}

void VuTierDispatch::ResumeIfWaiting(VuUnit& unit) {
    Vu1State& st = unit.state();
    if (!st.suspended) return;
    st.suspended = false;
    Execute(unit, st.suspended_tpc);
}
