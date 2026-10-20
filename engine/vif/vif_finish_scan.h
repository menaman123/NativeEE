#pragma once
// VifFinishScan: a side-effect-free VIF1 command walker over the spans the EE
// stages to the MTVU worker, counting the GS FINISH register writes the
// worker's replay will produce on PATH2 (A+D FINISH inside DIRECT / DIRECTHL
// payloads) -- the E425 early FINISH's PATH2 half. It mirrors the two VIF feeds'
// decode of where the next VIFcode is (Vif::ProcessFifo and VifFastSpan produce
// the identical sequence): one 32-bit code word; UNPACK / MPG / STMASK / STROW /
// STCOL payloads start on the next word (under a TTE quadword an UNPACK / MPG
// payload that does not fit the quadword's remaining words starts on the next
// quadword); DIRECT / DIRECTHL payloads start on the next quadword and run IMM
// quadwords (0 = 65536), crossing span ends; STCYCL sets the CL / WL an UNPACK's
// size depends on (VifUnpack::GetUnpackSize). The GIF side of every DIRECT
// payload goes through one GifFinishScan (the parser keeps PATH2's transfer
// state across packets). Everything else (MSCAL, FLUSH, MSKPATH3, the i-bit) has
// no payload and no stall in this engine.
//
// The scan may MISS a FINISH (the parser raises it as today); it must not
// report one the parser will not write. ResyncRegs(cl, wl) re-seeds the cycle
// registers after a savestate restore (the VIFS section rewrote them).
#include <cstddef>
#include <cstdint>

#include "engine/gif/gif_finish_scan.h"

class VifFinishScan {
public:
    // One staged span (`tte` = the walker's TTE quadword). Returns the FINISH
    // writes found in the DIRECT payloads completed or continued here.
    uint32_t Feed(const uint8_t* data, size_t size, bool tte);
    // The walk head's latch reset (Vif::ResetLatch): a partial quadword is dropped.
    void ResetLatch() { qw_phase_ = 0; }
    // After a savestate restore: the unit idles (nothing pending) with these cycle registers.
    void ResyncRegs(uint32_t cl, uint32_t wl);
    // The PATH2 GIF transfer state (the composed STAT view's OPH for PATH2).
    const GifFinishScan& Path2() const { return path2_; }
    GifFinishScan& Path2() { return path2_; }

private:
    enum class Mode : uint8_t { Idle, WaitData, Direct };

    GifFinishScan path2_;
    Mode mode_ = Mode::Idle;
    uint32_t words_left_ = 0;   // WaitData: payload words still owed; Direct: payload words still owed
    uint32_t direct_finishes_ = 0;   // FINISH writes seen in the DIRECT in progress (reported at its completion)
    uint32_t cl_ = 1, wl_ = 1;
    uint32_t qw_phase_ = 0;     // word index within the current quadword of the stream (0..3)
};
