#include "engine/ee/intc/timer_codec.h"

#include <cstring>

uint16_t TimerCodec::ApplyModeWrite(uint16_t old_mode, uint32_t value) {
    const uint16_t clear = static_cast<uint16_t>(value) & kModeFlags;
    uint16_t mode = static_cast<uint16_t>(value & ~static_cast<uint32_t>(kModeFlags));
    mode |= static_cast<uint16_t>(old_mode & kModeFlags & static_cast<uint16_t>(~clear));
    return mode;
}

namespace {
template <typename T>
void Put(std::vector<uint8_t>& out, const T& v) {
    uint8_t b[sizeof(T)];
    std::memcpy(b, &v, sizeof(T));
    out.insert(out.end(), b, b + sizeof(T));
}
template <typename T>
void Get(const uint8_t*& p, T& v) {
    std::memcpy(&v, p, sizeof(T));
    p += sizeof(T);
}
}  // namespace

void TimerCodec::Encode(const Regs t[4], std::vector<uint8_t>& out) {
    for (int i = 0; i < 4; ++i) {
        Put(out, t[i].mode);
        Put(out, t[i].comp);
        Put(out, t[i].hold);
        Put(out, t[i].count_base);
        Put(out, t[i].baseline_tick);
        Put(out, t[i].pending_ovf);
        Put(out, t[i].pending_cmp);
    }
}

bool TimerCodec::Decode(const uint8_t* p, size_t len, Regs t[4]) {
    if (len != kTimrPayloadLen) return false;
    for (int i = 0; i < 4; ++i) {
        Get(p, t[i].mode);
        Get(p, t[i].comp);
        Get(p, t[i].hold);
        Get(p, t[i].count_base);
        Get(p, t[i].baseline_tick);
        Get(p, t[i].pending_ovf);
        Get(p, t[i].pending_cmp);
        if (t[i].count_base > 0xFFFFu) return false;  // period invariant
    }
    return true;
}
