#include "engine/ipu/hle/dmac_channel_face_adapter.h"

#include <utility>

namespace ps2r::ipu {

DmacChannelFaceAdapter::DmacChannelFaceAdapter(::DmacChannelRegs& ipu_from, ::DmacChannelRegs& ipu_to,
                                               Kick kick)
    : ipu_from_(ipu_from), ipu_to_(ipu_to), kick_(std::move(kick)) {}

::DmacChannelRegs* DmacChannelFaceAdapter::Regs(uint32_t channel) {
    if (channel == kChannelIpuFrom) return &ipu_from_;
    if (channel == kChannelIpuTo) return &ipu_to_;
    ++out_of_range_;
    return nullptr;
}

uint32_t DmacChannelFaceAdapter::ReadChcr(uint32_t channel) {
    ::DmacChannelRegs* r = Regs(channel);
    return r ? r->chcr : 0;
}
void DmacChannelFaceAdapter::WriteChcr(uint32_t channel, uint32_t value) {
    if (::DmacChannelRegs* r = Regs(channel)) {
        r->chcr = value;
        if ((value & DmacChcr::kStr) && kick_) kick_(channel);
    }
}
uint32_t DmacChannelFaceAdapter::ReadMadr(uint32_t channel) {
    ::DmacChannelRegs* r = Regs(channel);
    return r ? r->madr : 0;
}
void DmacChannelFaceAdapter::WriteMadr(uint32_t channel, uint32_t value) {
    if (::DmacChannelRegs* r = Regs(channel)) r->madr = value;
}
uint32_t DmacChannelFaceAdapter::ReadTadr(uint32_t channel) {
    ::DmacChannelRegs* r = Regs(channel);
    return r ? r->tadr : 0;
}
void DmacChannelFaceAdapter::WriteTadr(uint32_t channel, uint32_t value) {
    if (::DmacChannelRegs* r = Regs(channel)) r->tadr = value;
}
uint32_t DmacChannelFaceAdapter::ReadQwc(uint32_t channel) {
    ::DmacChannelRegs* r = Regs(channel);
    return r ? r->qwc : 0;
}
void DmacChannelFaceAdapter::WriteQwc(uint32_t channel, uint32_t value) {
    if (::DmacChannelRegs* r = Regs(channel)) r->qwc = value;
}

}  // namespace ps2r::ipu
