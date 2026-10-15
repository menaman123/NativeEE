#pragma once
// The replay tool's GS-side recorder: every PATH1 packet a VU1 program kicks
// is kept verbatim (the arbiter's submit tap, in submission order) and the
// drained stream is decoded into a RegisterOpStream so a run's GS-visible
// output has both a byte image (packet diffing) and one hash (stream identity).
#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gs_sink.h"
#include "engine/gs/hle/ir/register_op_stream.h"

class XgkCaptureSink : public GsSink {
public:
    XgkCaptureSink() = default;

    // Install on the arbiter the unit's XGKICK submits to.
    void Attach(GifArbiter& arbiter);
    static void OnSubmit(void* user, GifPath path, const uint8_t* data, size_t size);

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

    const std::vector<std::vector<uint8_t>>& kicks() const { return kicks_; }
    size_t kick_bytes() const;
    const RegisterOpStream& ops() const { return ops_; }
    void Clear();

private:
    std::vector<std::vector<uint8_t>> kicks_;
    RegisterOpStream ops_;
};
