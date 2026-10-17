// VuTierDispatch -- the ladder. Rules in the header.
#include "engine/vu/tiers/vu_tier_dispatch.h"

#include "engine/vu/vu_unit.h"

VuTierDispatch::VuTierDispatch(VuSettings settings, uint64_t cycle_cap)
    : settings_(settings), interp_(cycle_cap), cached_(interp_, cycle_cap), block_(interp_, cycle_cap), aot_(block_, interp_) {}

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
    block_.set_cycle_cap(cap);
}

void VuTierDispatch::Execute(VuUnit& unit, uint32_t start_pc) {
    if (unit.id() == 1) {
        if (settings_.hle_enabled && hle_ != nullptr && hle_(hle_user_, unit, start_pc, unit.micro_generation())) return;
        if (mtvu_ != nullptr && mtvu_(mtvu_user_, unit, start_pc)) return;
    }
    switch (settings_.vu1_mode) {
        case VuTierMode::Block:
            if (unit.id() == 1) block_.Execute(unit, start_pc);
            else cached_.Execute(unit, start_pc);
            break;
        case VuTierMode::Aot:
            if (unit.id() == 1) aot_.Execute(unit, start_pc);
            else cached_.Execute(unit, start_pc);
            break;
        case VuTierMode::Cached: cached_.Execute(unit, start_pc); break;
        default: interp_.Run(unit, start_pc); break;
    }
}

void VuTierDispatch::ResumeIfWaiting(VuUnit& unit) {
    Vu1State& st = unit.state();
    if (!st.suspended) return;
    st.suspended = false;
    Execute(unit, st.suspended_tpc);
}
