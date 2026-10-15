// engine/ipu/hle/pss_demux_listener.h -- PssDemuxListener: what the program-stream
// demuxer hands out. Payload pointers are valid only for the duration of the call.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2r::ipu {

class PssDemuxListener {
public:
    virtual ~PssDemuxListener() = default;

    // One video PES packet's elementary-stream payload (stream ids 0xE0..0xEF).
    // `contains_sequence_end` is true when the payload holds an MPEG-2
    // sequence_end_code (00 00 01 B7): the movie's video ends with it.
    virtual void OnVideoEs(const uint8_t* data, size_t size, bool contains_sequence_end) = 0;

    // One audio PES packet's payload (private_stream_1 0xBD or 0xC0..0xDF).
    virtual void OnAudioPes(const uint8_t* data, size_t size) = 0;

    // The MPEG program end code (00 00 01 B9): nothing after it is input.
    virtual void OnProgramEnd() = 0;
};

}  // namespace ps2r::ipu
