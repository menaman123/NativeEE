#pragma once
// What the GIF hands to the GS, in stream order. One call per GS state
// mutation the parser performs, so a recording sink reproduces the write
// sequence exactly and the GS implementation replays it.
#include <cstddef>
#include <cstdint>

#include "engine/gif/gif_tag.h"

class GsSink {
public:
    virtual ~GsSink() = default;

    // A GIFtag was accepted on `path`: the GS resets Q to 1.0 (PRE -> PRIM
    // follows as a separate WriteRegister).
    virtual void BeginTag(const GifTag& tag, GifPath path) = 0;

    // A 64-bit general register write (REGLIST half, A+D pair, PACKED PRIM,
    // or any PACKED descriptor outside the decoded set). Never receives the
    // four vertex registers 0x04/0x05/0x0C/0x0D: the parser decodes them into
    // KickVertex. SIGNAL/FINISH/LABEL arrive here; the CSR/INTC effect is the GS's.
    virtual void WriteRegister(uint8_t reg, uint64_t value) = 0;

    // PACKED RGBAQ: r,g,b,a only, Q is left untouched.
    virtual void PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) = 0;

    // PACKED ST: s,t always; the raw q is passed and the GS applies it only
    // when q != 0.0f, and clears its uv-since-st latch.
    virtual void PackedSt(float s, float t, float q) = 0;

    // PACKED UV: 14-bit u,v; sets the GS uv-since-st latch.
    virtual void PackedUv(uint16_t u, uint16_t v) = 0;

    // PACKED FOG: the 8-bit F field; the GS stores it as the PRIM fog-enable
    // flag (f != 0), not as a fog coefficient.
    virtual void PackedFog(uint8_t f) = 0;

    // A vertex: x,y in pixels (12.4 unsigned decoded), z as float, the fog
    // byte, and whether this kick draws (XYZ2/XYZF2 without ADC).
    virtual void KickVertex(float x, float y, float z, uint8_t fog, bool draw) = 0;

    // One IMAGE quadword: the GS (re)activates the host->local transfer if
    // idle, then consumes lo and hi as two HWREG halves.
    virtual void ImageData(uint64_t lo, uint64_t hi) = 0;

    // `qwords` consecutive IMAGE quadwords of one tag (16 bytes each, the
    // stream's byte order). The default is the per-quadword ImageData sequence
    // exactly; a sink that stores IMAGE data overrides it to take the span whole
    // (the GS transfer engine's staged store) -- same bytes, same order, one
    // call. The parser delivers IMAGE data through this method.
    virtual void ImageSpan(const uint8_t* data, size_t qwords);

    // GIFtag EOP reached (NLOOP 0 with EOP, or the last loop with EOP): the GS
    // flushes its pending primitive batch.
    virtual void EndOfPacket() = 0;

    // A drain pass delivered PATH3 data and no IMAGE transfer is mid-way: the
    // GS flushes its draw buffer if non-empty.
    virtual void FinishPath3() = 0;
};
