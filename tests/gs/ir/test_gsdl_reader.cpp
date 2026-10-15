// tests/gs/ir/test_gsdl_reader.cpp -- GsdlReader proofs.
//
//   test_gsdl_reader roundtrip <tmpdir>
//       Writes a synthetic v2 recording with the NEW structs (names, jobs with a
//       pixel payload, draws with vertex arrays, two closed frames and a truncated
//       tail frame; plus a v1 file) and reads it back: order, names, payloads,
//       the v1 zero-filled tail, and the tail-frame rule.
//   test_gsdl_reader census <capture.gsdl> <baseline.replay.txt>
//       Loads the capture and computes the CENSUS / PRIM / FLAGS / JOBS lines in
//       the replay tool's grammar; each must string-equal the baseline's line
//       after the "[GSREPLAY] " tag is stripped. Exit 77 when the corpus is absent.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/gs/hle/ir/gsdl_format.h"
#include "tests/lib/gsdl_reader.h"

using ps2r::gs::DrawRec;
using ps2r::gs::GsdlFileHeader;
using ps2r::gs::GsdlFormat;
using ps2r::gs::GsdlNameRec;
using ps2r::gs::GsdlReader;
using ps2r::gs::GsdlRecordHeader;
using ps2r::gs::JobKind;
using ps2r::gs::JobRec;
using ps2r::gs::VertexRec;

