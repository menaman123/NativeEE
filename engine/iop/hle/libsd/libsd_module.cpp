// engine/iop/hle/libsd/libsd_module.cpp -- the libsd bodies over SpuCore.
#include "engine/iop/hle/libsd/libsd_module.h"

#include <cmath>

#include "engine/iop/iop_clock.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_memory.h"
#include "engine/sound/spu_core.h"

namespace ps2r::iop::hle {

LibsdModule::LibsdModule(IopMemory& mem, sound::SpuCore& core) : mem_(mem), core_(core) {}

void LibsdModule::SetCompletion(IopKernel* kernel, HandlerRunner run) {
    kernel_ = kernel;
    run_handler_ = std::move(run);
}

void LibsdModule::Call(int ordinal, IopContext& ctx) {
    if (ordinal < 0 || ordinal >= kOrdinals) {
        ctx.gpr[2] = 0;
        return;
    }
    calls_[ordinal]++;
    const uint32_t a0 = ctx.gpr[4], a1 = ctx.gpr[5], a2 = ctx.gpr[6];
    uint32_t v0 = 0;
    switch (ordinal) {
    case 0:  core_.SdInit(); break;                          // SdInit(flag) -> 0
    case 1:  core_.SetParam(a0, a1); break;                  // SdSetParam(entry, value) -> 0
    case 2:  v0 = core_.GetParam(a0); break;                 // SdGetParam(entry) -> value
    case 3:  core_.SetSwitch(a0, a1); break;                 // SdSetSwitch(entry, mask) -> 0
    case 4:  v0 = core_.GetSwitch(a0); break;                // SdGetSwitch(entry) -> mask
    case 5:  core_.SetAddr(a0, a1); break;                   // SdSetAddr(entry, byte_addr) -> 0
    case 6:  v0 = core_.GetAddr(a0); break;                  // SdGetAddr(entry) -> byte_addr (NAX)
    case 7:  core_.SetCoreAttr(a0, a1); break;               // SdSetCoreAttr(entry, value) -> 0
    case 8:  ProcBatch(a0, a1, a2); break;                   // SdProcBatch(batch, rets, num) -> 0
    case 9:  VoiceTrans(ctx); return;                        // SdVoiceTrans -> bytes moved
    case 10: core_.NoteUnmodelled(10, a0); break;            // SdBlockTrans: no autodma model -> 0
    case 11:                                                 // SdVoiceTransStatus(chan, flag) -> 1 idle
        RetireTransCompletion(static_cast<int>(a0 & 1));     // the poll delivers a pending completion
        v0 = static_cast<uint32_t>(core_.VoiceTransStatus(static_cast<int>(a0 & 1), static_cast<int>(a1)));
        break;
    case 12: break;                                          // SdBlockTransStatus -> 0 idle
    case 13: core_.NoteUnmodelled(13, a0); break;            // SdSetEffectAttr -> 0 (no reverb)
    case 14: core_.NoteUnmodelled(14, a0); break;            // SdClearEffectWorkArea -> 0
    case 15:                                                 // SdSetTransIntrHandler(chan, fn, arg) -> 0
        core_.SetTransIntrHandler(static_cast<int>(a0 & 1), a1, a2);
        break;
    case 16: {                                               // SdNote2Pitch(center_note, center_fine, note, fine) -> pitch
        // The SPU2 pitch for `note` relative to the sample's center note: 0x1000 at the
        // center, doubling every 12 semitones (a fine step is 1/128 of a semitone).
        const double center = static_cast<double>(static_cast<uint16_t>(a0)) + static_cast<double>(static_cast<uint16_t>(a1)) / 128.0;
        const double note = static_cast<double>(static_cast<uint16_t>(a2)) + static_cast<double>(static_cast<uint16_t>(ctx.gpr[7])) / 128.0;
        const double pitch = 4096.0 * std::pow(2.0, (note - center) / 12.0);
        v0 = pitch >= 0x3FFF ? 0x3FFFu : (pitch < 0 ? 0u : static_cast<uint32_t>(pitch + 0.5));
        break;
    }
    default: break;
    }
    ctx.gpr[2] = v0;
}

// The batch: `num` 8-byte records {u16 func, u16 entry, u32 value} at `batch`;
// SET family 1..4 applied at once, GET family 0x10..0x13 read into rets[i]
// (a null rets discards them); unknown funcs skipped. GETCORE (0x13) reads the
// core-attribute shadow. A null or oversized batch processes nothing.
void LibsdModule::ProcBatch(uint32_t batch, uint32_t rets, uint32_t num) {
    if (batch == 0 || num == 0 || num > kMaxBatchRecords) return;
    for (uint32_t i = 0; i < num; i++) {
        const uint32_t rec = batch + i * kBatchRecordBytes;
        const uint16_t func = mem_.Read<uint16_t>(rec + 0);
        const uint16_t entry = mem_.Read<uint16_t>(rec + 2);
        const uint32_t value = mem_.Read<uint32_t>(rec + 4);
        uint32_t rv = 0;
        switch (func) {
        case 0x1: core_.SetParam(entry, value); break;
        case 0x2: core_.SetSwitch(entry, value); break;
        case 0x3: core_.SetAddr(entry, value); break;
        case 0x4: core_.SetCoreAttr(entry, value); break;
        case 0x10: rv = core_.GetParam(entry); break;
        case 0x11: rv = core_.GetSwitch(entry); break;
        case 0x12: rv = core_.GetAddr(entry); break;
        case 0x13: rv = core_.CoreAttr(sound::SpuCore::EntryCore(entry), sound::SpuCore::CoreAttrIndex(entry)); break;
        default: break;
        }
        if (rets) mem_.Write<uint32_t>(rets + i * 4, rv);
        batch_records_++;
    }
}

// SdVoiceTrans(chan, mode, iop_addr, spu_addr, size): the fifth argument is on
// the caller's stack (+0x10). The bytes move now (clamped to IOP RAM and to SPU
// RAM by the core); the completion is scheduled as a kernel event due at once
// -- delivered at the kernel's next dispatch or retired by the module's own
// status poll, never inside this call.
void LibsdModule::VoiceTrans(IopContext& ctx) {
    const int chan = static_cast<int>(ctx.gpr[4] & 1);
    const uint32_t mode = ctx.gpr[5];
    const uint32_t iop_addr = ctx.gpr[6];
    const uint32_t spu_addr = ctx.gpr[7];
    const uint32_t size = mem_.Read<uint32_t>(ctx.gpr[29] + 0x10);
    const uint32_t masked = iop_addr & IopMemory::kMask;
    const uint32_t room = IopMemory::kSize - masked;
    uint8_t* host = mem_.Pointer(masked, size <= room ? size : room);
    uint32_t moved = 0;
    if (host) moved = core_.VoiceTrans(chan, mode, host, spu_addr, size <= room ? size : room);
    voice_trans_bytes_ += moved;
    pending_seq_[chan]++;
    ScheduleTransCompletion(chan);
    ctx.gpr[2] = moved;
}

void LibsdModule::ScheduleTransCompletion(int chan) {
    chan &= 1;
    // A second transfer on a core with a completion still pending: the hardware
    // would have interrupted before the DMA could be re-programmed -- deliver it.
    RetireTransCompletion(chan);
    const uint64_t ticket = next_ticket_++;
    pending_ticket_[chan] = ticket;
    if (!kernel_) return;
    kernel_->ScheduleEventUs(kernel_->Clock().NowUs(), [this, chan, ticket] {
        if (pending_ticket_[chan] != ticket) return;   // retired by a status poll: stale
        pending_ticket_[chan] = 0;
        FireTransIntrHandler(chan);
    });
}

bool LibsdModule::RetireTransCompletion(int chan) {
    chan &= 1;
    if (pending_ticket_[chan] == 0) return false;
    pending_ticket_[chan] = 0;
    FireTransIntrHandler(chan);
    return true;
}

// The registered handler runs with a0 = channel, a1 = the registered argument,
// on the scratch stack at the top of IOP RAM (as the kernel runs a thread entry).
void LibsdModule::FireTransIntrHandler(int chan) {
    uint32_t handler = 0, arg = 0;
    if (!core_.GetTransIntrHandler(chan, handler, arg) || handler == 0 || !run_handler_) return;
    IopContext cb;
    cb.gpr[4] = static_cast<uint32_t>(chan);
    cb.gpr[5] = arg;
    cb.gpr[29] = kScratchStack;
    handlers_fired_++;
    run_handler_(handler, cb);
}

std::string LibsdModule::CallsCensus() const {
    std::string s;
    for (int i = 0; i < kOrdinals; i++)
        if (calls_[i]) s += (s.empty() ? "" : ",") + std::to_string(i) + ":" + std::to_string(calls_[i]);
    return s;
}

void LibsdModule::ResetPendingSequence() {
    pending_seq_[0] = pending_seq_[1] = 0;
    pending_ticket_[0] = pending_ticket_[1] = 0;
}

}  // namespace ps2r::iop::hle
