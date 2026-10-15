// gs/hle/present/crtc_classifier.h -- CrtcClassifier: what the two CRTC circuits
// are showing (GS User's Manual 3.5 PMODE / DISPFB / DISPLAY).
//
// The class decides whether a frame may be presented straight from a GPU render
// target: when both circuits are enabled, read the SAME buffer (DISPFB1 ==
// DISPFB2) and differ only in DISPLAY.DY by at most one line -- the interlaced
// flicker-filter shape -- the merge is a blur of one source and promoting it to a
// single progressive present is exact; everything else is the mirror-decode path.
// The native-config predicate additionally requires the title's scanout tuple.
#pragma once

#include <cstdint>

#include "engine/gs/hle/targets/vram_layout.h"

namespace ps2r {
namespace gs {

class CrtcClassifier {
public:
    enum class Class : uint32_t { Unclassified = 0, DualIdentical1px = 1, Other = 2 };

    struct Snapshot {
        Class cls = Class::Unclassified;
        uint32_t fbp_bytes = 0;   // DISPFB1.FBP * 8192
        uint32_t fbw_px = 0;      // DISPFB1.FBW * 64
        uint32_t psm = 0;         // DISPFB1.PSM
        bool valid = false;
    };

    static Snapshot Classify(uint64_t pmode, uint64_t dispfb1, uint64_t dispfb2, uint64_t display1,
                             uint64_t display2);
    // Packed u32 form (bit0 valid, bits 2:1 class, 11:3 FBP pages, 17:12 FBW units, 22:18 PSM).
    static uint32_t Pack(const Snapshot& s);
    static Snapshot Unpack(uint32_t packed);
    static bool IsNativeConfig(const Snapshot& s, const VramLayout& layout);
};

}  // namespace gs
}  // namespace ps2r
