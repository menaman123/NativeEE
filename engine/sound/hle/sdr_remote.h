#pragma once
// SdrRemote: the SDRDRV sound-driver face -- the libsdr command set
// (sdrcmd.h's rSd* codes) answered from the ONE SpuCore the IOP-side libsd
// bodies also drive, so the EE's core-volume programming and the IOP's voice
// traffic meet in one model. Two entrances:
//   * the EE row sceSdRemote(mode, cmd, a2, a3, t0, t1) -- libsdr's SIF RPC to
//     the IOP replaced by the direct call (LibsdrBindings). Dispatch() forwards
//     ONLY 0x8010 rSdSetParam (the core volumes this title programs from the
//     EE), 0x80E0 rSdBlockTrans and 0x8100 rSdBlockTransStatus (the FMV audio
//     pacing); every other code -- including 0x8000 rSdInit, which must NOT
//     silence the voices the IOP module started -- answers 0, the reference
//     scope (the status queries read 0 = "no progress");
//   * the SIF RPC server 0x80000701 (RpcRouter::Server) when a title binds the
//     SDRDRV server itself: func = the libsd function number of the IOP
//     driver's table (0 SdInit, 1 SetParam, 2 GetParam, 3 SetSwitch,
//     4 GetSwitch, 5 SetAddr, 6 GetAddr, 7 SetCoreAttr, 8 GetCoreAttr,
//     9 Note2Pitch, 0xA Pitch2Note, 0xB/0xC ProcBatch*), args = the send
//     buffer's words, the result = the reply's first word.
// Invoke() is the full model behind the SIF face: 0x8000 rSdInit, 0x8010 rSdSetParam(entry=a2, value=a3),
// 0x8020 rSdGetParam, 0x8030/0x8040 Set/GetSwitch, 0x8050/0x8060 Set/GetAddr,
// 0x8070 rSdSetCoreAttr, 0x8080 rSdGetCoreAttr, 0x80E0 rSdBlockTrans
// (chan=a2, mode=a3, addr=t0, size=t1), 0x8100 rSdBlockTransStatus(chan=a2);
// every other code (voice transfers, effects, handlers) answers 0.
#include <cstdint>
#include <functional>

#include "engine/sif/hle/rpc_router.h"

namespace ps2r::sound {
class SpuCore;
}

namespace ps2r::sound::hle {

class SdBlockTrans;

class SdrRemote : public RpcRouter::Server {
public:
    static constexpr uint32_t kServerId = 0x80000701;
    enum Cmd : uint32_t {
        kInit = 0x8000, kSetParam = 0x8010, kGetParam = 0x8020, kSetSwitch = 0x8030, kGetSwitch = 0x8040,
        kSetAddr = 0x8050, kGetAddr = 0x8060, kSetCoreAttr = 0x8070, kGetCoreAttr = 0x8080,
        kVoiceTrans = 0x80D0, kBlockTrans = 0x80E0, kVoiceTransStatus = 0x80F0, kBlockTransStatus = 0x8100,
        kSetEffectAttr = 0x8130, kGetEffectAttr = 0x8140, kSetTransIntrHandler = 0x8160,
        kSetSpu2IntrHandler = 0x8170, kProcBatch2 = 0x81C0, kProcBatchEx2 = 0x81D0
    };
    static constexpr uint32_t kNote2PitchBase = 0x1000;   // 44.1 kHz base pitch
    static constexpr uint32_t kPitch2NoteMiddleC = 60;
    using ReadWord = std::function<uint32_t(uint32_t addr)>;
    using WriteWord = std::function<void(uint32_t addr, uint32_t value)>;

    SdrRemote(SpuCore& core, SdBlockTrans& block, ReadWord read, WriteWord write);

    // The EE sceSdRemote row: cmd with its argument registers (a2, a3, t0, t1);
    // the reference scope (0x8010 / 0x80E0 / 0x8100 forwarded, else 0).
    uint32_t Dispatch(uint32_t cmd, uint32_t a2, uint32_t a3, uint32_t t0, uint32_t t1);
    // The full rSd* command model (the SIF face's function table).
    uint32_t Invoke(uint32_t cmd, uint32_t a2, uint32_t a3, uint32_t t0, uint32_t t1);
    // The SIF RPC face.
    void Call(const RpcRouter::CallRequest& req) override;

    uint64_t Calls() const { return calls_; }
    uint64_t Commands(uint32_t cmd_index) const { return cmd_calls_[cmd_index & 0x1F]; }
    static uint32_t CmdIndex(uint32_t cmd) { return (cmd >> 4) & 0x1F; }

private:
    SpuCore& core_;
    SdBlockTrans& block_;
    ReadWord read_;
    WriteWord write_;
    uint64_t calls_ = 0;
    uint64_t cmd_calls_[32] = {};
};

}  // namespace ps2r::sound::hle
