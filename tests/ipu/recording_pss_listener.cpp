// tests/ipu/recording_pss_listener.cpp -- RecordingPssListener bodies.
#include "recording_pss_listener.h"

void RecordingPssListener::OnVideoEs(const uint8_t* data, size_t size, bool contains_sequence_end) {
    events.push_back({PssEvent::kVideo, std::vector<uint8_t>(data, data + size), contains_sequence_end});
    video_bytes += size;
    ++video_count;
    if (contains_sequence_end) ++sequence_end_count;
    es.insert(es.end(), data, data + size);
}

void RecordingPssListener::OnAudioPes(const uint8_t* data, size_t size) {
    events.push_back({PssEvent::kAudio, std::vector<uint8_t>(data, data + size), false});
    audio_bytes += size;
    ++audio_count;
}

void RecordingPssListener::OnProgramEnd() {
    events.push_back({PssEvent::kProgramEnd, {}, false});
    ++program_end_count;
}
