// gs/hle/present/crtc_classifier.cpp -- the dual-circuit one-line rule.
#include "engine/gs/hle/present/crtc_classifier.h"

namespace ps2r {
namespace gs {

CrtcClassifier::Snapshot CrtcClassifier::Classify(uint64_t pmode, uint64_t d1, uint64_t d2,
                                                  uint64_t y1, uint64_t y2) {
    const bool en1 = (pmode & 1) != 0, en2 = (pmode & 2) != 0;
    Class c = Class::Other;
    if (en1 && en2 && d1 == d2) {
        // DISPLAY.DY = bits 22:12; every other field identical and |DY1 - DY2| <= 1.
        const uint64_t dy_mask = 0x7FFull << 12;
        const uint32_t dy1 = static_cast<uint32_t>((y1 >> 12) & 0x7FF);
        const uint32_t dy2 = static_cast<uint32_t>((y2 >> 12) & 0x7FF);
        const uint32_t dyd = dy1 > dy2 ? dy1 - dy2 : dy2 - dy1;
        if ((y1 & ~dy_mask) == (y2 & ~dy_mask) && dyd <= 1) c = Class::DualIdentical1px;
    }
    Snapshot s;
    s.cls = c;
    s.fbp_bytes = static_cast<uint32_t>(d1 & 0x1FF) * 8192u;
    s.fbw_px = static_cast<uint32_t>((d1 >> 9) & 0x3F) * 64u;
    s.psm = static_cast<uint32_t>((d1 >> 15) & 0x1F);
    s.valid = true;
    return s;
}

uint32_t CrtcClassifier::Pack(const Snapshot& s) {
    return (s.valid ? 1u : 0u) | ((static_cast<uint32_t>(s.cls) & 3u) << 1) |
           (((s.fbp_bytes / 8192u) & 0x1FFu) << 3) | (((s.fbw_px / 64u) & 0x3Fu) << 12) |
           ((s.psm & 0x1Fu) << 18);
}

CrtcClassifier::Snapshot CrtcClassifier::Unpack(uint32_t p) {
    Snapshot s;
    s.valid = (p & 1u) != 0;
    s.cls = static_cast<Class>((p >> 1) & 3u);
    s.fbp_bytes = ((p >> 3) & 0x1FFu) * 8192u;
    s.fbw_px = ((p >> 12) & 0x3Fu) * 64u;
    s.psm = (p >> 18) & 0x1Fu;
    return s;
}

bool CrtcClassifier::IsNativeConfig(const Snapshot& s, const VramLayout& layout) {
    return s.valid && s.cls == Class::DualIdentical1px &&
           layout.IsNativeScanout(s.fbp_bytes, s.fbw_px, s.psm);
}

}  // namespace gs
}  // namespace ps2r
