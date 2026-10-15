#include "tools/vu1_replay/xgk_capture_sink.h"

void XgkCaptureSink::Attach(GifArbiter& arbiter) { arbiter.SetSubmitObserver(&XgkCaptureSink::OnSubmit, this); }

void XgkCaptureSink::OnSubmit(void* user, GifPath path, const uint8_t* data, size_t size) {
    if (path != GifPath::Path1) return;
    auto* self = static_cast<XgkCaptureSink*>(user);
    self->kicks_.emplace_back(data, data + size);
}

void XgkCaptureSink::BeginTag(const GifTag& tag, GifPath path) { ops_.BeginTag(tag, path); }
void XgkCaptureSink::WriteRegister(uint8_t reg, uint64_t value) { ops_.WriteRegister(reg, value); }
void XgkCaptureSink::PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { ops_.PackedRgbaq(r, g, b, a); }
void XgkCaptureSink::PackedSt(float s, float t, float q) { ops_.PackedSt(s, t, q); }
void XgkCaptureSink::PackedUv(uint16_t u, uint16_t v) { ops_.PackedUv(u, v); }
void XgkCaptureSink::PackedFog(uint8_t f) { ops_.PackedFog(f); }
void XgkCaptureSink::KickVertex(float x, float y, float z, uint8_t fog, bool draw) {
    ops_.KickVertex(x, y, z, fog, draw);
}
void XgkCaptureSink::ImageData(uint64_t lo, uint64_t hi) { ops_.ImageData(lo, hi); }
void XgkCaptureSink::EndOfPacket() { ops_.EndOfPacket(); }
void XgkCaptureSink::FinishPath3() { ops_.FinishPath3(); }

size_t XgkCaptureSink::kick_bytes() const {
    size_t n = 0;
    for (const auto& k : kicks_) n += k.size();
    return n;
}

void XgkCaptureSink::Clear() {
    kicks_.clear();
    ops_.Clear();
}
