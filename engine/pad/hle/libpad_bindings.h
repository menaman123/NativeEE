#pragma once
// LibpadBindings: the libpad SDK rows (library "libpad.a"; the addresses are
// the title's sdk_bindings.tsv) as real bodies over PadProtocol and libpad's
// own EE-side tables. libpad 3.0.1 keeps three statics the rows read and
// write -- their ADDRESSES are title data (TitleProfile::Pad), never known
// here:
//   port_table  (port*0x70 + slot*0x1c): +0x00 pad area, +0x04 static
//               buffer, +0x08 pad id, +0x0C 0, +0x10 the slot-open sentinel
//               every reader gates on
//   open_flags  (port*0x1c + slot*7): the open byte
//   static_area (port*0x80 + slot*0x20): the per-slot static buffer
//               scePadSetActDirect fills (+0x04 cmd 1, +0x08 length 6, +0x0C
//               the six actuator bytes)
// Readers pick the slot with the larger +0x58 counter (scePadGetDmaStr).
//
//   scePadPortOpen(port, slot, area)      -> tables written, the protocol's
//                                            area registered; port*2+slot+1
//   scePadPortClose(port, slot)           -> tables cleared, area cleared; 1
//   scePadGetDmaStr(port, slot, dst)      -> 128-byte slot copy when dst; slot addr
//   scePadRead(port, slot, rdata)         -> `length` (+0x60) bytes copied; length
//   scePadGetState(port, slot)            -> 0x63 unopened; 5 when state 6 and
//                                            reqState 2; else the state byte
//   scePadSetReqState(port, slot, state)  -> +0x71 of the active slot; 1
//   scePadGetReqState(port, slot)         -> +0x71
//   scePadInfoAct(port, slot, actno, term)-> the DualShock 2 actuator table
//   scePadInfoMode(port, slot, mode, idx) -> the four PAD_MODECUR* cases
//   scePadSetMainMode / SetActAlign / SetButtonInfo / EnterPressMode
//                                         -> reqState 2 (the accepted command); 1
//   scePadSetActDirect(port, slot, data)  -> 0 until currentTask 1; the static
//                                            buffer written; 1
//   scePadGetButtonMask(port, slot)       -> the +0x79..0x7C bytes (0x3FFFF)
//   scePadGetModVersion()                 -> 0x0300
//   scePadInit2 / _send_to_iop / _pad_intr_hdr -> 1 / 1 / 0 (RPC fragments of
//                                            the SDK bodies the rows replace)
// Every row gates on the open sentinel: 0 when the slot was never opened.
#include <cstddef>
#include <cstdint>
#include <functional>

#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest;
class PadProtocol;

class LibpadBindings {
public:
    // The title's libpad statics (TitleProfile::Pad supplies them).
    struct Statics {
        uint32_t port_table = 0;
        uint32_t open_flags = 0;
        uint32_t static_area = 0;
    };
    using Now = std::function<uint64_t()>;   // the current boundary (PadProtocol's clock)

    static constexpr int32_t kModVersion = 0x0300;
    static constexpr int32_t kStateDisconnected = 0x63;
    static constexpr int32_t kStateFindCtp1 = 5;
    static constexpr uint32_t kMaxPort = 1, kMaxSlot = 3;
    static constexpr uint32_t kSlotBytes = 0x80;
    static constexpr int kActuators = 2;   // = PadProtocol::kActuators

    // Registers every row above; returns the number registered.
    static int Bind(SdkBindingManifest& m, PadProtocol* protocol, Statics statics, Now now);
    static const char* const* RowNames(size_t* count);

    static void PortOpen(CpuContext& ctx);
    static void PortClose(CpuContext& ctx);
    static void GetDmaStr(CpuContext& ctx);
    static void Read(CpuContext& ctx);
    static void GetState(CpuContext& ctx);
    static void SetReqState(CpuContext& ctx);
    static void GetReqState(CpuContext& ctx);
    static void InfoAct(CpuContext& ctx);
    static void InfoMode(CpuContext& ctx);
    static void SetMainMode(CpuContext& ctx);
    static void SetActDirect(CpuContext& ctx);
    static void SetActAlign(CpuContext& ctx);
    static void GetButtonMask(CpuContext& ctx);
    static void SetButtonInfo(CpuContext& ctx);
    static void EnterPressMode(CpuContext& ctx);
    static void GetModVersion(CpuContext& ctx);
    static void Init2(CpuContext& ctx);
    static void SendToIop(CpuContext& ctx);
    static void IntrHandler(CpuContext& ctx);

    // Census (tests / run.meta).
    static uint64_t OpenCalls();
    static uint64_t ReadCalls();
    static uint64_t GetStateCalls();
    static uint64_t ReadsServed();   // scePadRead calls that served a 0x20-byte report
};
