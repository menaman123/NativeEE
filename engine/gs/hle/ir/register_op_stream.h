#pragma once
// The recording GsSink: every call becomes one 24-byte RegisterOp, appended in
// stream order, with a running FNV-1a 64 over the record bytes. The hash is the
// GS-side observable of a GIF stream independent of any renderer.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/gif/gs_sink.h"

enum class RegisterOpKind : uint8_t {
    Tag = 0,
    Reg = 1,
    Rgbaq = 2,
    St = 3,
    Uv = 4,
    Fog = 5,
    Kick = 6,
    Image = 7,
    Eop = 8,
    Finish = 9,
};

// One recorded sink call. `path` is the GifPath of the packet the call belongs
// to (0 before the first BeginTag); `pad` is always 0 so the byte image hashes
// deterministically. Field packing per kind:
//   Tag    reg 0     aux nloop | eop<<15 | pre<<16 | flg<<17 | nregs<<19   lo prim   hi regs nibbles
//   Reg    reg r     aux 0     lo value                                     hi 0
//   Rgbaq  reg 0x01  aux 0     lo r | g<<8 | b<<16 | a<<24                  hi 0
//   St     reg 0x02  aux 0     lo bits(s) | bits(t)<<32                     hi bits(q)
//   Uv     reg 0x03  aux 0     lo u | v<<16                                 hi 0
//   Fog    reg 0x0A  aux 0     lo f                                         hi 0
//   Kick   reg 0     aux 0     lo bits(x) | bits(y)<<32                     hi bits(z) | fog<<32 | draw<<40
//   Image  reg 0x54  aux 0     lo lo                                        hi hi
//   Eop / Finish: all zero
struct RegisterOp {
    uint8_t kind;
    uint8_t reg;
    uint8_t path;
    uint8_t pad;
    uint32_t aux;
    uint64_t lo;
    uint64_t hi;
};

// Retention: the hash is folded record by record as each call arrives, so the
// observable never needs the records themselves. A stream constructed with
// `retain_records = false` keeps only the hash and the count -- its footprint is
// constant over a leg of any length (a retaining stream grows by 24 bytes per
// sink call: every IMAGE quadword of every host->local upload, every vertex).
// Replay consumers that read `Ops()` (the DrawIR verifier, the unit pins)
// construct a retaining stream.
class RegisterOpStream : public GsSink {
public:
    explicit RegisterOpStream(bool retain_records = true);

    void BeginTag(const GifTag& tag, GifPath path) override;
    void WriteRegister(uint8_t reg, uint64_t value) override;
    void PackedRgbaq(uint8_t r, uint8_t g, uint8_t b, uint8_t a) override;
    void PackedSt(float s, float t, float q) override;
    void PackedUv(uint16_t u, uint16_t v) override;
    void PackedFog(uint8_t f) override;
    void KickVertex(float x, float y, float z, uint8_t fog, bool draw) override;
    void ImageData(uint64_t lo, uint64_t hi) override;
    void EndOfPacket() override;
    void FinishPath3() override;

    // FNV-1a 64 chained over every record so far (offset basis when empty).
    uint64_t Hash() const { return hash_; }
    // Records recorded so far (retained or not).
    size_t Count() const { return count_; }
    // The retained records; empty for a hash-only stream.
    const std::vector<RegisterOp>& Ops() const { return ops_; }
    bool RetainsRecords() const { return retain_; }
    // Bytes held by the retained records (0 for a hash-only stream).
    size_t RetainedBytes() const { return ops_.capacity() * sizeof(RegisterOp); }
    void Clear();

private:
    void Push(RegisterOpKind kind, uint8_t reg, uint32_t aux, uint64_t lo, uint64_t hi);

    std::vector<RegisterOp> ops_;
    uint64_t hash_;
    size_t count_ = 0;
    bool retain_ = true;
    uint8_t path_ = 0;
};
