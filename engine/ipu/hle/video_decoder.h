// engine/ipu/hle/video_decoder.h -- VideoDecoder: the elementary-stream decoder
// behind the sceMpeg HLE. It takes MPEG-2 video ES bytes as the demuxer produces
// them and appends whole decoded pictures, in decode order, to a caller-owned
// FIFO. The FFmpeg-backed implementation is the only production one; tests may
// substitute a scripted decoder.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

#include "engine/ipu/hle/decoded_picture.h"

namespace ps2r::ipu {

class VideoDecoder {
public:
    virtual ~VideoDecoder() = default;

    // Feeds `size` ES bytes; every picture completed by them is pushed to `out`.
    virtual void Feed(const uint8_t* data, size_t size, std::deque<DecodedPicture>& out) = 0;

    // End of stream: drains the pictures still buffered inside the decoder.
    virtual void Flush(std::deque<DecodedPicture>& out) = 0;

    // The frame rate written in the sequence header, known once the first
    // picture has been decoded; 0.0 before that or when the header has none.
    virtual double AuthoredFps() const = 0;

    // Releases the codec state; the object may be reused after a new Feed.
    virtual void Shutdown() = 0;
};

}  // namespace ps2r::ipu
