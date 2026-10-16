// engine/iop/hle/libsd/libsd_module.h -- LibsdModule: the 16 libsd entries
// STREAM.IRX imports (libsd_stub_0..15 in the generated TU) as real bodies over
// the SPU2 core (sound::SpuCore). The stub index -> libsd export:
//   0 SdInit            8 SdProcBatch         (the batch: 8-byte records)
//   1 SdSetParam        9 SdVoiceTrans        (IOP RAM <-> SPU RAM, then the
//   2 SdGetParam                               registered handler fires as a
//   3 SdSetSwitch                              deferred kernel event)
//   4 SdGetSwitch      10 SdBlockTrans        (no autodma model: 0)
//   5 SdSetAddr        11 SdVoiceTransStatus  (retires a pending completion)
//   6 SdGetAddr        12 SdBlockTransStatus  (idle: 0)
//   7 SdSetCoreAttr    13 SdSetEffectAttr / 14 SdClearEffectWorkArea (no reverb: 0)
//                      15 SdSetTransIntrHandler(chan, fn, arg)
//                      16 SdNote2Pitch(center_note, center_fine, note, fine) (export 13; pure arithmetic)
// A voice transfer's data moves inline (the module reads its buffers back at
// once); only the COMPLETION is deferred: the SPU DMA interrupt arrives after
// SdVoiceTrans has returned and the module has latched its in-flight state --
// firing the handler inside the call loses the completion edge and wedges the
// transfer engine. A status poll (SdVoiceTransStatus) retires a pending
// completion inline, as the hardware IRQ would have arrived by the time the
// module polls. The batch (SdProcBatch) applies SET records 1..4 and stores the
// GET records 0x10..0x13 into rets[i]: the stream resume path restores the
// paused voices' pitch exclusively through it.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "engine/iop/iop_context.h"

namespace ps2r::iop {
class IopMemory;
class IopKernel;
}
namespace ps2r::sound {
class SpuCore;
}

namespace ps2r::iop::hle {

class LibsdModule {
public:
    static constexpr int kOrdinals = 17;   // 0..15 the first title's set + 16 SdNote2Pitch (export 13)
    static constexpr int kCores = 2;
    static constexpr uint32_t kMaxBatchRecords = 512;
    static constexpr uint32_t kBatchRecordBytes = 8;
    static constexpr uint32_t kScratchStack = (2u << 20) - 0x200u;   // the handler's stack: 0x200 below the IOP RAM top

    // Runs a guest function (the registered handler) with the context prepared.
    using HandlerRunner = std::function<void(uint32_t addr, IopContext& ctx)>;

    LibsdModule(IopMemory& mem, sound::SpuCore& core);

    // The completion path: the kernel that carries the deferred event and the
    // runner that invokes the handler. Without both, a transfer fires no handler.
    void SetCompletion(IopKernel* kernel, HandlerRunner run);

    // Dispatch by libsd stub index; out of range -> $v0 = 0.
    void Call(int ordinal, IopContext& ctx);

    // Savestate restore: pending completions are transient (the boot path re-parks).
    void ResetPendingSequence();
    uint32_t PendingSequence(int core) const { return pending_seq_[core & 1]; }
    bool CompletionPending(int core) const { return pending_ticket_[core & 1] != 0; }
    uint32_t Calls(int ordinal) const { return calls_[ordinal & 15]; }
    // "<ord>:<n>,..." for the ordinals called (run.meta libsd_calls=).
    std::string CallsCensus() const;
    uint64_t VoiceTransBytes() const { return voice_trans_bytes_; }
    uint32_t HandlersFired() const { return handlers_fired_; }
    uint32_t BatchRecords() const { return batch_records_; }

private:
    void ProcBatch(uint32_t batch, uint32_t rets, uint32_t num);
    void VoiceTrans(IopContext& ctx);
    void ScheduleTransCompletion(int chan);
    bool RetireTransCompletion(int chan);
    void FireTransIntrHandler(int chan);

    IopMemory& mem_;
    sound::SpuCore& core_;
    IopKernel* kernel_ = nullptr;
    HandlerRunner run_handler_;
    uint64_t pending_ticket_[kCores] = {0, 0};
    uint64_t next_ticket_ = 1;
    uint32_t pending_seq_[kCores] = {0, 0};
    uint32_t calls_[kOrdinals] = {};
    uint64_t voice_trans_bytes_ = 0;
    uint32_t handlers_fired_ = 0;
    uint32_t batch_records_ = 0;
};

}  // namespace ps2r::iop::hle
