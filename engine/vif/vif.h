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
// program (VIF0 has no double buffer); the i-bit sets STAT.INT | VIS unless ERR.MII
// and stalls the unit until FBRST.STC (SetIrqHooks: the composition's stall).
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

    // The i-bit interrupt stall (EE User's Manual, VIFcode i bit / VIFn_STAT VIS, INT /
    // VIFn_FBRST STC): a code word with the i bit set and ERR.MII clear raises INT and
    // STALLS the unit after the command completes; the unit resumes when the EE writes
    // FBRST.STC. The stall is the composition's: `on_irq` runs on the feeding thread right
    // after the command (the INTC raise, and the park under MTVU); `on_stc` runs on the
    // writing thread after STC cleared VIS / INT (the release). Null = no stall (the
    // register bits alone).
    using IrqHook = void (*)(void* user, int vif);
    void SetIrqHooks(IrqHook on_irq, IrqHook on_stc, void* user);

    // The native-executor surface (a title's chain executor, ChainWalker::
    // SetVifChainExecutor, replays the codes it does not unpack itself through
    // the engine's own command executor, and raises the i bit of a code it did):
    // one decoded VIFcode with its payload (`payload_size` bytes; an UNPACK's
    // payload carries one lookahead word after the data), exactly as the feed
    // paths call it. The caller has set regs(vif).code and, for an UNPACK,
    // SetUnpackPhase. MSCAL/MSCALF/MSCNT run the double-buffer swap + the VU kick.
    void ExecuteCommand(int vif, uint32_t cmd, uint32_t imm, uint32_t num, const uint8_t* payload,
                        size_t payload_size, bool irq, bool use_mask);
    // The command carrying the i bit completed: INT (+ VIS) unless ERR.MII, then the hook.
    void RaiseIBit(int vif);
    // Parity of an UNPACK payload's first word inside its quadword (the V2-32 / V3-32 W rule).
    void SetUnpackPhase(int vif, uint8_t phase) { u_[vif].unpack_phase = phase; }

    // The native SPAN executor (a title's): consulted first in ProcessData, on the
    // feeding thread (the EE / DMAC thread inline, the MTVU worker under the staged
    // feed), with the unit in whatever state the previous span left it. True = the
    // span was consumed and the unit left exactly as the engine's parse would have
    // left it; false = untouched, the engine's parse runs. Null = none.
    using SpanExecutor = bool (*)(void* user, int vif, const uint8_t* data, size_t size);
    void SetSpanExecutor(SpanExecutor fn, void* user);
    bool HasSpanExecutor() const { return span_exec_ != nullptr; }
    // The span parser's state-carrying steps, shared by the whole-span parser and a
    // native span executor so a payload or DIRECT stream crossing a span end is
    // parked and resumed by ONE implementation. `data`/`nwords` is the span in
    // 32-bit words, `s` the word the payload starts at.
    //   SubmitDirectSpan: a DIRECT / DIRECTHL whose payload (`bytes` = QWC x 16) is
    //     inside the span: PATH2 submit, idle, the i bit.
    //   ParkDirect: a DIRECT whose payload crosses the span end: DirectStream with
    //     the span's words from `s` buffered.
    //   ContinueDirect: a span arriving while a DIRECT streams: words consumed;
    //     `*streaming` true = the whole span went to the stream and it is still open.
    //   ParkPayload: a command whose payload crosses the span end: WaitData, the
    //     partial quadword held, the span's remaining words latched.
    //   CompletePending: the caller executed the parked command from the held words
    //     + the next span (unit(vif) exposes current_qw / word_index / fifo / the
    //     pending_* fields): the unit goes idle with nothing held.
    void SubmitDirectSpan(int vif, const uint8_t* payload, size_t bytes, bool hl, bool irq);
    void ParkDirect(int vif, uint32_t qwc, bool hl, bool irq, const uint8_t* data, size_t nwords, size_t s);
    size_t ContinueDirect(int vif, const uint8_t* data, size_t nwords, bool* streaming);
    void ParkPayload(int vif, uint32_t cmd, uint32_t imm, uint32_t num, uint32_t bytes_needed, bool irq, bool use_mask,
                     const uint8_t* data, size_t nwords, size_t s);
    void CompletePending(int vif);
    //   LatchWords: a span that does not complete the parked payload either: every word
    //     to the latch (the FIFO accumulates whole quadwords; nothing executes).
    void LatchWords(int vif, const uint8_t* data, size_t nwords);

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
    IrqHook on_irq_ = nullptr;
    IrqHook on_stc_ = nullptr;
    void* irq_user_ = nullptr;
    SpanExecutor span_exec_ = nullptr;
    void* span_exec_user_ = nullptr;
};
