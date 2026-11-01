// tools/gs_replay/stream_census.h -- StreamCensus: the per-frame DRAW CENSUS of a .gsstream capture, no GPU.
//
// The static-closure side of the renderer's roofline (PERF1): for every frame of the capture, what the
// GS consumer was asked to do -- draws, vertices, triangles, the blended / textured / depth-tested
// shares, the FULL-SCREEN draws (a draw whose vertex extent covers >= `full_frac` of the frame buffer
// the draw targets: the heat-haze / bloom / blur / shadow passes that scale with scale^2), the
// render-to-texture reads (a textured draw whose TBP is a frame buffer written earlier in the frame),
// the distinct frame targets and the target switches (a different FRAME/ZBUF than the previous draw),
// the upload jobs and their bytes, the sum of the draws' bounding-box areas over the frame area (the
// overdraw factor the fill rate is spent on). One CSV row per frame; a summary of median / p99 / max
// per column and the heaviest frames by each of draws, full-screen passes, bbox area and upload bytes
// -- the worst captured frame the stress lane is built from.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps2r {
namespace tools {

struct StreamCensusRow {
    uint32_t frame = 0;
    uint32_t draws = 0, verts = 0, tris = 0, sprites = 0;
    uint32_t blended = 0, textured = 0, ztested = 0, zwrite = 0, atested = 0, gouraud = 0, fogged = 0;
    uint32_t fullscreen = 0;        // draws covering >= full_frac of their target
    uint32_t fullscreen_blended = 0;
    uint32_t rtt_reads = 0;         // textured draws sampling a frame buffer written earlier this frame
    uint32_t targets = 0;           // distinct FRAME.FBP this frame
    uint32_t target_switches = 0;   // draws whose (fbp, zbp) differ from the previous draw's
    uint32_t tex_switches = 0;      // textured draws whose (tbp, psm, cbp) differ from the previous textured draw's
    uint32_t uploads = 0;
    uint64_t upload_bytes = 0;
    uint32_t jobs = 0;
    double bbox_area_frames = 0.0;  // sum of bbox areas / (fbw x frame height) -- the overdraw factor
    double fullscreen_area_frames = 0.0;
    uint32_t psm_ct32 = 0, psm_ct16 = 0, psm_t8 = 0, psm_t4 = 0, psm_other = 0;   // textured draws by source PSM
};

struct StreamCensusOptions {
    const char* in_path = nullptr;
    const char* csv_path = nullptr;
    double full_frac = 0.9;         // a draw is full-screen when its extent covers this fraction of the target
    uint32_t frame_height = 448;    // the target's height assumed for the area fractions (the GS has no FRAME height)
    uint32_t top = 8;               // heaviest frames listed per criterion
    uint32_t max_frames = 0;
    bool quiet = false;
};

class StreamCensus {
public:
    explicit StreamCensus(const StreamCensusOptions& opt) : opt_(opt) {}
    int Run();
    const std::vector<StreamCensusRow>& Rows() const { return rows_; }

private:
    void PrintSummary() const;
    void WriteCsv() const;
    StreamCensusOptions opt_;
    std::vector<StreamCensusRow> rows_;
    uint64_t total_records_ = 0;
};

}  // namespace tools
}  // namespace ps2r
