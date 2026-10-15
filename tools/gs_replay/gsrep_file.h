// tools/gs_replay/gsrep_file.h -- GsrepFile: the `.gsrep` replay artifact --
// a packed header (magic "GSRP", version 1, the downsample grid size, the frame
// count / chained replay hash / totals patched at close) followed by one packed
// frame record per replayed frame {vsync_frame, w, h, draws, verts, full_hash,
// ds_hash} and the ds_n x ds_n RGBA downsample bytes. Writer (append + patch-back),
// reader (for the diff mode) and the deterministic integer box-average downsample
// whose rounding the artifact pins (truncating division, rows in buffer order).
#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

namespace ps2r {
namespace tools {

#pragma pack(push, 1)
struct GsrepHeader {
    uint32_t magic, version;
    uint32_t ds_n;
    uint32_t frame_count;    // patched at close
    uint64_t replay_hash;    // patched at close
    uint64_t total_draws, total_verts;
};
struct GsrepFrame {
    uint32_t vsync_frame;
    uint32_t w, h;           // full framebuffer dims (0x0 = no target yet)
    uint32_t draws;
    uint64_t verts;
    uint64_t full_hash;      // FNV over the full-res RGBA bytes
    uint64_t ds_hash;        // FNV over the ds_n*ds_n*4 downsample that follows
};
#pragma pack(pop)

static_assert(sizeof(GsrepHeader) == 40, "GsrepHeader is 40 bytes");
static_assert(sizeof(GsrepFrame) == 40, "GsrepFrame is 40 bytes");

class GsrepFile {
public:
    static constexpr uint32_t kMagic = 0x50525347u;   // "GSRP"
    static constexpr uint32_t kVersion = 1;

    struct Loaded {
        GsrepHeader hdr{};
        std::vector<GsrepFrame> frames;
        std::vector<std::vector<uint8_t>> ds;
    };

    GsrepFile() = default;
    ~GsrepFile();
    GsrepFile(const GsrepFile&) = delete;
    GsrepFile& operator=(const GsrepFile&) = delete;

    // Opens `path` for writing and emits the provisional header; false on failure.
    bool Open(const char* path, uint32_t ds_n);
    bool IsOpen() const { return f_ != nullptr; }
    void AppendFrame(const GsrepFrame& fr, const std::vector<uint8_t>& ds);
    // Patches the header and closes.
    void Close(uint32_t frame_count, uint64_t replay_hash, uint64_t total_draws, uint64_t total_verts);

    // Reads a whole artifact (magic / version / grid checks); false with a message.
    static bool Load(const char* path, Loaded& out);
    // ds_n x ds_n RGBA box average of a w x h RGBA8 buffer (all zero when empty).
    static void Downsample(const std::vector<uint8_t>& rgba, int w, int h, uint32_t ds_n,
                           std::vector<uint8_t>& out);

private:
    std::FILE* f_ = nullptr;
    GsrepHeader hdr_{};
};

}  // namespace tools
}  // namespace ps2r
