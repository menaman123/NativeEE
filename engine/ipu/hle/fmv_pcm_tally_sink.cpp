#include "engine/ipu/hle/fmv_pcm_tally_sink.h"

namespace ps2r::ipu {

void FmvPcmTallySink::Push(const int16_t* stereo, uint32_t frames) {
    (void)stereo;
    ++pushes_;
    frames_ += frames;
}

void FmvPcmTallySink::GateOpen() {
    ++gate_opens_;
    open_ = true;
}

void FmvPcmTallySink::Flush() {
    ++flushes_;
    open_ = false;
}

}  // namespace ps2r::ipu
