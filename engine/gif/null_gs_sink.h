#pragma once
// NullGsSink: a GsSink that discards the stream. For a GIF parser whose
// consumer is not the GS -- a VU tier's verifier judging a program by the bytes
// it kicked (taken at the arbiter's submit tap), a tool that only needs the
// arbiter's contract honoured. Every call is a no-op.
#include <cstddef>
#include <cstdint>

#include "engine/gif/gs_sink.h"

class NullGsSink : public GsSink {
public:
    void BeginTag(const GifTag& tag, GifPath path) override;
    void WriteRegister(uint8_t reg, uint64_t value) override;
    void PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
    void PackedSt(float s, float t, float q) override;
    void PackedUv(uint16_t u, uint16_t v) override;
    void PackedFog(uint8_t f) override;
    void KickVertex(float x, float y, float z, uint8_t fog, bool draw) override;
    void ImageData(uint64_t lo, uint64_t hi) override;
    void EndOfPacket() override;
    void FinishPath3() override;
};
