#pragma once
// An order marker in the arbiter inbox: the EE opens it at the exact stream
// position the inline path would have submitted a VIF1 span's packets, the
// ONE worker fills it while replaying that span, then seals it. The drain
// promotes only sealed markers, so the GS-visible order equals the submission
// order for every interleaving of the two threads.
//
// Cross-thread contract: `filled` is written lock-free by the single worker
// until `sealed`; readers touch it only after observing `sealed` under the
// arbiter's inbox mutex. `sealed` / `detached` are written under that mutex.
#include <cstdint>
#include <functional>
#include <vector>

#include "engine/gif/gif_tag.h"

// One submitted packet. `boundary_seq` 0 marks a normal packet; nonzero is a
// frame-boundary command slot with empty data (`bnd_enq_ns` is the enqueue
// timestamp handed to the boundary hook, opaque here). `is_directhl` /
// `is_path3_image` are recorded and unused: DIRECTHL pre-empting a PATH3 IMAGE
// is not modelled.
struct GifArbiterPacket {
    GifPath path = GifPath::Path3;
    bool is_directhl = false;
    bool is_path3_image = false;
    std::vector<uint8_t> data;
    // Optional already-lowered producer output in the same ordered slot.
    // true: deliver the owned semantic payload; false: observe fallback only,
    // without mutating the GS (the drain then sends `data` to the raw parser).
    std::function<void(bool)> direct;
    // A packet-free library job in the same output order. It has no GIF
    // payload, path effect or frame-boundary meaning.
    std::function<void()> ordered;
    uint32_t boundary_seq = 0;
    uint64_t bnd_enq_ns = 0;
};

inline uint64_t GifArbPayloadBytes(const GifArbiterPacket& p) { return p.data.size(); }

struct GifMtvuMarker {
    std::vector<GifArbiterPacket> filled;  // worker-produced packets, in production order
    bool sealed = false;                   // the span completed; every packet is in
    bool detached = false;                 // clear() dropped its inbox slot while the worker still owned it
};
