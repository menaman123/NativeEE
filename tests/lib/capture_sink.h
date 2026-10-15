// tests/lib/capture_sink.h -- CaptureSink: a DrawIrSink that records every draw,
// job and frame it receives, for the GS state / transfer / replay tests.
#pragma once

#include <vector>

#include "engine/gs/hle/ir/draw_ir_sink.h"

namespace ps2r {
namespace gs {

class CaptureSink : public DrawIrSink {
public:
    void OnDraw(const DrawIr& d) override { draws.push_back(d); }
    void OnJob(const JobRec& j) override { jobs.push_back(j); }
    void OnFrame(const JobRec& f) override { frames.push_back(f); }
    std::vector<DrawIr> draws;
    std::vector<JobRec> jobs;
    std::vector<JobRec> frames;
};

}  // namespace gs
}  // namespace ps2r
