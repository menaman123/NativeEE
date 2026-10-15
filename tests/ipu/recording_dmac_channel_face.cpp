// tests/ipu/recording_dmac_channel_face.cpp -- RecordingDmacChannelFace bodies.
#include "recording_dmac_channel_face.h"

#include <cstdio>

namespace ps2r::ipu {

void RecordingDmacChannelFace::Note(char kind, uint32_t channel, const char* reg, uint32_t value) {
    char buf[64];
    if (kind == 'R') {
        std::snprintf(buf, sizeof(buf), "R%u.%s", channel, reg);
    } else {
        std::snprintf(buf, sizeof(buf), "W%u.%s=0x%08x", channel, reg, value);
    }
    log_.emplace_back(buf);
}

uint32_t RecordingDmacChannelFace::ReadChcr(uint32_t channel) {
    Note('R', channel, "CHCR", 0);
    return Channel(channel).chcr;
}
void RecordingDmacChannelFace::WriteChcr(uint32_t channel, uint32_t value) {
    Note('W', channel, "CHCR", value);
    Channel(channel).chcr = value;
}
uint32_t RecordingDmacChannelFace::ReadMadr(uint32_t channel) {
    Note('R', channel, "MADR", 0);
    return Channel(channel).madr;
}
void RecordingDmacChannelFace::WriteMadr(uint32_t channel, uint32_t value) {
    Note('W', channel, "MADR", value);
    Channel(channel).madr = value;
}
uint32_t RecordingDmacChannelFace::ReadTadr(uint32_t channel) {
    Note('R', channel, "TADR", 0);
    return Channel(channel).tadr;
}
void RecordingDmacChannelFace::WriteTadr(uint32_t channel, uint32_t value) {
    Note('W', channel, "TADR", value);
    Channel(channel).tadr = value;
}
uint32_t RecordingDmacChannelFace::ReadQwc(uint32_t channel) {
    Note('R', channel, "QWC", 0);
    return Channel(channel).qwc;
}
void RecordingDmacChannelFace::WriteQwc(uint32_t channel, uint32_t value) {
    Note('W', channel, "QWC", value);
    Channel(channel).qwc = value;
}

}  // namespace ps2r::ipu
