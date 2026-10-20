// tools/gs_replay/gs_replay.cpp -- the GS replay tool over the NEW GS HLE layers and
// the GL backend, title-neutral on this tree:
//
//   gs_replay stream <in.gsstream> [--scale N] [--warmup N] [--max-frames N] [--csv f]
//                    [--json f] [--dump-ppm DIR] [--dump-frames a-b] [--quiet] [--sync-capture] [--swap] [--cd-barrier]
//                    [--backend gl|metal]                                         (exit 77 = no GL / Metal device)
//       the renderer THROUGHPUT harness: the consumer alone over a captured consumer
//       stream (engine/gs/hle/capture), per-frame ms / fps / frames-lane hashes.
//
// The first title's corpus legs (replay / census / diff / classdiff over a `.gsdl`
// recording and its RendererProfile; census.cpp, gsrep_file.cpp, replay_diff.cpp,
// skip_filter.cpp stay for that driver) are not built here: this tree carries no
// renderer profile. Exit codes: 0 ok; 2 usage or a load failure; 77 no GL context
// (SKIP). Every knob is a flag: this tool reads no environment.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "tools/gs_replay/frame_range.h"
#include "tools/gs_replay/stream_replay_driver.h"

using namespace ps2r;
using ps2r::tools::FrameRange;

namespace {

int Usage() {
    std::fprintf(stderr,
                 "usage: gs_replay stream <in.gsstream> [--scale N] [--warmup N] [--max-frames N]\n"
                 "                 [--csv f] [--json f] [--dump-ppm DIR] [--dump-frames a-b]\n"
                 "                 [--quiet] [--sync-capture] [--swap] [--cd-barrier] [--backend gl|metal] [--metal-hw-depth] [--metal-commit-draws N] [--trace-draw N@F]\n"
                 "                 [--force-shader-blend] [--draw-limit N[@F]]\n"
                 "                 (exit 77 = no GL / Metal device)\n");
    return 2;
}

bool ParseRange(const char* s, FrameRange* fr) {
    unsigned a = 0, b = UINT32_MAX;
    if (std::sscanf(s, "%u-%u", &a, &b) < 1) return false;
    fr->lo = a;
    fr->hi = b;
    return true;
}

int MainStream(int argc, char** argv) {
    if (argc < 3) return Usage();
    tools::StreamReplayDriver::Options opt;
    opt.in_path = argv[2];
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) opt.scale = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--warmup") && i + 1 < argc) opt.warmup = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        else if (!std::strcmp(argv[i], "--max-frames") && i + 1 < argc) opt.max_frames = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        else if (!std::strcmp(argv[i], "--csv") && i + 1 < argc) opt.csv_path = argv[++i];
        else if (!std::strcmp(argv[i], "--json") && i + 1 < argc) opt.json_path = argv[++i];
        else if (!std::strcmp(argv[i], "--dump-ppm") && i + 1 < argc) opt.dump_dir = argv[++i];
        else if (!std::strcmp(argv[i], "--dump-frames") && i + 1 < argc) { if (!ParseRange(argv[++i], &opt.dump_range)) return Usage(); }
        else if (!std::strcmp(argv[i], "--quiet")) opt.quiet = true;
        else if (!std::strcmp(argv[i], "--sync-capture")) opt.deferred_capture = false;
        else if (!std::strcmp(argv[i], "--swap")) opt.swap_hidden_window = true;
        else if (!std::strcmp(argv[i], "--cd-barrier")) opt.cd_barrier = true;
        else if (!std::strcmp(argv[i], "--force-shader-blend")) opt.force_shader_blend = true;
        else if (!std::strcmp(argv[i], "--metal-hw-depth")) opt.metal_hw_depth = true;
        else if (!std::strcmp(argv[i], "--trace-draw") && i + 1 < argc) {
            char* end = nullptr;
            opt.trace_draw = static_cast<uint32_t>(std::strtoul(argv[++i], &end, 0));
            if (end && *end == '@') opt.trace_draw_frame = static_cast<uint32_t>(std::strtoul(end + 1, nullptr, 0));
        }
        else if (!std::strcmp(argv[i], "--metal-commit-draws") && i + 1 < argc)
            opt.metal_commit_draws = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 0));
        else if (!std::strcmp(argv[i], "--draw-limit") && i + 1 < argc) {
            char* end = nullptr;
            opt.draw_limit = static_cast<uint32_t>(std::strtoul(argv[++i], &end, 0));
            if (end && *end == '@') opt.draw_limit_frame = static_cast<uint32_t>(std::strtoul(end + 1, nullptr, 0));
        }
        else if (!std::strcmp(argv[i], "--backend") && i + 1 < argc) {
            const char* b = argv[++i];
            if (!std::strcmp(b, "metal")) opt.metal = true;
            else if (!std::strcmp(b, "gl")) opt.metal = false;
            else return Usage();
        }
        else return Usage();
    }
    tools::StreamReplayDriver driver(opt);
    return driver.Run();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return Usage();
    const std::string cmd = argv[1];
    if (cmd == "stream") return MainStream(argc, argv);
    return Usage();
}
