#pragma once
// Dmac: the EE DMA controller -- the ten channel register files, the global
// registers (D_CTRL, D_STAT, D_PCR, D_SQWC, D_RBSR, D_RBOR, D_STADR, D_ENABLER,
// D_ENABLEW), the MMIO map over 0x10008000-0x1000E060 + 0x1000F520/F590, and
// channel start / completion / interrupt dispatch. Implements MmioHandler but
// is NOT registered with the router in this phase.
//
// PS2 rules: writing CHCR with STR set starts the channel; the transfer runs
// to completion (instantly here), clears STR, sets D_STAT.CIS(ch) and zeroes
// QWC; INT1 is asserted while CIS & CIM is non-zero or BEIS is set; the kernel's DMAC
// handlers run for each pending, enabled channel. The VIF1 chain kick is the
// one deferred transfer (DmacDeferredVif1). D_STAT provides status W1C and mask
// toggles; D_PCR is writable. D_CTRL / D_SQWC / D_RBSR / D_RBOR / D_STADR /
// D_ENABLEW writes remain dropped.
// Unsupported SPR requests retain STR without signalling completion; faults
// terminate with CIS and, for bus errors, BEIS, retaining outstanding QWC.
//
// IPU FROM retains STR and outstanding QWC on a short drain; the owner retries
// StartChannel when output becomes available. Its MADR advances by bytes drained.
// Channel bodies outside this module are injected: SIF1 (the SIF command
// packet), IPU FROM/TO (the IPU FIFOs); null = the channel completes with no
// data movement. Scratchpad channels use DmacSprTransfer.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"
#include "engine/dmac/chain_walker.h"
#include "engine/dmac/dmac_channel.h"
#include "engine/dmac/dmac_deferred_vif1.h"
#include "engine/dmac/dmac_handler_registry.h"
#include "engine/dmac/dmac_spr_transfer.h"
#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/mem/mmio_handler.h"

class MainMemory;
class Scratchpad;
class Vif;
class GifArbiter;

// The global register file, in savestate order.
struct DmacGlobalRegs {
    uint32_t ctrl = 0;
    uint32_t stat = 0;  // low half CIS, high half CIM
    uint32_t pcr = 0;
    uint32_t sqwc = 0;
    uint32_t rbsr = 0;
    uint32_t rbor = 0;
    uint32_t stadr = 0;
    uint32_t enabler = 0;
    uint32_t enablew = 0;
};

class Dmac : public MmioHandler {
public:
    using GuestCall = std::function<bool(uint32_t pc, CpuContext& ctx)>;
    using SifCallback = std::function<void(int ch, const DmacChannelRegs& regs)>;
    using IpuCallback = std::function<void(uint8_t* host, size_t bytes)>;
    // Returns bytes transferred, a multiple of 16 no greater than requested.
    using IpuDrainCallback = std::function<size_t(uint8_t* host, size_t bytes)>;
    using InterruptRaise = std::function<void()>;

    static constexpr size_t kSectionFixedBytes = 324;   // 10 x 28 + 9 x 4 + i32 next id + u32 count
    static constexpr size_t kHandlerRecordBytes = 25;   // i32 cause, i32 id, u32 pc, u32 gp, u32 arg, i32 flag, u8 active
    static constexpr uint32_t kMaxHandlers = 1024;

    Dmac(MainMemory& ram, Scratchpad& spad, Vif& vif, GifArbiter& arbiter);

    // MMIO: sub-word reads widen from the 32-bit register at addr & ~3; a
    // sub-word store writes the raw value as the whole register; 8 bytes = two halves.
    uint64_t Read(uint32_t addr, unsigned size) override;
    void Write(uint32_t addr, unsigned size, uint64_t value) override;

