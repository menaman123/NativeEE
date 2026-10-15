// engine/sound/hle/sd_block_trans.cpp -- the virtual SPU2 block transfer.
#include "engine/sound/hle/sd_block_trans.h"

namespace ps2r::sound::hle {

SdBlockTrans::SdBlockTrans(int block_ms, FrameClock clock)
    : block_frames_(static_cast<uint32_t>((block_ms < 1 ? 1 : block_ms) * kFramesPerMs)), clock_(std::move(clock)) {}

// WRITE / READ / WRITE_FROM start a block now; STOP tears the channel down and
// answers 0 (the stopped address of an idle channel). `size` is the EE ring the
// FMV player streams from; the virtual block does not consume it.
int32_t SdBlockTrans::Arm(uint32_t chan, uint32_t mode, uint32_t /*size*/) {
    Channel& x = ch_[chan & 1];
    if ((mode & kDirMask) == kStop) {
        x.active = false;
        return 0;
    }
    x.active = true;
    x.start_frames = clock_ ? clock_() : 0;
    return 0;
}

// The address ramps linearly across the block span with elapsed mixer frames;
// at the block's duration the channel completes (address 0) and the double
// buffer flips so the next armed block alternates halves like the retail ring.
uint32_t SdBlockTrans::Status(uint32_t chan) {
    Channel& x = ch_[chan & 1];
    if (!x.active) return ComposeStatus(x.buf_index, 0);
    const uint64_t now = clock_ ? clock_() : 0;
    const uint64_t elapsed = now >= x.start_frames ? now - x.start_frames : 0;
    if (elapsed >= block_frames_) {
        x.active = false;
        x.buf_index ^= 1;
        return ComposeStatus(x.buf_index, 0);
    }
    uint32_t addr = static_cast<uint32_t>((static_cast<uint64_t>(kBlockSpan) * elapsed) / block_frames_);
    if (addr == 0) addr = 1;   // non-zero while active (0 means complete)
    return ComposeStatus(x.buf_index, addr);
}

void SdBlockTrans::Export(int chan, bool& active, uint32_t& buf_index, uint64_t& elapsed_frames) const {
    const Channel& x = ch_[chan & 1];
    active = x.active;
    buf_index = x.buf_index;
    const uint64_t now = clock_ ? clock_() : 0;
    elapsed_frames = (x.active && now >= x.start_frames) ? now - x.start_frames : 0;
}

void SdBlockTrans::Import(int chan, bool active, uint32_t buf_index, uint64_t elapsed_frames) {
    Channel& x = ch_[chan & 1];
    x.active = active;
    x.buf_index = buf_index;
    const uint64_t now = clock_ ? clock_() : 0;
    x.start_frames = now >= elapsed_frames ? now - elapsed_frames : 0;
}

}  // namespace ps2r::sound::hle
