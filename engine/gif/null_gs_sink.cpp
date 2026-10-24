// NullGsSink -- every GsSink call discarded (see the header).
#include "engine/gif/null_gs_sink.h"

void NullGsSink::BeginTag(const GifTag&, GifPath) {}
void NullGsSink::WriteRegister(uint8_t, uint64_t) {}
void NullGsSink::PackedRgbaq(uint8_t, uint8_t, uint8_t, uint8_t) {}
void NullGsSink::PackedSt(float, float, float) {}
void NullGsSink::PackedUv(uint16_t, uint16_t) {}
void NullGsSink::PackedFog(uint8_t) {}
void NullGsSink::KickVertex(float, float, float, uint8_t, bool) {}
void NullGsSink::ImageData(uint64_t, uint64_t) {}
void NullGsSink::EndOfPacket() {}
void NullGsSink::FinishPath3() {}
