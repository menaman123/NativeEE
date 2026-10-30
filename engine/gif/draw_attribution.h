#pragma once
// DrawAttribution: which generated function submitted the GIF packet the drain
// owner is delivering right now. Every GifArbiterPacket carries `submit_fn`,
// the shadow-stack top of the thread that submitted it (SubmitterFn(): the EE
// function that kicked the GIF DMA chain / ran the VIF1 span inline; a packet
// produced on the MTVU worker inherits the function that opened its order
// marker on the EE). The drain pass sets Current() before each delivery, so a
// GS-side recorder on the drain owner (the DrawIR / draw-list builder behind
// gs.frame) reads CurrentFn() when it creates a draw record and stores it as
// the draw's issuing function (0 = unknown).
//
// Two tables are kept as evidence and for readers that have no record of
// their own: a per-frame list of delivered packets (fn, path, bytes) in
// delivery order, swapped at every frame boundary (LastFrame()), and a
// process-wide census (packets, bytes) per submitting function (Census()).
#include <cstdint>
#include <vector>

#include "engine/gif/gif_tag.h"

class DrawAttribution {
public:
    struct Packet {
        uint32_t fn = 0;
        GifPath path = GifPath::Path3;
        uint32_t bytes = 0;
    };
    struct CensusRow {
        uint32_t fn = 0;
        uint64_t packets = 0;
        uint64_t bytes = 0;
    };
    static constexpr size_t kFrameTableMax = 65536;

    // The submitting thread's shadow-stack top (a function address), 0 when none.
    static uint32_t SubmitterFn();

    // The tables (frame list, census) are kept only while enabled (default off:
    // SetCurrent then costs two thread-local stores). CurrentFn() always works.
    static void Enable(bool on);
    static bool Enabled();

    // Drain owner: the packet about to be delivered / the pass is over (0).
    static void SetCurrent(uint32_t fn, GifPath path, uint32_t bytes);
    static void EndPass();
    static uint32_t CurrentFn();
    static GifPath CurrentPath();
    // Drain owner: the frame boundary passed; the current frame's table becomes LastFrame().
    static void FrameBoundary(uint32_t seq);

    // Readers (any thread): the last completed frame's packets and its sequence.
    static std::vector<Packet> LastFrame(uint32_t* seq = nullptr);
    static std::vector<CensusRow> Census();
    static void ResetCensus();
    static uint64_t Delivered();
    static uint64_t Unattributed();
};
