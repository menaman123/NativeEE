#include "counting_gs_sink.h"

void CountingGsSink::BeginTag(const GifTag& /*tag*/, GifPath /*path*/) { ++begin_tag; }

void CountingGsSink::WriteRegister(uint8_t reg, uint64_t value) {
    ++write_register;
    last_reg = reg;
    last_value = value;
}

void CountingGsSink::PackedRgbaq(uint8_t /*r*/, uint8_t /*g*/, uint8_t /*b*/, uint8_t /*a*/) { ++packed_rgbaq; }

void CountingGsSink::PackedSt(float /*s*/, float /*t*/, float /*q*/) { ++packed_st; }

void CountingGsSink::PackedUv(uint16_t /*u*/, uint16_t /*v*/) { ++packed_uv; }

void CountingGsSink::PackedFog(uint8_t /*f*/) { ++packed_fog; }

void CountingGsSink::KickVertex(float /*x*/, float /*y*/, float /*z*/, uint8_t /*fog*/, bool draw) {
    ++kick_vertex;
    if (draw) ++kick_draw;
}

void CountingGsSink::ImageData(uint64_t /*lo*/, uint64_t /*hi*/) { ++image_data; }

void CountingGsSink::EndOfPacket() { ++end_of_packet; }

void CountingGsSink::FinishPath3() { ++finish_path3; }

uint64_t CountingGsSink::Total() const {
    return begin_tag + write_register + packed_rgbaq + packed_st + packed_uv + packed_fog + kick_vertex +
           image_data + end_of_packet + finish_path3;
}

void CountingGsSink::Reset() { *this = CountingGsSink{}; }
