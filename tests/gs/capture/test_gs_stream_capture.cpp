// tests/gs/capture/test_gs_stream_capture.cpp -- GsStreamRecorder -> GsStreamReader round
// trip (CPU-only): the records the consumer would receive come back in order with their
// payloads -- the image at open, the EE image's op log before the next event, a draw with
// its vertices, an upload job WITH its pixels (held to the next event, as the queue does),
// the frame with its CRTC snapshot, the restated display state of a windowed open -- and
// every call is forwarded to the next sink.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/gs/hle/capture/gs_stream_format.h"
#include "engine/gs/hle/capture/gs_stream_reader.h"
#include "engine/gs/hle/capture/gs_stream_recorder.h"
#include "engine/gs/vram/vram_op_log.h"

using namespace ps2r;
using namespace ps2r::gs;

static int g_fail = 0, g_checks = 0;
#define CHECK(c)                                                                          \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_fail; }  \
    } while (0)

namespace {
struct TestGsStreamCapture : DrawIrSink {   // the counting next sink (the file's one primary type)
    int draws = 0, jobs = 0, frames = 0;
    void OnDraw(const DrawIr&) override { ++draws; }
    void OnJob(const JobRec&) override { ++jobs; }
    void OnFrame(const JobRec&) override { ++frames; }
};

JobRec Job(JobKind k, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0, uint32_t a4 = 0) {
    JobRec j{};
    j.type = static_cast<uint32_t>(k);
    j.seq = 7;
    j.src_frame = 3;
    j.arg1 = a1; j.arg2 = a2; j.arg3 = a3; j.arg4 = a4;
    j.vram_epoch = 11;
    return j;
}
}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : ".";
    const std::string path = dir + "/roundtrip.gsstream";
    VramImage image;
    const uint8_t pattern[4] = {1, 2, 3, 4};
    image.WriteStream(0x1000, pattern, 4);
    image.SettleWrites();
    TestGsStreamCapture next;
    VramOpLog host_mirror;   // the mirror the host had installed (the queue's)
    std::vector<uint8_t> pixels(16 * 16 * 4, 0xAB);
    GsStreamRecorder::Hooks hooks;
    hooks.upload_pixels = [&pixels] { return &pixels; };
    hooks.crtc = [] {
        CrtcClassifier::Snapshot s;
        s.valid = true;
        s.cls = CrtcClassifier::Class::Other;
        s.fbp_bytes = 56 * 8192;
        s.fbw_px = 512;
        s.psm = 0;
        return s;
    };
    {
        GsStreamRecorder rec(&next, &host_mirror, hooks);
        // A windowed open: the display jobs before it are restated at the open.
        rec.OnJob(Job(JobKind::SetWindow, 0, (512u << 16) | 512u, (448u << 16) | 224u));
        rec.OnJob(Job(JobKind::SetFrontBuffer, 0x70000, 512, 0));
        rec.OnFrame(Job(JobKind::VSync, 1));            // frame 0: before the window
        rec.Arm(path, &image, /*first=*/1, /*count=*/1);
        rec.OnFrame(Job(JobKind::VSync, 1));            // frame boundary 1: the open
        CHECK(rec.IsOpen());
        // EE image writes between records travel as an op log (and reach the host's mirror).
        image.SetMirror(&rec);
        const uint8_t bytes[8] = {9, 8, 7, 6, 5, 4, 3, 2};
        image.WriteStream(0x2000, bytes, 8);
        image.SettleWrites();
        DrawIr ir;
        ir.seq = 5;
        ir.src_frame = 3;
        ir.vram_epoch = 11;
        ir.prim_type = 4;
        ir.raster.frame.fbp = 0x70000;
        ir.raster.frame.fbw = 8;
        ir.vertex_count = 3;
        ir.verts.resize(3);
        for (int i = 0; i < 3; ++i) { ir.verts[i].x = 10.f * i; ir.verts[i].y = 2.f; ir.verts[i].r = uint8_t(i); }
        rec.OnDraw(ir);
        JobRec up = Job(JobKind::UploadVRAMTexture, 0x3000, 16, 16, 1024);
        up.payload_bytes = static_cast<uint32_t>(pixels.size());
        rec.OnJob(up);                                   // held until the next event
        rec.OnJob(Job(JobKind::InvalidateTextures, 0x3000, 1024));
        rec.OnFrame(Job(JobKind::VSync, 1));            // frame boundary 2: the one captured frame
        CHECK(rec.IsOpen());
        rec.OnFrame(Job(JobKind::VSync, 1));            // frame boundary 3: count 1 reached -> closed before it
        CHECK(!rec.IsOpen());
        rec.OnDraw(ir);                                  // after the close: forwarded, not recorded
        image.SetMirror(nullptr);
        CHECK(next.draws == 2 && next.jobs == 4 && next.frames == 4);
        CHECK(host_mirror.Ops() == 2);                   // the forwarded WriteStream + SettleWrites
        CHECK(rec.Count().draws == 1 && rec.Count().jobs == 4 && rec.Count().frames == 1 && rec.Count().vram_ops == 1);
    }
    GsStreamReader reader;
    CHECK(reader.Open(path));
    GsStreamReader::Record r;
    // 1. the image at open
    CHECK(reader.Next(&r) && r.kind == GsStreamFormat::kVramInit && r.bytes.size() == VramImage::kBytes);
    CHECK(r.bytes.size() == VramImage::kBytes && std::memcmp(r.bytes.data() + 0x1000, pattern, 4) == 0);
    // 2-3. the restated display state
    CHECK(reader.Next(&r) && r.kind == GsStreamFormat::kJob && r.job.type == uint32_t(JobKind::SetWindow) && r.job.arg2 == ((512u << 16) | 512u));
    CHECK(reader.Next(&r) && r.kind == GsStreamFormat::kJob && r.job.type == uint32_t(JobKind::SetFrontBuffer) && r.job.arg1 == 0x70000);
    // (the opening boundary's own frame record is not written)
    // 4. the op log, replayable
    CHECK(reader.Next(&r) && r.kind == GsStreamFormat::kVramOps);
    {
        VramImage target;
        VramOpLog ops;
        ops.LoadEncoded(r.bytes.data(), r.bytes.size());
        CHECK(ops.Replay(target) == 2);
        uint8_t got[8];
        target.ReadView(0x2000, 8, got);
        CHECK(got[0] == 9 && got[7] == 2);
    }
    // 5. the draw with its vertices
    CHECK(reader.Next(&r) && r.kind == GsStreamFormat::kDraw && r.draw.verts.size() == 3 && r.draw.vertex_count == 3);
    CHECK(r.draw.verts.size() == 3 && r.draw.verts[2].x == 20.f && r.draw.verts[1].r == 1 && r.draw.raster.frame.fbp == 0x70000);
    // 6. the upload job WITH its pixels (written at the next event, before that event's record)
    CHECK(reader.Next(&r) && r.kind == GsStreamFormat::kJob && r.job.type == uint32_t(JobKind::UploadVRAMTexture) && r.job.payload_incl == 1);
    CHECK(r.bytes.size() == pixels.size() && r.bytes[0] == 0xAB);
    // 7. the invalidate, 8. the captured frame with its CRTC snapshot, then the end.
    CHECK(reader.Next(&r) && r.kind == GsStreamFormat::kJob && r.job.type == uint32_t(JobKind::InvalidateTextures));
    CHECK(reader.Next(&r) && r.kind == GsStreamFormat::kFrame && r.crtc.valid && r.crtc.fbp_bytes == 56 * 8192 && r.crtc.fbw_px == 512);
    CHECK(!reader.Next(&r) && reader.Error().empty());
    CHECK(reader.Records() == 8);
    std::remove(path.c_str());
    std::printf("%s: %d checks, %d failures\n", g_fail ? "FAIL" : "PASS", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
