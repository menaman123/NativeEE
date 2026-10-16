#pragma once
#include "engine/dmac/dmac_channel.h"
#include "engine/dmac/dmac_transfer_stats.h"

class MainMemory;
class Scratchpad;

enum class DmacSprResult { Complete, Unsupported, BusError, InvalidTag };

// RAM <-> scratchpad normal transfers, RAM -> scratchpad source chains (toSPR
// MOD=1: REFE/CNT/NEXT/REF/END tags in RAM) and scratchpad -> RAM destination
// chains (fromSPR MOD=1: CNTS/CNT/END tags read from the scratchpad stream
// itself, each followed by its QWC quadwords; ADDR is the RAM destination --
// the way a renderer copies the display list it built in the scratchpad).
// Unsupported requests stay busy for diagnosis; bus errors terminate in Dmac.
// EE User's Manual v6.0 sections 5.1, 5.2.2-3, 5.4, 5.6, 5.9-10.
class DmacSprTransfer {
public:
    static constexpr uint32_t kMaxTags = 10000;

    DmacSprTransfer(MainMemory& ram, Scratchpad& spad);
    void ObserveCtrlWrite(uint32_t ctrl);
    DmacSprResult Run(bool to_spr, DmacChannelRegs& regs, uint32_t ctrl);
    const DmacTransferStats& TransferStats() const { return stats_; }

private:
    enum class RejectReason {
        Mode, ZeroQwc, MfifoOrStall, VuMemory, ScratchpadSource, PriorityControl,
        WalkLimit, BusAddress, InvalidTag
    };
    DmacSprResult Reject(DmacSprResult result, RejectReason reason);
    void LogRejection(bool to_spr, const DmacChannelRegs& requested, const DmacChannelRegs& stopped, uint32_t ctrl);
    DmacSprResult Copy(bool to_spr, DmacChannelRegs& regs, DmacTransferCount& count);
    DmacSprResult SourceChain(DmacChannelRegs& regs, DmacTransferCount& count);
    DmacSprResult DestinationChain(DmacChannelRegs& regs, DmacTransferCount& count);

    MainMemory& ram_;
    Scratchpad& spad_;
    DmacTransferStats stats_;
    RejectReason reason_ = RejectReason::Mode;
    uint16_t reported_[8]{};  // One bit per reason, indexed by direction/MOD.
    bool reported_ctrl_ = false;
};
