// engine/sound/hle/sdr_remote.cpp -- the SDRDRV command set over SpuCore.
#include "engine/sound/hle/sdr_remote.h"

#include "engine/sound/hle/sd_block_trans.h"
#include "engine/sound/spu_core.h"

namespace ps2r::sound::hle {

SdrRemote::SdrRemote(SpuCore& core, SdBlockTrans& block, ReadWord read, WriteWord write)
    : core_(core), block_(block), read_(std::move(read)), write_(std::move(write)) {}

// The EE row keeps the reference scope: the three commands the title's call
// sites depend on are forwarded; the rest answer 0 (a status query reads
// "no progress", a fire-and-forget call reads success). In particular rSdInit
// from the EE does not reach the core: the IOP module owns SdInit.
uint32_t SdrRemote::Dispatch(uint32_t cmd, uint32_t a2, uint32_t a3, uint32_t t0, uint32_t t1) {
    switch (cmd) {
    case kSetParam:
    case kBlockTrans:
    case kBlockTransStatus:
        return Invoke(cmd, a2, a3, t0, t1);
    default:
        calls_++;
        cmd_calls_[CmdIndex(cmd)]++;
        return 0;
    }
}

uint32_t SdrRemote::Invoke(uint32_t cmd, uint32_t a2, uint32_t a3, uint32_t t0, uint32_t t1) {
    (void)t0;
    calls_++;
    cmd_calls_[CmdIndex(cmd)]++;
    switch (cmd) {
    case kInit: core_.SdInit(); return 0;
    case kSetParam: core_.SetParam(a2, a3); return 0;
    case kGetParam: return core_.GetParam(a2);
    case kSetSwitch: core_.SetSwitch(a2, a3); return 0;
    case kGetSwitch: return core_.GetSwitch(a2);
    case kSetAddr: core_.SetAddr(a2, a3); return 0;
    case kGetAddr: return core_.GetAddr(a2);
    case kSetCoreAttr: core_.SetCoreAttr(a2, a3); return 0;
    case kGetCoreAttr: return core_.CoreAttr(SpuCore::EntryCore(a2), SpuCore::CoreAttrIndex(a2));
    case kBlockTrans: return static_cast<uint32_t>(block_.Arm(a2, a3, t1));
    case kBlockTransStatus: return block_.Status(a2);
    default: return 0;   // voice transfers, effects, handlers: no remote model
    }
}

// The IOP driver's function table over SIF: the send buffer's words are the
// arguments; the result is the reply's first word.
void SdrRemote::Call(const RpcRouter::CallRequest& req) {
    const uint32_t w0 = req.send_size >= 4 && read_ ? read_(req.send) : 0;
    const uint32_t w1 = req.send_size >= 8 && read_ ? read_(req.send + 4) : 0;
    uint32_t result = 0;
    switch (req.func) {
    case 0x00: result = Invoke(kInit, 0, 0, 0, 0); break;
    case 0x01: result = Invoke(kSetParam, w0, w1, 0, 0); break;
    case 0x02: result = Invoke(kGetParam, w0, 0, 0, 0); break;
    case 0x03: result = Invoke(kSetSwitch, w0, w1, 0, 0); break;
    case 0x04: result = Invoke(kGetSwitch, w0, 0, 0, 0); break;
    case 0x05: result = Invoke(kSetAddr, w0, w1, 0, 0); break;
    case 0x06: result = Invoke(kGetAddr, w0, 0, 0, 0); break;
    case 0x07: result = Invoke(kSetCoreAttr, w0, w1, 0, 0); break;
    case 0x08: result = Invoke(kGetCoreAttr, w0, 0, 0, 0); break;
    case 0x09: calls_++; result = kNote2PitchBase; break;      // SdNote2Pitch
    case 0x0A: calls_++; result = kPitch2NoteMiddleC; break;   // SdPitch2Note
    default: calls_++; result = 0; break;                      // ProcBatch*/transfers: 0
    }
    if (req.recv && req.recv_size >= 4 && write_) write_(req.recv, result);
}

}  // namespace ps2r::sound::hle
