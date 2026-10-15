// tests/ipu/recording_pss_listener.h -- RecordingPssListener: a PssDemuxListener
// that records every callback (kind, byte count, payload bytes, flag) in order.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/ipu/hle/pss_demux_listener.h"

struct PssEvent {
    enum Kind { kVideo, kAudio, kProgramEnd };
    Kind kind;
    std::vector<uint8_t> bytes;
    bool sequence_end;
};

class RecordingPssListener : public ps2r::ipu::PssDemuxListener {
public:
    std::vector<PssEvent> events;
    size_t video_bytes = 0;
    size_t audio_bytes = 0;
    unsigned video_count = 0;
    unsigned audio_count = 0;
    unsigned program_end_count = 0;
    unsigned sequence_end_count = 0;
    // Concatenation of every video payload (the elementary stream).
    std::vector<uint8_t> es;

    void OnVideoEs(const uint8_t* data, size_t size, bool contains_sequence_end) override;
    void OnAudioPes(const uint8_t* data, size_t size) override;
    void OnProgramEnd() override;
};