namespace {

int g_checks = 0, g_failures = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        g_checks++;                                                   \
        if (!(cond)) {                                                \
            g_failures++;                                             \
            std::printf("  FAIL line %d: ", __LINE__);                \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

// ---- a minimal writer over the NEW structs -----------------------------------
std::FILE* OpenRecording(const std::string& path, uint32_t version, uint32_t flags) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return nullptr;
    GsdlFileHeader h{};
    h.magic = GsdlFormat::kMagic; h.version = version; h.flags = flags; h.start_unix_ns = 12345;
    std::fwrite(&h, sizeof(h), 1, f);
    return f;
}
void WriteRecord(std::FILE* f, uint32_t kind, const void* p, size_t n, const void* extra = nullptr,
                 size_t nextra = 0) {
    GsdlRecordHeader rh{kind, static_cast<uint32_t>(n + nextra)};
    std::fwrite(&rh, sizeof(rh), 1, f);
    std::fwrite(p, 1, n, f);
    if (nextra) std::fwrite(extra, 1, nextra, f);
}

JobRec MakeJob(JobKind k, uint32_t seq, uint32_t frame, uint32_t a1 = 0, uint32_t a2 = 0) {
    JobRec j{};
    j.type = static_cast<uint32_t>(k); j.seq = seq; j.src_frame = frame; j.arg1 = a1; j.arg2 = a2;
    return j;
}

DrawRec MakeDraw(uint32_t seq, uint32_t frame, uint32_t fbp, uint32_t nverts, uint8_t seed,
                 std::vector<VertexRec>& verts, uint32_t handler = 0, uint32_t inst = 0) {
    DrawRec d{};
    d.seq = seq; d.src_frame = frame; d.prim_type = 4; d.alpha_blend = 1; d.alpha_b = 1; d.alpha_d = 1;
    d.frame_fbp = fbp; d.frame_fbw = 640; d.frame_fbmsk = 0xff000000u; d.zbuf_zbp = 0x1000; d.zbuf_psm = 0x30;
    d.test_zte = 1; d.test_ztst = 2; d.test_ate = 1; d.test_atst = 6; d.test_aref = 0x40;
    d.scissor_x1 = 639; d.scissor_y1 = 447;
    d.tex_enabled = 1; d.tpsm = 0x13; d.tw = 6; d.th = 6; d.tbp = 0x2000 + seed; d.cbp = 0x3000;
    d.vram_snapshot_bytes = 64; d.vram_snapshot_hash = 0x1000 + seed;
    d.hle_handler = handler; d.hle_frame_inst = inst; d.hle_image = handler ? 0x1122334455667788ull : 0;
    d.hle_kick_pc = handler ? 0x90 : 0; d.off_x = 2048.0f; d.off_y = 2176.5f; d.gs_context = 1;
    verts.clear();
    for (uint32_t i = 0; i < nverts; i++) {
        VertexRec v{};
        v.x = 100.f + i * 3.5f + seed; v.y = 200.f - i; v.z = static_cast<float>(0xFFFF00u - i);
        v.s = 0.1f * i; v.t = 0.5f; v.q = 1.f;
        v.r = static_cast<uint8_t>(seed + i); v.g = 64; v.b = 32; v.a = 128; v.fog = 0xff;
        v.draw_kick = i >= 2;
        verts.push_back(v);
    }
    d.vertex_count = nverts; d.verts_incl = 1;
    d.vertex_hash = GsdlFormat::VertexHash(verts.data(), verts.size());
    GsdlFormat::Bbox(verts.data(), verts.size(), d.bbox);
    return d;
}

int RoundTrip(const std::string& dir) {
    const std::string p1 = dir + "/gsdl_rt_v2.gsdl", p3 = dir + "/gsdl_rt_v1.gsdl";
    const uint32_t hid = GsdlFormat::HandlerId("vs_test_prog");
    std::vector<VertexRec> vb;
    {
        std::FILE* wf = OpenRecording(p1, 2, GsdlFormat::kFlagVerts | GsdlFormat::kFlagPixels);
        CHECK(wf != nullptr, "open %s", p1.c_str());
        // frame 1: scissor job, name, two HLE draws, upload with payload, VSync
        JobRec j = MakeJob(JobKind::SetScissor, 2, 1, 0x1234, 0x5678);
        WriteRecord(wf, GsdlFormat::kJob, &j, sizeof(j));
        GsdlNameRec nr{hid, 12};
        WriteRecord(wf, GsdlFormat::kName, &nr, sizeof(nr), "vs_test_prog", 12);
        DrawRec d = MakeDraw(3, 1, 0x7000, 6, 7, vb, hid, 0);
        WriteRecord(wf, GsdlFormat::kDraw, &d, sizeof(d), vb.data(), vb.size() * sizeof(VertexRec));
        d = MakeDraw(4, 1, 0x23000, 3, 9, vb, hid, 1);
        WriteRecord(wf, GsdlFormat::kDraw, &d, sizeof(d), vb.data(), vb.size() * sizeof(VertexRec));
        std::vector<uint8_t> px(256, 0xAB);
        JobRec up = MakeJob(JobKind::UploadVRAMTexture, 5, 1, 0x2F000, 64);
        up.payload_bytes = 256; up.payload_incl = 1; up.payload_hash = GsdlFormat::Fnv1aSampled(px.data(), px.size());
        WriteRecord(wf, GsdlFormat::kJob, &up, sizeof(up), px.data(), px.size());
        JobRec vs = MakeJob(JobKind::VSync, 6, 1, 1);
        WriteRecord(wf, GsdlFormat::kFrame, &vs, sizeof(vs));
        // frame 2: an HLE draw and a raw draw with NO vertex array (hash only), VSync
        d = MakeDraw(7, 2, 0x7000, 8, 3, vb, hid, 0);
        WriteRecord(wf, GsdlFormat::kDraw, &d, sizeof(d), vb.data(), vb.size() * sizeof(VertexRec));
        d = MakeDraw(8, 2, 0x8000, 3, 4, vb);
        d.verts_incl = 0;
        WriteRecord(wf, GsdlFormat::kDraw, &d, sizeof(d));
        vs = MakeJob(JobKind::VSync, 9, 2, 0);
        WriteRecord(wf, GsdlFormat::kFrame, &vs, sizeof(vs));
        // truncated tail: one job + one draw whose vertex bytes are cut short
        j = MakeJob(JobKind::InvalidateTextures, 10, 3, 0, 0x400);
        WriteRecord(wf, GsdlFormat::kJob, &j, sizeof(j));
        d = MakeDraw(11, 3, 0x7000, 4, 5, vb);
        GsdlRecordHeader rh{GsdlFormat::kDraw, static_cast<uint32_t>(sizeof(d) + 2 * sizeof(VertexRec))};
        std::fwrite(&rh, sizeof(rh), 1, wf);
        std::fwrite(&d, sizeof(d), 1, wf);
        std::fwrite(vb.data(), sizeof(VertexRec), 1, wf);  // only ONE of the two promised vertices
        std::fclose(wf);
    }
    GsdlReader::Recording r;
    CHECK(GsdlReader::Load(p1.c_str(), r), "load v2");
    CHECK(r.truncated, "short final record marks the recording truncated");
    CHECK(r.hdr.version == 2 && r.hdr.flags == 3, "hdr");
    // The cut record is dropped; the job before it becomes a tail frame (vsync UINT32_MAX).
    CHECK(r.frames.size() == 3, "frames %zu", r.frames.size());
    if (r.frames.size() == 3) {
        CHECK(r.frames[2].vsync_frame == UINT32_MAX && r.frames[2].jobs.size() == 1 && r.frames[2].draws.empty(),
              "tail frame keeps the job before the cut record");
    }
    if (r.frames.size() >= 2) {
        const GsdlReader::Frame& f1 = r.frames[0];
        CHECK(f1.vsync_frame == 1 && f1.vsync.arg1 == 1 && f1.vsync.seq == 6, "frame 1 vsync");
        CHECK(f1.draws.size() == 2 && f1.jobs.size() == 2, "frame 1 counts");
        CHECK(f1.order.size() == 4 && f1.order[0] == 0 && f1.order[1] == 1 && f1.order[2] == 1 && f1.order[3] == 0, "order");
        CHECK(f1.jobs[0].j.type == 1 && f1.jobs[0].j.arg1 == 0x1234, "scissor job");
        CHECK(f1.jobs[1].j.type == 9 && f1.jobs[1].pixels.size() == 256 && f1.jobs[1].pixels[0] == 0xAB, "upload payload");
        const GsdlReader::Draw& d = f1.draws[0];
        CHECK(d.d.vertex_count == 6 && d.verts.size() == 6, "verts loaded");
        CHECK(d.d.hle_handler == hid && d.d.hle_frame_inst == 0 && f1.draws[1].d.hle_frame_inst == 1, "identity");
        CHECK(d.d.off_x == 2048.0f && d.d.off_y == 2176.5f && d.d.gs_context == 1, "offset/ctx");
        if (d.verts.size() == 6) {
            CHECK(d.verts[3].x == 100.f + 3 * 3.5f + 7 && d.verts[3].y == 197.f, "vertex xy");
            CHECK(d.verts[3].r == 10 && d.verts[3].draw_kick == 1 && d.verts[1].draw_kick == 0, "vertex rgba/kick");
        }
        CHECK(d.d.bbox[0] == 107.f && d.d.bbox[4] == 200.f, "bbox");
        CHECK(d.d.vertex_hash == GsdlFormat::VertexHash(d.verts.data(), d.verts.size()), "vertex hash re-computes");
        const GsdlReader::Frame& f2 = r.frames[1];
        CHECK(f2.vsync_frame == 2 && f2.vsync.arg1 == 0 && f2.draws.size() == 2 && f2.jobs.empty(), "frame 2");
        CHECK(f2.draws[1].verts.empty() && f2.draws[1].d.verts_incl == 0 && f2.draws[1].d.vertex_count == 3, "hash-only draw");
        CHECK(f2.draws[1].d.hle_handler == 0, "raw draw");
    }
    CHECK(r.names.size() == 1 && r.names[0].first == hid && r.names[0].second == "vs_test_prog", "name table");
    CHECK(r.Name(hid) && std::strcmp(r.Name(hid), "vs_test_prog") == 0 && r.Name(0) == nullptr, "Name()");

    // A tail frame: the same stream without the cut record keeps job 10 as a frame with vsync UINT32_MAX.
    {
        std::FILE* wf = OpenRecording(p3, 2, GsdlFormat::kFlagVerts);
        CHECK(wf != nullptr, "open tail");
        JobRec vs = MakeJob(JobKind::VSync, 1, 1, 1);
        WriteRecord(wf, GsdlFormat::kFrame, &vs, sizeof(vs));
        JobRec j = MakeJob(JobKind::InvalidateTextures, 2, 2, 0, 0x400);
        WriteRecord(wf, GsdlFormat::kJob, &j, sizeof(j));
        DrawRec d = MakeDraw(3, 2, 0x7000, 4, 5, vb);
        WriteRecord(wf, GsdlFormat::kDraw, &d, sizeof(d), vb.data(), vb.size() * sizeof(VertexRec));
        std::fclose(wf);
        GsdlReader::Recording t;
        CHECK(GsdlReader::Load(p3.c_str(), t), "load tail");
        CHECK(!t.truncated && t.frames.size() == 2, "tail frames %zu", t.frames.size());
        if (t.frames.size() == 2) {
            CHECK(t.frames[0].vsync_frame == 1 && t.frames[0].draws.empty() && t.frames[0].jobs.empty(), "empty frame 1");
            CHECK(t.frames[1].vsync_frame == UINT32_MAX && t.frames[1].jobs.size() == 1 && t.frames[1].draws.size() == 1, "tail");
            CHECK(t.frames[1].vsync.type == 0 && t.frames[1].vsync.seq == 0, "tail vsync zero");
        }
    }
    // v1: the 188-byte prefix; the v2 tail must read as zero even when the file bytes past it are garbage.
    {
        std::FILE* wf = OpenRecording(p3, 1, GsdlFormat::kFlagVerts);
        CHECK(wf != nullptr, "open v1");
        DrawRec d{};
        d.seq = 5; d.src_frame = 1; d.vertex_count = 1; d.verts_incl = 1; d.frame_fbp = 0xABC00; d.prim_type = 3;
        d.vertex_hash = 77;
        d.hle_handler = 0xDEADBEEF; d.hle_image = 0xFEEDFACE0ull; d.gs_context = 7;  // poison past the v1 size
        VertexRec v{}; v.x = 42.f;
        GsdlRecordHeader rh{GsdlFormat::kDraw, static_cast<uint32_t>(GsdlFormat::kDrawRecSizeV1 + sizeof(v))};
        std::fwrite(&rh, sizeof(rh), 1, wf);
        std::fwrite(&d, 1, GsdlFormat::kDrawRecSizeV1, wf);
        std::fwrite(&v, sizeof(v), 1, wf);
        JobRec vs = MakeJob(JobKind::VSync, 6, 1);
        WriteRecord(wf, GsdlFormat::kFrame, &vs, sizeof(vs));
        std::fclose(wf);
        GsdlReader::Recording v1;
        CHECK(GsdlReader::Load(p3.c_str(), v1), "load v1");
        CHECK(!v1.truncated && v1.hdr.version == 1 && v1.frames.size() == 1, "v1 shape");
        if (v1.frames.size() == 1 && v1.frames[0].draws.size() == 1) {
            const GsdlReader::Draw& vd = v1.frames[0].draws[0];
            CHECK(vd.d.seq == 5 && vd.d.frame_fbp == 0xABC00 && vd.d.prim_type == 3 && vd.d.vertex_hash == 77, "v1 fields");
            CHECK(vd.d.hle_handler == 0 && vd.d.hle_image == 0 && vd.d.gs_context == 0 && vd.d.off_x == 0.f, "v1 tail zero");
            CHECK(vd.verts.size() == 1 && vd.verts[0].x == 42.f, "v1 verts");
        } else {
            CHECK(false, "v1 file must hold one frame with one draw");
        }
        CHECK(v1.names.empty(), "v1 has no names");
    }
    // Rejections: bad magic, unsupported version.
    {
        std::FILE* wf = OpenRecording(p3, 3, 0);
        CHECK(wf != nullptr, "open v3");
        std::fclose(wf);
        GsdlReader::Recording x;
        CHECK(!GsdlReader::Load(p3.c_str(), x), "version 3 rejected");
        std::FILE* f = std::fopen(p3.c_str(), "wb");
        const char junk[24] = "not a recording at all";
        std::fwrite(junk, 1, sizeof(junk), f);
        std::fclose(f);
        CHECK(!GsdlReader::Load(p3.c_str(), x), "bad magic rejected");
    }
    std::remove(p1.c_str());
    std::remove(p3.c_str());
    return 0;
}

// ---- census in the replay tool's grammar ------------------------------------
std::vector<std::string> Census(const GsdlReader::Recording& r) {
    uint64_t frames = 0, draws = 0, verts = 0, jobs = 0, prim[8] = {}, jt[16] = {};
    uint64_t tex = 0, abe = 0, date = 0, fog = 0, iip = 0, up_payload = 0, up_nopayload = 0;
    for (const GsdlReader::Frame& f : r.frames) {
        frames++;
        for (const GsdlReader::Draw& d : f.draws) {
            const DrawRec& x = d.d;
            draws++; verts += x.vertex_count; prim[x.prim_type & 7]++;
            if (x.tex_enabled) tex++;
            if (x.alpha_blend) abe++;
            if (x.test_date) date++;
            if (x.fog_enabled) fog++;
            if (x.gouraud) iip++;
        }
        for (const GsdlReader::Job& j : f.jobs) {
            jobs++; jt[j.j.type & 15]++;
            if (j.j.type == static_cast<uint32_t>(JobKind::UploadVRAMTexture)) {
                if (j.j.payload_incl) up_payload++; else up_nopayload++;
            }
        }
    }
    char b[512];
    std::vector<std::string> out;
    std::snprintf(b, sizeof(b), "CENSUS frames=%llu draws=%llu verts=%llu jobs=%llu draws/frame=%.4f verts/frame=%.2f",
                  (unsigned long long)frames, (unsigned long long)draws, (unsigned long long)verts,
                  (unsigned long long)jobs, frames ? (double)draws / (double)frames : 0.0,
                  frames ? (double)verts / (double)frames : 0.0);
    out.push_back(b);
    std::snprintf(b, sizeof(b), "PRIM point=%llu line=%llu linestrip=%llu tri=%llu tristrip=%llu trifan=%llu sprite=%llu",
                  (unsigned long long)prim[0], (unsigned long long)prim[1], (unsigned long long)prim[2],
                  (unsigned long long)prim[3], (unsigned long long)prim[4], (unsigned long long)prim[5],
                  (unsigned long long)prim[6]);
    out.push_back(b);
    std::snprintf(b, sizeof(b), "FLAGS tex=%llu abe=%llu date=%llu fog=%llu iip=%llu", (unsigned long long)tex,
                  (unsigned long long)abe, (unsigned long long)date, (unsigned long long)fog, (unsigned long long)iip);
    out.push_back(b);
    std::string jl = "JOBS";
    static const char* jn[] = {"draw", "scis", "blend", "depth", "dispfb", "window", "clear", "vsync", "inval", "upload", "syncrt"};
    for (int i = 0; i < 11; i++) {
        if (!jt[i]) continue;
        std::snprintf(b, sizeof(b), " %s=%llu", jn[i], (unsigned long long)jt[i]);
        jl += b;
    }
    std::snprintf(b, sizeof(b), " upload_payload=%llu upload_nopayload=%llu", (unsigned long long)up_payload,
                  (unsigned long long)up_nopayload);
    jl += b;
    out.push_back(jl);
    return out;
}

int CensusMatches(const char* cap, const char* baseline) {
    std::FILE* bf = std::fopen(baseline, "r");
    if (!bf) {
        std::printf("SKIP: baseline %s absent\n", baseline);
        return 77;
    }
    std::vector<std::string> want;
    char line[2048];
    while (std::fgets(line, sizeof(line), bf)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        const std::string tag = "[GSREPLAY] ";
        if (s.compare(0, tag.size(), tag) != 0) continue;
        s = s.substr(tag.size());
        if (s.compare(0, 7, "CENSUS ") == 0 || s.compare(0, 5, "PRIM ") == 0 || s.compare(0, 6, "FLAGS ") == 0 ||
            s.compare(0, 5, "JOBS ") == 0)
            want.push_back(s);
    }
    std::fclose(bf);
    CHECK(want.size() == 4, "baseline has %zu census lines", want.size());
    GsdlReader::Recording r;
    if (std::FILE* f = std::fopen(cap, "rb")) {
        std::fclose(f);
    } else {
        std::printf("SKIP: capture %s absent\n", cap);
        return 77;
    }
    CHECK(GsdlReader::Load(cap, r), "load %s", cap);
    const std::vector<std::string> got = Census(r);
    for (size_t i = 0; i < got.size() && i < want.size(); i++) {
        std::printf("  NEW %s\n  OLD %s\n", got[i].c_str(), want[i].c_str());
        CHECK(got[i] == want[i], "census line %zu differs", i);
    }
    // Hash oracle: the recorder's vertex_hash and bbox re-compute from the stored
    // vertex arrays with the NEW primitives, for every draw of the capture.
    size_t draws = 0, bad_hash = 0, bad_bbox = 0;
    for (const GsdlReader::Frame& f : r.frames)
        for (const GsdlReader::Draw& d : f.draws) {
            if (!d.d.verts_incl || d.verts.size() != d.d.vertex_count) continue;
            draws++;
            if (GsdlFormat::VertexHash(d.verts.data(), d.verts.size()) != d.d.vertex_hash) bad_hash++;
            float bb[6];
            GsdlFormat::Bbox(d.verts.data(), d.verts.size(), bb);
            if (std::memcmp(bb, d.d.bbox, sizeof(bb)) != 0) bad_bbox++;
        }
    CHECK(draws > 0 && bad_hash == 0, "vertex_hash re-computes for every capture draw (%zu bad of %zu)", bad_hash, draws);
    CHECK(bad_bbox == 0, "bbox re-computes for every capture draw (%zu bad of %zu)", bad_bbox, draws);
    std::printf("  frames=%zu truncated=%d names=%zu vertex_hash re-computed for %zu draws\n", r.frames.size(),
                (int)r.truncated, r.names.size(), draws);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    int rc = 1;
    if (argc >= 3 && std::strcmp(argv[1], "roundtrip") == 0) rc = RoundTrip(argv[2]);
    else if (argc >= 4 && std::strcmp(argv[1], "census") == 0) rc = CensusMatches(argv[2], argv[3]);
    else {
        std::fprintf(stderr, "usage: test_gsdl_reader roundtrip <tmpdir> | census <cap.gsdl> <baseline.replay.txt>\n");
        return 2;
    }
    std::printf("test_gsdl_reader %s: %d checks, %d failures\n", argv[1], g_checks, g_failures);
    if (g_failures) return 1;
    return rc;
}
