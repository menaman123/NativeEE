#pragma once
// SdBlockTrans: the libsdr block-transfer (SPU2 autodma) command model the EE
// rows rSdBlockTrans (0x80E0) / rSdBlockTransStatus (0x8100) answer from. The
// title's FMV player arms a block ({chan 0, mode WRITE_FROM|LOOP, EE ring,
// size}) and polls the status word until the transfer address returns to 0,
// then flips to the other half of its ring. No PCM is consumed here (the FMV
// feed is Phase 7): the block is VIRTUAL -- it "transfers" a fixed span over
// a fixed duration of MIXER time (Config audio.sdr_block_ms; 8 ms default,
// 48 frames per ms), so the reported address advances monotonically and the
// block completes deterministically without a wall clock. The status word is
// block.c's: top byte = the active double-buffer index, low 24 bits = the
// current transfer address (0 = idle/complete; never 0 while a block runs).
//   Arm(chan, mode, size)   -> 0 (STOP tears the block down: 0)
//   Status(chan)            -> the status word
// State per channel {active, buf_index, elapsed frames} is the SDRX section.
#include <cstdint>
#include <functional>

namespace ps2r::sound::hle {

class SdBlockTrans {
public:
    static constexpr int kChannels = 2;
    static constexpr uint32_t kDirMask = 0x3;
    static constexpr uint32_t kStop = 0x2;             // SD_TRANS_STOP
    static constexpr uint32_t kBlockSpan = 0x4000;     // the address span one block walks
    static constexpr uint32_t kFramesPerMs = 48;       // 48 kHz mixer frames

    struct Channel {
        bool active = false;
        uint32_t buf_index = 0;
        uint64_t start_frames = 0;   // mixer frame count at Arm
    };
    // The mixer's cumulative frame count (the domain's audio clock).
    using FrameClock = std::function<uint64_t()>;

    SdBlockTrans(int block_ms, FrameClock clock);

    int32_t Arm(uint32_t chan, uint32_t mode, uint32_t size);
    uint32_t Status(uint32_t chan);

    static uint32_t ComposeStatus(uint32_t buf_index, uint32_t addr) { return ((buf_index & 0xFF) << 24) | (addr & 0xFFFFFF); }
    uint32_t BlockFrames() const { return block_frames_; }
    const Channel& ChannelState(int chan) const { return ch_[chan & 1]; }
    // Savestate: {active, buf_index, elapsed frames} per channel.
    void Export(int chan, bool& active, uint32_t& buf_index, uint64_t& elapsed_frames) const;
    void Import(int chan, bool active, uint32_t buf_index, uint64_t elapsed_frames);

private:
    uint32_t block_frames_;
    FrameClock clock_;
    Channel ch_[kChannels];
};

}  // namespace ps2r::sound::hle
