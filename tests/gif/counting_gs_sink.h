#pragma once
// Test-only GsSink that counts calls per method; the parser suites use it to
// assert call multiplicities without inspecting record contents.
#include <cstdint>

#include "engine/gif/gs_sink.h"

class CountingGsSink : public GsSink {
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

    uint64_t Total() const;
    void Reset();

    uint64_t begin_tag = 0;
    uint64_t write_register = 0;
    uint64_t packed_rgbaq = 0;
    uint64_t packed_st = 0;
    uint64_t packed_uv = 0;
    uint64_t packed_fog = 0;
    uint64_t kick_vertex = 0;
    uint64_t kick_draw = 0;      // KickVertex calls with draw == true
    uint64_t image_data = 0;
    uint64_t end_of_packet = 0;
    uint64_t finish_path3 = 0;
    uint8_t last_reg = 0;        // reg of the most recent WriteRegister
    uint64_t last_value = 0;     // value of the most recent WriteRegister
};
