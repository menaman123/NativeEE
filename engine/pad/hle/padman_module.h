#pragma once
// PadmanModule: the padman IOP module's EE-facing RPC face -- the two servers
// libpad 3.0.1 binds (0x80000100 the command server, 0x80000101 the pad-data
// server). Every libpad command the title issues is an EE-side row
// (LibpadBindings), so the face serves what still reaches it through
// sceSifCallRpc: the SIF function is always 1 and the PADMAN opcode is the
// first word of the send buffer --
//   0xFF init, 0x01 open (registers the pad area with PadProtocol), 0x02
//   close, 0x03 info-mode, 0x04 info-act, 0x05 get-state (2 STABLE), 0x06
//   read (the idle 8-byte report), 0x07 set-main-mode, 0x08 set-act-direct,
//   0x09 set-act-align, 0x0A get-button-mask, 0x0B set-button-info.
// The result word is fanned out to reply +0x00 / +0x10 / +0x14: libpad's
// callers read it from different offsets per command.
#include <cstdint>
#include <functional>

#include "engine/sif/hle/rpc_router.h"

class PadProtocol;

class PadmanModule : public RpcRouter::Server {
public:
    static constexpr uint32_t kCommandServerId = 0x80000100;
    static constexpr uint32_t kDataServerId = 0x80000101;
    static constexpr uint32_t kOpInit = 0xFF, kOpOpen = 0x01, kOpClose = 0x02, kOpInfoMode = 0x03, kOpInfoAct = 0x04,
                              kOpGetState = 0x05, kOpRead = 0x06, kOpSetMainMode = 0x07, kOpSetActDirect = 0x08,
                              kOpSetActAlign = 0x09, kOpGetButtonMask = 0x0A, kOpSetButtonInfo = 0x0B;
    static constexpr int32_t kStateStable = 2;
    static constexpr uint32_t kReplyResultOffsets[3] = {0x00, 0x10, 0x14};

    using ReadWord = std::function<uint32_t(uint32_t addr)>;
    using WriteByte = std::function<void(uint32_t addr, uint8_t value)>;
    using WriteWord = std::function<void(uint32_t addr, uint32_t value)>;
    using Now = std::function<uint64_t()>;

    PadmanModule(PadProtocol& protocol, ReadWord read, WriteByte write8, WriteWord write32, Now now)
        : protocol_(protocol), read_(std::move(read)), write8_(std::move(write8)), write32_(std::move(write32)),
          now_(std::move(now)) {}

    void Call(const RpcRouter::CallRequest& req) override;

    uint64_t Calls() const { return calls_; }
    uint64_t InitCalls() const { return init_calls_; }
    uint64_t OpenCalls() const { return open_calls_; }
    uint64_t UnknownCalls() const { return unknown_calls_; }
    uint32_t LastOp() const { return last_op_; }

private:
    PadProtocol& protocol_;
    ReadWord read_;
    WriteByte write8_;
    WriteWord write32_;
    Now now_;
    uint64_t calls_ = 0, init_calls_ = 0, open_calls_ = 0, unknown_calls_ = 0;
    uint32_t last_op_ = 0;
};
