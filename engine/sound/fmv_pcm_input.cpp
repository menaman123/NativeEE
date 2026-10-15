#include "engine/sound/fmv_pcm_input.h"

#include <algorithm>
#include <cstring>

namespace ps2r::sound {

FmvPcmInput::FmvPcmInput(uint32_t capacity_frames)
    : buf_(new int16_t[static_cast<size_t>(capacity_frames) * 2]), capacity_(capacity_frames), mask_(capacity_frames - 1) {}

void FmvPcmInput::Push(const int16_t* stereo, uint32_t frames) {
    if (!stereo || frames == 0) return;
    const uint64_t space = capacity_ - (tail_ - head_);
    uint32_t n = frames;
    if (n > space) {   // drop-newest: the frames that do not fit are counted and lost
        dropped_ += n - static_cast<uint32_t>(space);
        n = static_cast<uint32_t>(space);
    }
    uint32_t done = 0;
    while (done < n) {
        const uint32_t idx = static_cast<uint32_t>((tail_ + done) & mask_);
        const uint32_t span = std::min<uint32_t>(n - done, capacity_ - idx);
        std::memcpy(buf_.get() + static_cast<size_t>(idx) * 2, stereo + static_cast<size_t>(done) * 2,
                    static_cast<size_t>(span) * 2 * sizeof(int16_t));
        done += span;
    }
    tail_ += n;
    pushed_ += n;
    for (uint32_t i = 0; i < n; i++)
        if (stereo[static_cast<size_t>(i) * 2] != 0 || stereo[static_cast<size_t>(i) * 2 + 1] != 0) ++pushed_nonzero_;
}

void FmvPcmInput::GateOpen() {
    ++gate_opens_;
    gate_ = true;
}

void FmvPcmInput::Flush() {
    ++flushes_;
    gate_ = false;
    head_ = tail_;   // everything queued is discarded
}

int FmvPcmInput::Pop(int16_t* out, int want) {
    if (!gate_ || want <= 0 || tail_ <= head_) return 0;
    const int m = static_cast<int>(std::min<uint64_t>(static_cast<uint64_t>(want), tail_ - head_));
    int done = 0;
    while (done < m) {
        const uint32_t idx = static_cast<uint32_t>((head_ + static_cast<uint64_t>(done)) & mask_);
        const int span = std::min<int>(m - done, static_cast<int>(capacity_ - idx));
        std::memcpy(out + static_cast<size_t>(done) * 2, buf_.get() + static_cast<size_t>(idx) * 2,
                    static_cast<size_t>(span) * 2 * sizeof(int16_t));
        done += span;
    }
    head_ += static_cast<uint64_t>(m);
    consumed_ += static_cast<uint64_t>(m);
    return m;
}

}  // namespace ps2r::sound
