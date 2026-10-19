// VuUnit -- the unit record and its pipelines. Rules in the header.
#include "engine/vu/vu_unit.h"

#include <cstring>

#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu_float.h"

VuUnit::VuUnit(int id, GifArbiter* arbiter, bool xgkick_paced_drain, bool lazy_flags)
    : id_(id), state_(new Vu1State()), flags_(lazy_flags) {
    state_->mem_mask = (id == 0) ? Vu1State::kVu0MemMask : Vu1State::kVu1MemMask;
    std::memset(&state_->regs, 0, sizeof(state_->regs));
    state_->regs.Reset();
    std::memset(state_->micro, 0, sizeof(state_->micro));
    std::memset(state_->data, 0, sizeof(state_->data));
    if (arbiter != nullptr) xgk_.reset(new XgKick(*arbiter, xgkick_paced_drain));
    dispatch_.reset(new VuTierDispatch());
    Reset();
}

VuUnit::~VuUnit() = default;

void VuUnit::Execute(uint32_t start_pc) {
    if (kick_drop_) {
        ++dropped_kicks_;
        return;
    }
    dispatch_->Execute(*this, start_pc);
}

void VuUnit::ResumeIfWaiting() { dispatch_->ResumeIfWaiting(*this); }

void VuUnit::Reset() {
    if (xgk_) xgk_->Reset();
    stalls_.Reset();
    flags_.SetPPending(false, 0, 0.0f);
    state_->suspended = false;
    state_->suspended_tpc = 0;
    state_->xtop_read_count = 0;
    state_->xtop_exit_requested = false;
    state_->xgkick_has_fired = false;
    state_->xgkick_count = 0;
    state_->cur_bundle_pc = 0;
}

void VuUnit::WriteDataMem(uint32_t addr, const uint8_t* src, size_t size) {
    const uint32_t mask = state_->mem_mask;
    for (size_t i = 0; i < size; ++i) state_->data[(addr + i) & mask] = src[i];
}

void VuUnit::WriteMicroMem(uint32_t addr, const uint8_t* src, size_t size) {
    const uint32_t mask = state_->mem_mask;
    for (size_t i = 0; i < size; ++i) state_->micro[(addr + i) & mask] = src[i];
    ++state_->micro_generation;
}

void VuUnit::ReadDataMem(uint32_t addr, uint8_t* dst, size_t size) const {
    const uint32_t mask = state_->mem_mask;
    for (size_t i = 0; i < size; ++i) dst[i] = state_->data[(addr + i) & mask];
}

void VuUnit::BumpMicroEpoch() {
    ++state_->micro_generation;
}

void VuUnit::SetVifTop(uint16_t top, uint16_t itop) {
    vif_top_ = top;
    vif_itop_ = itop;
}

void VuUnit::LoadCase(const Vu1Case& c) {
    std::memcpy(&state_->regs, &c.regs, sizeof(VUregs));
    std::memcpy(state_->micro, c.micro_mem, Vu1Case::kMicroSize);
    std::memcpy(state_->data, c.data_mem, Vu1Case::kDataSize);
    ++state_->micro_generation;
    vif_top_ = static_cast<uint16_t>(c.vif1_top);
    vif_itop_ = static_cast<uint16_t>(c.vif1_itops);  // XITOP reads the ITOPS shadow
    flags_.SetQPending(c.q_pending_valid != 0, c.q_pending_retire, VuFloat::Bits2F(c.q_pending_value_bits),
                       static_cast<uint16_t>(c.q_pending_status));
    flags_.SetPPending(c.p_pending_valid != 0, c.p_pending_retire, VuFloat::Bits2F(c.p_pending_value_bits));
    flags_.SetCycle(c.vu_cycle);
}

void VuUnit::CopyStateFrom(const VuUnit& other) {
    *state_ = *other.state_;
    flags_ = other.flags_;
    stalls_ = other.stalls_;
    if (xgk_ && other.xgk_) xgk_->CopyDrainState(*other.xgk_);
    vif_top_ = other.vif_top_;
    vif_itop_ = other.vif_itop_;
}

void VuUnit::SaveCase(Vu1Case& c, uint32_t start_pc) const {
    std::memset(&c, 0, sizeof(c));
    c.magic = Vu1Case::kMagic;
    c.version = Vu1Case::kVersion;
    c.start_pc = start_pc;
    std::memcpy(&c.regs, &state_->regs, sizeof(VUregs));
    std::memcpy(c.micro_mem, state_->micro, Vu1Case::kMicroSize);
    std::memcpy(c.data_mem, state_->data, Vu1Case::kDataSize);
    c.vif1_top = vif_top_;
    c.vif1_tops = vif_top_;
    c.vif1_itop = vif_itop_;
    c.vif1_itops = vif_itop_;
    c.q_pending_valid = flags_.q_pending() ? 1 : 0;
    c.p_pending_valid = flags_.p_pending() ? 1 : 0;
    c.q_pending_retire = flags_.q_pending_retire();
    c.q_pending_value_bits = VuFloat::F2Bits(flags_.q_pending_value());
    c.q_pending_status = flags_.q_pending_status();
    c.p_pending_retire = flags_.p_pending_retire();
    c.p_pending_value_bits = VuFloat::F2Bits(flags_.p_pending_value());
    c.vu_cycle = flags_.cycle();
}
