// gs/state/gs_priv_regs.h -- GsPrivRegs: the GS privileged registers at
// 0x12000000.. (GS User's Manual chapter 6 / ps2tek "GS privileged registers"):
// PMODE, SMODE1/2, SRFSH, SYNCH1/2, SYNCV, DISPFB1/2, DISPLAY1/2, EXTBUF,
// EXTDATA, EXTWRITE, BGCOLOR, CSR, IMR, BUSDIR, SIGLBLID.
//
// Writes store the 64-bit value; the display registers additionally emit the
// scanout jobs the presenter consumes (SetFrontBuffer for DISPFB, SetWindow for
// DISPLAY, SetClearColor for BGCOLOR), each only when the register value (or the
// derived window triple) changed. Every job is stamped with the LIVE VRAM upload
// epoch at push time. CSR: bit 9 RESET zeroes the register file and leaves
// CSR = 0x1402 (FIFO empty, FINISH, bit 2) -- the DISPLAY window latches are not
// register state and survive it; bits 0-4 are write-1-to-clear (SIGNAL FINISH
// HSINT VSINT EDWINT); bit 8 FLUSH is a no-op. A CSR read returns the stored
// value verbatim. SIGNAL / FINISH / LABEL (general registers 0x60-0x62) route
// here to set the CSR event bits and SIGLBLID and count the GS interrupt.
#pragma once

#include <cstdint>
#include <functional>

#include "engine/gs/hle/ir/draw_ir_sink.h"

namespace ps2r {
namespace gs {

class GsPrivRegs {
public:
    enum Addr : uint32_t {
        kPmode = 0x0000, kSmode1 = 0x0010, kSmode2 = 0x0020, kSrfsh = 0x0030, kSynch1 = 0x0040,
        kSynch2 = 0x0050, kSyncv = 0x0060, kDispfb1 = 0x0070, kDisplay1 = 0x0080, kDispfb2 = 0x0090,
        kDisplay2 = 0x00A0, kExtbuf = 0x00B0, kExtdata = 0x00C0, kExtwrite = 0x00D0, kBgcolor = 0x00E0,
        kCsr = 0x1000, kImr = 0x1010, kBusdir = 0x1040, kSiglblid = 0x1080,
    };
    static constexpr uint64_t kCsrAfterReset = 0x1402;
    static constexpr uint32_t kCsrSignal = 1u << 0;
    static constexpr uint32_t kCsrFinish = 1u << 1;
    static constexpr uint32_t kCsrVsint = 1u << 3;
    static constexpr uint64_t kCsrFlush = 1ull << 8;   // FLUSH: drain the GS FIFO (a no-op here)
    static constexpr uint64_t kCsrReset = 1ull << 9;   // RESET: the register file to power-on

    struct Window { uint32_t w = 0, h = 0, vck = 0; bool valid = false; };

    GsPrivRegs();

    void SetSink(DrawIrSink* sink) { sink_ = sink; }
    // `vram_epoch` is read at every push (the live upload epoch of the mirror).
    void SetJobContext(uint32_t* seq_counter, const uint32_t* src_frame, std::function<uint64_t()> vram_epoch);

    void Write(uint32_t addr, uint64_t value);
    void WriteLower(uint32_t addr, uint32_t value);  // 32-bit half writes merge
    void WriteUpper(uint32_t addr, uint32_t value);
    uint64_t Read(uint32_t addr) const;
    void Reset();  // the register file and CSR; the DISPLAY window latches stay
    // A savestate restore: CSR is status the GS holds (FIFO/FINISH/VSINT bits),
    // not a value a write can store, so the image's CSR lands here verbatim.
    void RestoreCsr(uint64_t csr) { csr_ = csr; }
    uint64_t Writes() const { return writes_; }  // register writes since construction (Write and both halves)

    // General-register events (0x60 SIGNAL, 0x61 FINISH, 0x62 LABEL).
    void Signal(uint64_t value);
    void Finish();
    void Label(uint64_t value);
    void SetVsint() { csr_ |= kCsrVsint; }

    bool Interlaced() const { return interlaced_; }
    uint32_t TakeInterrupts();  // GS interrupt requests since the last call
    // Observer of each GS interrupt request as it is counted (SIGNAL / FINISH):
    // called with the CSR event bit (kCsrSignal / kCsrFinish) on the thread that
    // wrote the general register -- the composition root's hook to raise the INTC
    // GS cause in-line, gated by its IMR view. TakeInterrupts still counts them.
    void SetInterruptObserver(std::function<void(uint32_t csr_event)> observer) { interrupt_observer_ = std::move(observer); }

private:
    uint64_t& Slot(uint32_t addr);
    void Dispfb(uint32_t addr, uint64_t v);
    void Display(uint32_t addr, uint64_t v);
    void PushJob(JobKind kind, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4);

    uint64_t regs_[19] = {};
    uint64_t csr_ = kCsrAfterReset;
    bool interlaced_ = false;
    Window window_[2];
    uint32_t interrupts_ = 0;
    uint64_t writes_ = 0;
    DrawIrSink* sink_ = nullptr;
    uint32_t* seq_ = nullptr;
    const uint32_t* src_frame_ = nullptr;
    std::function<uint64_t()> vram_epoch_;
    std::function<void(uint32_t)> interrupt_observer_;
    uint64_t none_ = 0;
};

}  // namespace gs
}  // namespace ps2r
