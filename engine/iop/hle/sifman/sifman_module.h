// engine/iop/hle/sifman/sifman_module.h -- SifmanModule: the IOP sifman
// library as a hosted module sees it: sceSifInit (idx 5), sceSifSetDma (7),
// sceSifDmaStat (8), sceSifCheckInit (29), sceSifSetDmaIntr (32). A SetDma is a synchronous copy of
// each {src, dst, size} descriptor (16-byte stride) from IOP RAM into EE RAM
// through the bound EeRamSink, so the transfer is complete when the call
// returns. sceSifDmaStat follows the SDK contract for a completed transfer:
// it returns -1 for every id (the SDK's values are -1 = complete, 0 = queued,
// 1 = in progress; the module's post-DMA poll waits for a NEGATIVE status --
// `sceSifDmaStat(id)` then `bgez v0 -> loop` -- and would spin forever on 0).
// Ids are not tracked: an id sifman never issued is also reported complete,
// which is what the SDK reports once its ring has recycled the entry.
#pragma once

#include <cstdint>
#include <functional>

#include "engine/iop/hle/sifman/ee_ram_sink.h"
#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop {
class IopKernel;
}

namespace ps2r::iop::hle {

class SifmanModule {
public:
    static constexpr uint32_t kDescriptorStride = 16;
    static constexpr int32_t kDmaStatComplete = -1;   // SDK sifman: the transfer identified by `id` has completed

    SifmanModule(IopMemory& mem, EeRamSink* sink) : mem_(mem), sink_(sink) {}

    void SetSink(EeRamSink* sink) { sink_ = sink; }

    void sceSifInit(IopContext& ctx);       // int () -> 0
    void sceSifSetDma(IopContext& ctx);     // u32 (SifDmaTransfer* desc, int count) -> dma id (>= 1)
    void sceSifDmaStat(IopContext& ctx);    // int (id) -> -1 (complete: the copy ran inside SetDma)
    void sceSifCheckInit(IopContext& ctx);  // int () -> 1 (initialized)
    // u32 (desc, count, func, data) -> dma id: the same copy, then func(data) as a
    // deferred kernel event (the DMA-complete interrupt) through SetCompletion.
    void sceSifSetDmaIntr(IopContext& ctx);

    // The completion path of sceSifSetDmaIntr: the kernel that carries the deferred
    // event and the runner that invokes the guest handler (addr, ctx with $a0 = data).
    using HandlerRunner = std::function<void(uint32_t addr, IopContext& ctx)>;
    void SetCompletion(IopKernel* kernel, HandlerRunner run);
    static constexpr uint64_t kIntrCompletionUs = 50;

    uint32_t NextDmaId() const { return next_dma_id_; }
    void SetNextDmaId(uint32_t id) { next_dma_id_ = id ? id : 1; }   // savestate restore
    uint64_t BytesCopied() const { return bytes_copied_; }
    uint64_t DmaStatPolls() const { return dma_stat_polls_; }

private:
    IopMemory& mem_;
    EeRamSink* sink_;
    IopKernel* kernel_ = nullptr;
    HandlerRunner run_handler_;
    uint32_t next_dma_id_ = 1;
    uint64_t bytes_copied_ = 0;
    uint64_t dma_stat_polls_ = 0;
};

}  // namespace ps2r::iop::hle
