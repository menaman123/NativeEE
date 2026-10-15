// GifPacketBuilder -- PACKED GIF packet emission (layouts in the header).
#include "engine/vu/hle/gif_packet_builder.h"

#include <cstring>

void GifPacketBuilder::BeginTag(int prim, const std::vector<uint8_t>& regs, bool eop) {
    tag_off_ = buf_.size();
    buf_.resize(buf_.size() + 16, 0);
    uint64_t lo = 0;
    if (eop) lo |= 1ull << 15;
    if (prim >= 0) lo |= (1ull << 46) | ((uint64_t)(prim & 0x7FF) << 47);
    lo |= (uint64_t)(regs.size() & 0xF) << 60;  // 16 -> 0
    uint64_t hi = 0;
    for (size_t i = 0; i < regs.size() && i < 16; i++) hi |= (uint64_t)(regs[i] & 0xF) << (i * 4);
    std::memcpy(&buf_[tag_off_], &lo, 8);
    std::memcpy(&buf_[tag_off_ + 8], &hi, 8);
    nreg_ = regs.empty() ? 16 : (uint32_t)regs.size();
    qws_ = 0;
}

void GifPacketBuilder::EndTag() {
    uint64_t lo;
    std::memcpy(&lo, &buf_[tag_off_], 8);
    lo = (lo & ~0x7FFFull) | ((qws_ / nreg_) & 0x7FFF);
    std::memcpy(&buf_[tag_off_], &lo, 8);
}

void GifPacketBuilder::QW(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    const size_t o = buf_.size();
    buf_.resize(o + 16);
    std::memcpy(&buf_[o], &a, 4);
    std::memcpy(&buf_[o + 4], &b, 4);
    std::memcpy(&buf_[o + 8], &c, 4);
    std::memcpy(&buf_[o + 12], &d, 4);
    qws_++;
}

void GifPacketBuilder::PRIM(uint32_t prim) { QW(prim & 0x7FF, 0, 0, 0); }

void GifPacketBuilder::RGBAQ(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { QW(r, g, b, a); }

void GifPacketBuilder::ST(float s, float t, float q) {
    uint32_t a, b, c;
    std::memcpy(&a, &s, 4);
    std::memcpy(&b, &t, 4);
    std::memcpy(&c, &q, 4);
    QW(a, b, c, 0);
}

void GifPacketBuilder::UV(uint32_t u12_4, uint32_t v12_4) { QW(u12_4 & 0x3FFF, v12_4 & 0x3FFF, 0, 0); }

void GifPacketBuilder::XYZ2(uint32_t x12_4, uint32_t y12_4, uint32_t z, bool adc) {
    QW(x12_4 & 0xFFFF, y12_4 & 0xFFFF, z, adc ? (1u << 15) : 0);
}

void GifPacketBuilder::XYZF2(uint32_t x12_4, uint32_t y12_4, uint32_t z24, uint8_t fog, bool adc) {
    QW(x12_4 & 0xFFFF, y12_4 & 0xFFFF, (z24 & 0xFFFFFF) << 4, ((uint32_t)fog << 4) | (adc ? (1u << 15) : 0));
}

void GifPacketBuilder::AD(uint8_t addr, uint64_t value) {
    const size_t o = buf_.size();
    buf_.resize(o + 16, 0);
    std::memcpy(&buf_[o], &value, 8);
    buf_[o + 8] = addr;
    qws_++;
}

std::vector<uint8_t> GifPacketBuilder::Take() {
    std::vector<uint8_t> r;
    r.swap(buf_);
    return r;
}
