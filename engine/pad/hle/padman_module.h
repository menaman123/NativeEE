#pragma once
// PadmanModule: the padman IOP module's EE-facing RPC face -- the two servers
// libpad binds (0x80000100 the command server, 0x80000101 the pad-data
// server). The SIF function is always 1 and the PADMAN opcode is the first
// word of the send buffer. Two padman generations speak different opcode sets
// and the face follows the module it stands in for (SetModuleFacts: the
// .iopmod version of the padman image the title loaded):
//
// Legacy (version < 3.00; the 2.x padman): every libpad command the title
// issues is an EE-side row (LibpadBindings), so the face serves what still
// reaches it -- 0xFF init, 0x01 open (area at send+0x0C), 0x02 close, 0x03
// info-mode, 0x04 info-act, 0x05 get-state (2 STABLE), 0x06 read (the idle
// 8-byte report), 0x07 set-main-mode, 0x08 set-act-direct, 0x09
// set-act-align, 0x0A get-button-mask, 0x0B set-button-info. The result word
// is fanned out to reply +0x00 / +0x10 / +0x14.
//
// SDK 3 (version >= 3.00; padman 3.x/4.x, libpad 3.x raw): the command table
// the module's dispatcher indexes (padman.irx 4.10: handler 0x7648, table
// .rodata+0x7f50), each op with its own request and reply offsets --
//   0x01 OPEN (port +4, slot +8, area +0x10) -> result +0x0C, pad id +0x14
//   0x06 SET_MMODE (mode +0x0C, lock +0x10) -> +0x14   0x07 SET_ACTDIR -> +0x14
//   0x08 SET_ACTALIGN (6 bytes @+0x0C) -> +0x14        0x09 GET_BTNMASK -> +0x0C
//   0x0A SET_BTNINFO (info +0x0C) -> +0x10             0x0B SET_VREF -> +0x1C
//   0x0C GET_PORTMAX -> +0x0C   0x0D GET_SLOTMAX (port +4) -> +0x0C
//   0x0E CLOSE (port, slot, wait +0x10) -> +0x0C       0x0F END -> +0x0C
//   0x10 INIT (ee arg +0x10) -> +0x0C                  0x12 GET_MODVER -> +0x0C (the version)
//   0x14 / 0x18 (one word @+4, module setters) -> +0x08
// libpad checks GET_MODVER >> 8 against its own major (scePadInit fails on a
// mismatch), OPEN's result at +0x0C and the pad id at +0x14 -- the IOP address
// libpad's scePadSetActDirect DMAs the 32-byte actuator command to (two
// alternating halves, 0x40 bytes per slot): the face hands out slices of the
// actuator sink the composition names (the row image's .bss on a hosted
// padman), so the DMA lands where the console's padman keeps its pad record.
#include <cstdint>
#include <functional>

#include "engine/sif/hle/rpc_router.h"

class PadProtocol;

class PadmanModule : public RpcRouter::Server {
public:
    static constexpr uint32_t kCommandServerId = 0x80000100;
    static constexpr uint32_t kDataServerId = 0x80000101;
    // Legacy opcodes.
    static constexpr uint32_t kOpInit = 0xFF, kOpOpen = 0x01, kOpClose = 0x02, kOpInfoMode = 0x03, kOpInfoAct = 0x04,
                              kOpGetState = 0x05, kOpRead = 0x06, kOpSetMainMode = 0x07, kOpSetActDirect = 0x08,
                              kOpSetActAlign = 0x09, kOpGetButtonMask = 0x0A, kOpSetButtonInfo = 0x0B;
    static constexpr int32_t kStateStable = 2;
    static constexpr uint32_t kReplyResultOffsets[3] = {0x00, 0x10, 0x14};
    // SDK 3 opcodes.
    static constexpr uint32_t kSdk3Open = 0x01, kSdk3SetMainMode = 0x06, kSdk3SetActDirect = 0x07, kSdk3SetActAlign = 0x08,
                              kSdk3GetButtonMask = 0x09, kSdk3SetButtonInfo = 0x0A, kSdk3SetVref = 0x0B,
                              kSdk3GetPortMax = 0x0C, kSdk3GetSlotMax = 0x0D, kSdk3Close = 0x0E, kSdk3End = 0x0F,
                              kSdk3Init = 0x10, kSdk3GetModVersion = 0x12, kSdk3SetWordA = 0x14, kSdk3SetWordB = 0x18;
    static constexpr uint16_t kSdk3MinVersion = 0x0300;
    static constexpr uint32_t kSdk3PortMax = 2, kSdk3SlotMax = 1, kSdk3ActBytesPerSlot = 0x40, kSdk3SlotsPerPort = 4;

    // The padman module the face stands in for: version 0 = unknown (legacy);
    // act_sink/act_sink_size = the IOP RAM the actuator DMA may land in.
    struct ModuleFacts {
        uint16_t version = 0;
        uint32_t act_sink = 0;
        uint32_t act_sink_size = 0;
    };
    using Facts = std::function<ModuleFacts()>;   // evaluated per call (the module loads during the run)

    using ReadWord = std::function<uint32_t(uint32_t addr)>;
    using WriteByte = std::function<void(uint32_t addr, uint8_t value)>;
    using WriteWord = std::function<void(uint32_t addr, uint32_t value)>;
    using Now = std::function<uint64_t()>;

    PadmanModule(PadProtocol& protocol, ReadWord read, WriteByte write8, WriteWord write32, Now now)
        : protocol_(protocol), read_(std::move(read)), write8_(std::move(write8)), write32_(std::move(write32)),
          now_(std::move(now)) {}

    void SetModuleFacts(Facts facts) { facts_ = std::move(facts); }
    static bool IsSdk3(uint16_t version) { return version >= kSdk3MinVersion; }
    // The pad id OPEN answers under SDK 3 for (port, slot) over the sink; 1 without a sink.
    static uint32_t Sdk3PadId(const ModuleFacts& f, uint32_t port, uint32_t slot);

    void Call(const RpcRouter::CallRequest& req) override;

    uint64_t Calls() const { return calls_; }
    uint64_t InitCalls() const { return init_calls_; }
    uint64_t OpenCalls() const { return open_calls_; }
    uint64_t UnknownCalls() const { return unknown_calls_; }
    uint32_t LastOp() const { return last_op_; }
    bool LastSdk3() const { return last_sdk3_; }

private:
    void CallLegacy(const RpcRouter::CallRequest& req, uint32_t op);
    void CallSdk3(const RpcRouter::CallRequest& req, uint32_t op, const ModuleFacts& facts);
    void Reply(const RpcRouter::CallRequest& req, uint32_t offset, uint32_t value);

    PadProtocol& protocol_;
    ReadWord read_;
    WriteByte write8_;
    WriteWord write32_;
    Now now_;
    Facts facts_;
    uint64_t calls_ = 0, init_calls_ = 0, open_calls_ = 0, unknown_calls_ = 0;
    uint32_t last_op_ = 0;
    bool last_sdk3_ = false;
};
