// tests/core/test_drawlist_recorder.cpp -- CoreDrawlistRecorder (diagnostics builds):
// the DrawlistRecorder decorator writes a GsdlFormat v2 recording that GsdlReader
// loads back with the same draw / job / frame counts the counting sink behind it
// saw, the DrawRec byte-identical to DrawIr::ToDrawRec() (verts_incl set), the
// vertex arrays and the kName record present, kJob payload_incl 0. Also: the
// recorder forwards every call to its next sink whether or not a file is open.
//   test_drawlist_recorder <scratch dir>
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/core/diag/drawlist_recorder.h"
#include "engine/gs/hle/ir/counting_draw_ir_sink.h"
#include "engine/gs/hle/ir/gsdl_format.h"
#include "tests/lib/gsdl_reader.h"

using ps2r::diag::DrawlistRecorder;
using ps2r::gs::CountingDrawIrSink;
using ps2r::gs::DrawIr;
using ps2r::gs::GsdlFormat;
using ps2r::gs::GsdlReader;
using ps2r::gs::JobKind;
using ps2r::gs::JobRec;
using ps2r::gs::VertexRec;

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

DrawIr MakeDraw(uint32_t seq, uint32_t handler, uint32_t nverts) {
    DrawIr d;
    d.seq = seq;
    d.src_frame = 7;
    d.prim_type = 3;
    d.gouraud = true;
    d.identity.hle_handler = handler;
    d.identity.image_hash = 0x1122334455667788ull;
    d.raster.frame.fbw = 640;
    d.raster.scissor.x1 = 639;
    d.vertex_count = nverts;
    for (uint32_t i = 0; i < nverts; ++i) {
        VertexRec v{};
        v.x = 1.0f * i;
        v.y = 2.0f * i;
        v.r = static_cast<uint8_t>(i);
        v.a = 0x80;
        d.verts.push_back(v);
    }
    return d;
}

JobRec MakeJob(JobKind kind, uint32_t seq, uint32_t payload_bytes) {
    JobRec j{};
    j.type = static_cast<uint32_t>(kind);
    j.seq = seq;
    j.src_frame = 7;
    j.arg1 = 0x1000;
    j.payload_bytes = payload_bytes;
    j.payload_hash = payload_bytes ? 0xabcdef : 0;
    return j;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: test_drawlist_recorder <scratch dir>\n");
        return 2;
    }
    const std::string path = std::string(argv[1]) + "/drawlist_recorder.gsdl";
    const uint32_t handler = GsdlFormat::HandlerId("vs_test_program");

    CountingDrawIrSink tail;
    DrawlistRecorder rec(&tail, [handler](uint32_t id) -> const char* {
        return id == handler ? "vs_test_program" : nullptr;
    });
    Check(!rec.IsOpen(), "closed before Open");
    // Calls before Open are forwarded, not recorded.
    rec.OnJob(MakeJob(JobKind::SetClearColor, 0, 0));
    Check(tail.Jobs() == 1 && rec.Jobs() == 0, "a call before Open reaches the next sink and is not counted as recorded");

    Check(rec.Open(path), "Open writes the header");
    const DrawIr d1 = MakeDraw(1, handler, 3);
    const DrawIr d2 = MakeDraw(2, 0, 4);
    DrawIr d3 = MakeDraw(3, handler, 6);
    d3.verts.resize(2);  // partial vertex array: not written, verts_incl 0
    rec.OnDraw(d1);
    rec.OnJob(MakeJob(JobKind::UploadVRAMTexture, 4, 64 * 64 * 4));
    rec.OnFrame(MakeJob(JobKind::VSync, 5, 0));
    rec.OnDraw(d2);
    rec.OnDraw(d3);
    rec.OnFrame(MakeJob(JobKind::VSync, 6, 0));
    rec.Close();
    Check(!rec.IsOpen(), "Close");
    Check(rec.Draws() == 3 && rec.Jobs() == 1 && rec.Frames() == 2, "recorder counts: 3 draws, 1 job, 2 frames");
    Check(tail.Draws() == 3 && tail.Jobs() == 2 && tail.Frames() == 2, "the next sink saw every call (3 draws, 2 jobs, 2 frames)");
    Check(rec.Report().find("draws=3 jobs=1 frames=2") != std::string::npos, "Report() names the counts");

    GsdlReader::Recording r;
    Check(GsdlReader::Load(path.c_str(), r), "GsdlReader loads the recording");
    Check(r.hdr.version == GsdlFormat::kVersion && r.hdr.flags == GsdlFormat::kFlagVerts,
          "header: version 2, flags = vertices only (pixels never cross the sink boundary)");
    Check(!r.truncated, "not truncated");
    Check(r.frames.size() == 2, "two frames closed by kFrame");
    size_t draws = 0, jobs = 0;
    for (const auto& f : r.frames) {
        draws += f.draws.size();
        jobs += f.jobs.size();
    }
    Check(draws == tail.Draws(), "draw count read back == the counting sink's Draws()");
    Check(jobs == 1 && r.frames[0].jobs.size() == 1 && r.frames[0].jobs[0].j.payload_incl == 0 &&
              r.frames[0].jobs[0].j.payload_bytes == 64 * 64 * 4 && r.frames[0].jobs[0].pixels.empty(),
          "kJob: the JobRec as received, payload_incl 0, size and hash kept, no pixel bytes");
    Check(r.frames[0].vsync.seq == 5 && r.frames[1].vsync.seq == 6, "kFrame carries the VSync job");
    Check(r.names.size() == 1 && r.names[0].first == handler && r.names[0].second == "vs_test_program",
          "kName once for the stamped handler (two draws, one name record)");
    if (r.frames.size() == 2 && r.frames[0].draws.size() == 1 && r.frames[1].draws.size() == 2) {
        ps2r::gs::DrawRec want = d1.ToDrawRec();
        want.verts_incl = 1;
        Check(std::memcmp(&r.frames[0].draws[0].d, &want, sizeof(want)) == 0,
              "kDraw: DrawRec byte-identical to DrawIr::ToDrawRec() with verts_incl 1");
        Check(r.frames[0].draws[0].verts.size() == 3 && r.frames[0].draws[0].verts[2].x == 2.0f &&
                  r.frames[0].draws[0].verts[2].r == 2,
              "the whole vertex array follows the DrawRec");
        Check(r.frames[1].draws[0].d.hle_handler == 0 && r.frames[1].draws[0].verts.size() == 4,
              "a raw draw records without a kName and with its vertices");
        Check(r.frames[1].draws[1].d.verts_incl == 0 && r.frames[1].draws[1].verts.empty() &&
                  r.frames[1].draws[1].d.vertex_count == 6,
              "a partial vertex array is not written (verts_incl 0, vertex_count kept)");
    } else {
        Check(false, "frame shapes: 1 draw then 2 draws");
    }

    // A path that cannot be created: Open false, calls still forwarded.
    DrawlistRecorder bad(&tail);
    Check(!bad.Open(std::string(argv[1]) + "/no_such_dir/x.gsdl"), "Open on an uncreatable path is false");
    bad.OnFrame(MakeJob(JobKind::VSync, 9, 0));
    Check(tail.Frames() == 3 && bad.Frames() == 0, "an unopened recorder forwards and records nothing");

    std::remove(path.c_str());
    std::printf("%s\n", g_failures ? "test_drawlist_recorder: FAIL" : "test_drawlist_recorder: PASS");
    return g_failures ? 1 : 0;
}
