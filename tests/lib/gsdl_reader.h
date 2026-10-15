// tests/lib/gsdl_reader.h -- GsdlReader: in-memory loader for `.gsdl` recordings
// (GsdlFormat), versions 1 and 2. Test and tool support only; the engine never
// reads recordings.
//
// Reader rules:
//   * reject a file whose header fails GsdlFormat::ValidateHeader;
//   * a kDraw payload shorter than the version's DrawRec size, a kName shorter
//     than its NameRec, a kJob/kFrame shorter than a JobRec, or a short read of a
//     payload marks the recording truncated and stops;
//   * v1 DrawRecs are the kDrawRecSizeV1 prefix, the v2 tail reads as zero;
//   * vertices are kept iff verts_incl and the payload holds them all; upload
//     pixels iff payload_incl and the payload holds them all (kJob only);
//   * kFrame closes the frame (vsync_frame = the job's src_frame, the job kept);
//   * Frame::order records the job/draw interleave in stream order;
//   * records after the last kFrame form a tail frame with vsync_frame UINT32_MAX.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "engine/gs/hle/ir/gsdl_format.h"

namespace ps2r {
namespace gs {

class GsdlReader {
public:
    struct Draw {
        DrawRec d;
        std::vector<VertexRec> verts;
    };
    struct Job {
        JobRec j;
        std::vector<uint8_t> pixels;
    };
    struct Frame {
        uint32_t vsync_frame = 0;
        std::vector<Draw> draws;
        std::vector<Job> jobs;        // non-draw jobs, in order
        std::vector<uint32_t> order;  // 0 = job, 1 = draw; index by counting
        JobRec vsync{};               // the closing VSync job (zero for a tail frame)
    };
    struct Recording {
        GsdlFileHeader hdr{};
        std::vector<Frame> frames;
        std::vector<std::pair<uint32_t, std::string>> names;  // handler -> name, file order
        bool truncated = false;
        const char* Name(uint32_t handler) const;
    };

    // Loads `path`; false (with a message on stderr) when it is not a recording.
    static bool Load(const char* path, Recording& r);
};

}  // namespace gs
}  // namespace ps2r
