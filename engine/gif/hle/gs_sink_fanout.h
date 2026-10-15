#pragma once
// GsSinkFanout: a GsSink that forwards every call, in order, to each of its
// member sinks. The composition root hands the parser one sink; the fan-out lets
// the GS façade and the register-op recorder both observe the identical stream
// (the recorder's hash is compared against the façade's draws at the gate).
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "engine/gif/gs_sink.h"

class GsSinkFanout : public GsSink {
public:
    GsSinkFanout() = default;
    GsSinkFanout(std::initializer_list<GsSink*> sinks);

    void Add(GsSink* sink);
    size_t Size() const { return sinks_.size(); }

    void BeginTag(const GifTag& tag, GifPath path) override;
    void WriteRegister(uint8_t reg, uint64_t value) override;
    void PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
    void PackedSt(float s, float t, float q) override;
    void PackedUv(uint16_t u, uint16_t v) override;
    void PackedFog(uint8_t f) override;
    void KickVertex(float x, float y, float z, uint8_t fog, bool draw) override;
    void ImageData(uint64_t lo, uint64_t hi) override;
    void ImageSpan(const uint8_t* data, size_t qwords) override;
    void EndOfPacket() override;
    void FinishPath3() override;

private:
    std::vector<GsSink*> sinks_;
};
