#include "engine/gif/hle/gs_sink_fanout.h"

GsSinkFanout::GsSinkFanout(std::initializer_list<GsSink*> sinks) {
    for (GsSink* s : sinks) Add(s);
}

void GsSinkFanout::Add(GsSink* sink) {
    if (sink) sinks_.push_back(sink);
}

void GsSinkFanout::BeginTag(const GifTag& tag, GifPath path) {
    for (GsSink* s : sinks_) s->BeginTag(tag, path);
}

void GsSinkFanout::WriteRegister(uint8_t reg, uint64_t value) {
    for (GsSink* s : sinks_) s->WriteRegister(reg, value);
}

void GsSinkFanout::PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    for (GsSink* s : sinks_) s->PackedRgbaq(r, g, b, a);
}

void GsSinkFanout::PackedSt(float s, float t, float q) {
    for (GsSink* k : sinks_) k->PackedSt(s, t, q);
}

void GsSinkFanout::PackedUv(uint16_t u, uint16_t v) {
    for (GsSink* s : sinks_) s->PackedUv(u, v);
}

void GsSinkFanout::PackedFog(uint8_t f) {
    for (GsSink* s : sinks_) s->PackedFog(f);
}

void GsSinkFanout::KickVertex(float x, float y, float z, uint8_t fog, bool draw) {
    for (GsSink* s : sinks_) s->KickVertex(x, y, z, fog, draw);
}

void GsSinkFanout::ImageData(uint64_t lo, uint64_t hi) {
    for (GsSink* s : sinks_) s->ImageData(lo, hi);
}

void GsSinkFanout::ImageSpan(const uint8_t* data, size_t qwords) {
    for (GsSink* s : sinks_) s->ImageSpan(data, qwords);
}

void GsSinkFanout::EndOfPacket() {
    for (GsSink* s : sinks_) s->EndOfPacket();
}

void GsSinkFanout::FinishPath3() {
    for (GsSink* s : sinks_) s->FinishPath3();
}
