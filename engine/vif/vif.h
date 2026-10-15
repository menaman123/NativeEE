#pragma once
// The VIF0/VIF1 command processors: both units' register files, the quadword
// FIFO + word latch fed by the DMAC (or the VIF1 FIFO window), the VIFcode
// interpreter (STCYCL ... MSCAL / MPG / DIRECT / UNPACK), the double-buffer
// rule, and the MMIO register file. Implements MmioHandler but is NOT
// registered with the router in this phase.
//
// PS2 rules: the VIF consumes its stream one 32-bit VIFcode at a time; a
// command's payload follows the code word; UNPACK / MPG payloads start on the
// next word; DIRECT / DIRECTHL payloads start on the next quadword and go to
// the GIF on PATH2; MSCAL / MSCALF / MSCNT latch TOP := TOPS, ITOPS := ITOP,
// toggle DBF and swap TOPS between BASE and BASE+OFST before starting the VU1
// program (VIF0 has no double buffer); the i-bit sets STAT.INT unless ERR.MII.
//
// Cross-thread contract: single writer (the EE / DMAC thread), or the MTVU
// worker for VIF1 while it runs (MtvuVifFeed: the worker replays every VIF1
// span through ProcessVif1 on its thread; the EE feeds nothing to VIF1 then).
#include <cstddef>
#include <cstdint>

#include "engine/ee/mem/mmio_handler.h"
#include "engine/vif/vif_regs.h"
#include "engine/vif/vif_unpack.h"
#include "engine/vu/tiers/mtvu_vif_feed.h"

class GifArbiter;
class GifRegs;
class VuUnit;
class VifFastSpan;

// Module-local settings. `fast_span` (the norm) parses a whole DMA span in
// place and falls back to the word latch only when the span cannot be parsed
// that way; false forces the word path (the feed-equivalence proof drives both).
struct VifSettings {
    bool fast_span = true;
    VifUnpackSettings unpack;
};

class Vif final : public MmioHandler, public MtvuVifFeed {
public:
    static constexpr uint32_t kVif0Base = 0x10003800u;
    static constexpr uint32_t kVif1Base = 0x10003C00u;
    static constexpr uint32_t kRegsSize = 0x180u;  // through C3 at +0x14C
    static constexpr uint32_t kVif1FifoBase = 0x10005000u;
    static constexpr uint32_t kFifoSize = 0x10u;

    Vif(VuUnit& vu0, VuUnit& vu1, GifArbiter& arbiter, GifRegs& gif_regs,
        VifSettings settings = VifSettings{});

    // Power-on: registers zero except CYCLE = CL 1 / WL 1; every FIFO, latch,
    // cursor and pending command cleared on both units.
    void Reset();
    // One unit's transient state only -- parser state, word latch, FIFO,
    // decode cursor, pending command, DIRECT buffer -- back to idle; the
    // register file is untouched (a savestate restore writes the registers
    // and then idles the unit, since states are captured with nothing pending).
    void ResetTransient(int vif);

    // The DMA feed: `size` bytes of VIF stream for unit `vif` (0/1).
    void ProcessData(int vif, const uint8_t* data, size_t size);
    // The VIF1 FIFO window (0x10005000..0x1000500C): one 32-bit word per slot,
    // the quadword is submitted on the last slot.
    void WriteFifo(int vif, uint32_t address, uint32_t word);

    // MMIO register file (both units + the FIFO window); `size` is ignored.
    // The OLD router only exposed FBRST/ERR (+ VIF1_STAT while MTVU ran) for reads, FBRST/ERR + the FIFO window for writes; everything else read 0 / was dropped (P5-VIF-MMIO-OBSERVABLE).
    uint64_t Read(uint32_t addr, unsigned size) override;
    void Write(uint32_t addr, unsigned size, uint64_t value) override;

    // MTVU seam: the unit VIF1 programs run on (the worker rebinds it), the
    // TTE-quadword bracket and the latch reset at a chain-walk head.
    VuUnit& CtxFor(int vif);
    void BindVu1(VuUnit* unit) override;
    void SetTteFeed(int vif, bool on);
    void ResetLatch(int vif);
    // MtvuVifFeed: the VIF1 forms of the three seam members.
    void ProcessVif1(const uint8_t* data, size_t size) override;
    void SetVif1TteFeed(bool on) override;
    void ResetVif1Latch() override;
    // Savestate admissibility: nothing buffered, nothing pending.
    bool IsIdle(int vif) const;

    // TOPS/ITOPS after each MSCAL/MSCALF/MSCNT swap, for the VU1 HLE runtime.
    using TopsObserver = void (*)(void* user, uint16_t tops, uint16_t itops);
    void SetTopsObserver(TopsObserver fn, void* user);

    VifRegs& regs(int vif) { return u_[vif].regs; }
    const VifRegs& regs(int vif) const { return u_[vif].regs; }
    const VifUnitState& unit(int vif) const { return u_[vif]; }
    VifUnpack& unpack() { return unpack_; }
    const VifUnpack& unpack() const { return unpack_; }
    VifSettings& settings() { return settings_; }

private:
    friend class VifFastSpan;

    void WriteToLatch2(int vif, uint32_t word);
    void ProcessFifo(int vif);
    void ExecuteCommand(int vif, uint32_t cmd, uint32_t imm, uint32_t num, const uint8_t* payload,
                        size_t payload_size, bool irq, bool use_mask);
    void UpdateStat(int vif, uint32_t vps);
    // Payload bytes a code word needs after itself (0 = none); sets `use_mask` for UNPACK.
    uint32_t BytesNeeded(int vif, uint32_t cmd, uint32_t num, bool* use_mask) const;
    void SubmitDirect(int vif);
    void CompleteDirect(int vif);
    void SwapDoubleBuffer();
    void KickVu1(uint32_t start_pc, bool resume);
    void WriteRegister(int vif, uint32_t offset, uint32_t value);
    uint32_t ReadRegister(int vif, uint32_t offset) const;

    VuUnit& vu0_;
    VuUnit& vu1_;
    VuUnit* vu1_target_;
    GifArbiter& arbiter_;
    GifRegs& gif_regs_;
    VifSettings settings_;
    VifUnpack unpack_;
    VifUnitState u_[2];
    TopsObserver tops_obs_ = nullptr;
    void* tops_user_ = nullptr;
};
