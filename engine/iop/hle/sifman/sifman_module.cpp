// engine/iop/hle/sifman/sifman_module.cpp -- SifmanModule: sifman's synchronous SIF DMA.
#include "engine/iop/hle/sifman/sifman_module.h"

namespace ps2r::iop::hle {

void SifmanModule::sceSifInit(IopContext& ctx) { ctx.gpr[2] = 0; }

void SifmanModule::sceSifSetDma(IopContext& ctx) {
    const uint32_t desc = ctx.gpr[4];
    const uint32_t count = ctx.gpr[5];
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t entry = desc + i * kDescriptorStride;
        const uint32_t src = mem_.Read<uint32_t>(entry + 0);
        const uint32_t dst = mem_.Read<uint32_t>(entry + 4);
        const uint32_t size = mem_.Read<uint32_t>(entry + 8);
        const uint8_t* iop_src = mem_.Pointer(src, size);
        if (iop_src && sink_ && sink_->WriteEeRam(dst, iop_src, size)) bytes_copied_ += size;
    }
    ctx.gpr[2] = next_dma_id_++;
}

// The copy ran inside sceSifSetDma, so every transfer is complete: the SDK's -1.
void SifmanModule::sceSifDmaStat(IopContext& ctx) {
    ++dma_stat_polls_;
    ctx.gpr[2] = static_cast<uint32_t>(kDmaStatComplete);
}

void SifmanModule::sceSifCheckInit(IopContext& ctx) { ctx.gpr[2] = 1; }

}  // namespace ps2r::iop::hle
