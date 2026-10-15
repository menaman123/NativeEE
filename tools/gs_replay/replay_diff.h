// tools/gs_replay/replay_diff.h -- ReplayDiff: the two numeric comparison modes.
//
// RunDiff compares two `.gsrep` artifacts frame by frame over the downsample
// grids (equal grid required, min(frame counts) compared): per-channel mean and
// max delta, the share of pixels within --tol, and the verdict IDENT (full hash
// + dims equal) / OK (max <= tol, or the share >= --pct with pct < 100) / DIFF;
// exit 3 when the frame counts differ or any frame is DIFF.
// RunClassDiff compares the per-class draw / vertex totals of two `.gsdl`
// recordings (one loaded at a time) and, with --seq, the per-draw class sequence
// (first divergence + mismatch count); exit 3 on any difference, 2 on a load
// failure.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "engine/gs/hle/classrdr/class_classifier.h"
#include "tools/gs_replay/frame_range.h"

namespace ps2r {
namespace tools {

class ReplayDiff {
public:
    static int RunDiff(const char* pa, const char* pb, int tol, double pct_req, size_t max_lines);
    static int RunClassDiff(const gs::ClassClassifier::Rules& rules, const char* pa, const char* pb,
                            const FrameRange& fr, bool want_seq);

private:
    struct ClassProfile {
        std::map<std::string, uint64_t> draws, verts;
        std::vector<uint8_t> seq;   // class per draw, file order
        uint64_t total_draws = 0;
    };
    static bool BuildClassProfile(const gs::ClassClassifier::Rules& rules, const char* path, const FrameRange& fr,
                                  bool want_seq, ClassProfile& out);
};

}  // namespace tools
}  // namespace ps2r