    // Kick channel `ch` from its registers (CHCR.STR already written).
    void StartChannel(int ch);
    // STR cleared, CIS(ch) set, QWC zeroed; raises INT1 through the slot when CIS & CIM becomes non-zero.
    void CompleteChannel(int ch);
    bool CheckInterrupt() const;
    // D_STAT.CIM(ch) as the kernel's _EnableDmac / _DisableDmac set it; raises INT1 when the
    // newly enabled channel already has its status bit pending.
    void SetChannelInterruptMask(int ch, bool enable);
    // Run the registered handlers of every pending, enabled channel in channel
    // order ($a0 = channel, $gp = handler gp; a -1 return stops that channel's
    // chain), then acknowledge each channel's CIS bit.
    void DispatchInterrupt(CpuContext& ctx);

    // The deferred VIF1 kick's observation points outside the register file
    // (HLE syncs, the frame boundary) and the savestate quiesce precondition.
    void FlushDeferredVif1();
    void ChaseVif1Tail();
    bool HasDeferredVif1() const { return deferred_.Active(); }
    // The kicking thread blocks in a kernel wait (WaitSema / WaitEventFlag /
    // SleepThread ...): the CPU stops appending tags and the DMA it kicked runs
    // to completion while it sleeps, so a parked VIF1 chain is walked (its
    // PATH2 before any PATH3) and the arbiter's queued PATH3 reaches the GS. A
    // title that observes completion through the GS FINISH / DMAC interrupt
    // handlers instead of a register read has no other observation point before
    // its handler's semaphore is waited on. No effect when nothing is pending.
    void ObserveAtKernelWait();

    void SetGuestCall(GuestCall fn);
    void SetSifCallback(SifCallback fn);
    void SetIpuFeedInput(IpuCallback fn);
    void SetIpuDrainOutput(IpuDrainCallback fn);
    // Optional: the IPU input FIFO's free bytes. Bound, a toIPU transfer moves only what
    // fits and the channel keeps STR/QWC/MADR for a later StartChannel (the hardware's
    // eight-quadword FIFO: libmpeg reads D4_QWC and IPU_CTRL.IFC to locate the stream).
    using IpuRoomCallback = std::function<size_t()>;
    void SetIpuInputRoom(IpuRoomCallback fn);
    void SetInterruptRaise(InterruptRaise fn);

    DmacChannelRegs& channel(int ch) { return ch_[ch]; }
    const DmacChannelRegs& channel(int ch) const { return ch_[ch]; }
    DmacGlobalRegs& globals() { return g_; }
    const DmacGlobalRegs& globals() const { return g_; }
    DmacHandlerRegistry& handlers() { return handlers_; }
    const DmacHandlerRegistry& handlers() const { return handlers_; }
    ChainWalker& walker() { return walker_; }
    DmacDeferredVif1& deferred() { return deferred_; }
    // Lifetime census; never serialized or reset by savestate restore.
    const DmacTransferStats& TransferStats() const { return spr_.TransferStats(); }

    // The TAG_DMAC section (frozen layout: channels, globals, next handler id,
    // count, records cause-then-id). Exposed for registration at integration.
    ps2r::savestate::SectionSpec SavestateSection();
    void SerializeSection(std::vector<uint8_t>& out) const;
    static bool ValidateSection(const std::vector<uint8_t>& payload, std::string* err);
    bool RestoreSection(const std::vector<uint8_t>& payload, std::string* err);

    // Channel of a register address for reads (page ranges) / writes (256-byte pages); -1 = none.
    static int ChannelForRead(uint32_t addr, uint32_t* offset);
    static int ChannelForWrite(uint32_t addr);

private:
    uint32_t Read32(uint32_t addr);
    void Write32(uint32_t addr, uint32_t value);
    void RunVifChain(int ch);
    static void RunVif1Thunk(void* user);

    ChainWalker walker_;
    DmacDeferredVif1 deferred_;
    DmacHandlerRegistry handlers_;
    GifArbiter& arbiter_;
    DmacChannelRegs ch_[kDmacChannelCount];
    DmacGlobalRegs g_;
    GuestCall guest_call_;
    SifCallback sif_;
    IpuCallback ipu_feed_input_;
    IpuDrainCallback ipu_drain_output_;
    IpuRoomCallback ipu_input_room_;
    InterruptRaise raise_;
    DmacSprTransfer spr_;
};
