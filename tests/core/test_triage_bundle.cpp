// tests/core/test_triage_bundle.cpp -- CoreTriageBundle: the crash path's
// attribution record (ALL-ON, NO EXCEPTIONS). A synthetic fault -- a thrown
// std::runtime_error caught on the throwing thread, NoteThrowSite'd -- composed
// with a DrawlistRing that saw more frames than it retains writes
// <scratch>/triage/<UTC>/ with every file present and non-empty: census.txt,
// drawlist_last200.txt (<= 200 frames, the newest; bounded ring), fault.txt
// (park / error / faulting draw / producer / throw site), backtrace.txt
// (backtrace_symbols_fd of the noted frames) and run.meta (park=exception,
// finalized=1). Also pins the ring's bound (no growth past its capacity), its
// Last(), and a second bundle in the same second (no overwrite).
//   test_triage_bundle <scratch dir>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

#include "engine/core/triage/triage_bundle.h"
#include "engine/gs/hle/ir/counting_draw_ir_sink.h"
#include "engine/gs/hle/ir/drawlist_ring.h"
#include "engine/platform/host/platform.h"
#include <filesystem>
#include <system_error>

using ps2r::gs::CountingDrawIrSink;
using ps2r::gs::DrawIr;
using ps2r::gs::DrawlistRing;
using ps2r::gs::JobKind;
using ps2r::gs::JobRec;
using ps2r::triage::TriageBundle;

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

std::string ReadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s;
}

bool NonEmptyFile(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && std::filesystem::file_size(path, ec) > 0;
}

size_t CountLines(const std::string& text, const char* prefix) {
    size_t n = 0, start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        if (text.compare(start, std::strlen(prefix), prefix) == 0) ++n;
        start = end + 1;
    }
    return n;
}

DrawIr MakeDraw(uint32_t seq, uint32_t frame, uint32_t handler) {
    DrawIr d;
    d.seq = seq;
    d.src_frame = frame;
    d.identity.hle_handler = handler;
    d.identity.frame_inst = frame;
    d.identity.kick_pc = 0x1a0;
    d.prim_type = 4;
    d.vertex_count = 6;
    d.raster.frame.fbp = 0x8000;
    d.raster.frame.fbw = 640;
    d.tex_enabled = true;
    d.tex.tbp = 0x100000;
    d.bbox[0] = 1.5f;
    return d;
}

JobRec MakeVsync(uint32_t seq, uint32_t frame) {
    JobRec j{};
    j.type = static_cast<uint32_t>(JobKind::VSync);
    j.seq = seq;
    j.src_frame = frame;
    j.arg1 = 1;
    return j;
}

void Fault() { throw std::runtime_error("Graphics Movie presentation release refused (synthetic)"); }

}  // namespace

int main(int argc, char** argv) {
    const std::string scratch = argc > 1 ? argv[1] : ".";
    const std::string root = scratch + "/triage_bundle_test";
    ps2r::host::MakeDirectory(root.c_str());

    // --- the bounded drawlist ring: 450 frames x 3 draws; the dump keeps the newest 200 ---
    CountingDrawIrSink tail;
    DrawlistRing ring(&tail, [](uint32_t id) -> const char* { return id == 0x77 ? "test_program" : nullptr; });
    uint32_t seq = 1;
    for (uint32_t frame = 1; frame <= 450; ++frame) {
        for (int k = 0; k < 3; ++k) ring.OnDraw(MakeDraw(seq++, frame, k == 0 ? 0x77u : 0u));
        ring.OnFrame(MakeVsync(seq++, frame));
    }
    ring.OnDraw(MakeDraw(seq++, 451, 0x77));   // the open (faulting) frame
    Check(ring.Frames() == 450 && ring.Draws() == 450 * 3 + 1, "ring counted 450 frames / 1351 draws");
    Check(tail.Frames() == 450 && tail.Draws() == 450 * 3 + 1, "ring forwarded every call to the next sink");
    DrawlistRing::DrawRecord last;
    Check(ring.Last(&last) && last.seq == seq - 1 && last.src_frame == 451 && last.handler == 0x77, "Last() is the newest draw");
    const std::string described = ring.Describe(last);
    Check(described.find("handler=0x77(test_program)") != std::string::npos && described.find("frame=451") != std::string::npos &&
              described.find("fb=0x8000/640") != std::string::npos && described.find("tex=0x100000") != std::string::npos,
          "Describe names the handler, frame, framebuffer and texture");
    const std::string dump = ring.Dump();
    const size_t frame_lines = CountLines(dump, "frame src_frame=");
    Check(frame_lines == 200 + 1, "dump covers exactly the last 200 closed frames + the open one");
    Check(dump.find("frame src_frame=251 ") != std::string::npos && dump.find("frame src_frame=250 ") == std::string::npos,
          "the oldest retained frame is 251 (450 - 200 + 1); 250 is gone");
    Check(dump.find("frame src_frame=451 draws=1 jobs=0 dropped=0 (open") != std::string::npos, "the open frame is marked");
    Check(CountLines(dump, "  draw seq=") == 200 * 3 + 1, "the retained draws are the last 200 frames' (601)");
    Check(dump.find("capacity=200/32768") != std::string::npos, "the dump states the fixed capacity");
    {
        // No growth past the capacity: 40,000 draws in one frame retain 32768, drop the rest.
        DrawlistRing big(nullptr);
        for (uint32_t i = 0; i < 40000; ++i) big.OnDraw(MakeDraw(i + 1, 9, 0));
        const std::string d = big.Dump();
        Check(d.find("draws_total=40000 draws_retained=32768") != std::string::npos, "the record ring is bounded at its capacity");
        Check(d.find("frame src_frame=9 draws=40000 jobs=0 dropped=7232") != std::string::npos, "the overflow is counted as dropped");
        Check(CountLines(d, "  draw seq=") == 32768, "exactly the capacity's draws are printed");
    }

    // --- the synthetic fault, caught on the throwing thread ---
    TriageBundle::Reset();
    Check(!TriageBundle::ThrowSiteNoted(), "no throw site before the fault");
    std::string what;
    try {
        Fault();
    } catch (const std::exception& e) {
        what = e.what();
        TriageBundle::NoteThrowSite(e.what());
    }
    Check(TriageBundle::ThrowSiteNoted() && what.find("synthetic") != std::string::npos, "the throw site is noted at the catch");
    TriageBundle::NoteThrowSite("a later fault");   // first wins

    TriageBundle::Files files;
    files.census = "build_id=test\nframes=451\nfinalized=1\npark=exception\npark_detail=" + what +
                   "\ngraphics_refusals_text=total=1,faults=1,first_fault=frame:117/source_frame:120/n:1/producer:0x1a01cc/detail:4/reason:ordering\n";
    files.run_meta = files.census;
    files.drawlist = dump;
    TriageBundle::Fault fault;
    fault.park = "exception";
    fault.pc = 0x1a01cc;
    fault.tid = 1;
    fault.detail = what;
    fault.last_draw = described;
    fault.producer = "graphics_refusals_text=total=1,faults=1,first_fault=frame:117/source_frame:120/n:1/producer:0x1a01cc/detail:4/reason:ordering";
    files.fault = TriageBundle::RenderFault(fault);
    std::string err;
    const std::string dir = TriageBundle::Write(root, files, &err);
    Check(!dir.empty(), ("bundle written: " + (dir.empty() ? err : dir)).c_str());
    Check(dir.rfind(root + "/triage/", 0) == 0 && dir.size() >= root.size() + 8 + 16, "the bundle lives under <root>/triage/<UTC timestamp>");
    for (const char* name : {"census.txt", "drawlist_last200.txt", "fault.txt", "backtrace.txt", "run.meta"})
        Check(NonEmptyFile(dir + "/" + name), (std::string("present and non-empty: ") + name).c_str());
    const std::string fault_txt = ReadFile(dir + "/fault.txt");
    Check(fault_txt.find("park=exception park_pc=0x1a01cc park_tid=1") != std::string::npos, "fault.txt: the park line");
    Check(fault_txt.find("error: " + what) != std::string::npos, "fault.txt: the error text");
    Check(fault_txt.find("faulting_draw: draw seq=") != std::string::npos && fault_txt.find("handler=0x77(test_program)") != std::string::npos,
          "fault.txt: the faulting draw");
    Check(fault_txt.find("producer: graphics_refusals_text=") != std::string::npos && fault_txt.find("producer:0x1a01cc") != std::string::npos,
          "fault.txt: the producer entry");
    Check(fault_txt.find("throw_site: noted on thread") != std::string::npos && fault_txt.find("what=" + what) != std::string::npos,
          "fault.txt: the noted throw site (first wins)");
    const std::string bt = ReadFile(dir + "/backtrace.txt");
    Check(bt.rfind("throwing thread ", 0) == 0 && bt.find(what) != std::string::npos, "backtrace.txt: headed by the throwing thread + what");
    Check(CountLines(bt, "") >= 3 && (bt.find("test_triage_bundle") != std::string::npos || bt.find("0x") != std::string::npos),
          "backtrace.txt: backtrace_symbols_fd frames follow");
    Check(ReadFile(dir + "/census.txt") == files.census && ReadFile(dir + "/run.meta").find("park=exception") != std::string::npos,
          "census.txt = the rows; run.meta carries park=exception");
    Check(ReadFile(dir + "/drawlist_last200.txt") == dump, "drawlist_last200.txt = the ring's dump");

    // A second bundle inside the same second gets its own directory.
    const std::string dir2 = TriageBundle::Write(root, files, &err);
    Check(!dir2.empty() && dir2 != dir, "a second bundle never overwrites the first");

    // Reset clears the note; a Write without a note uses the writer's own thread.
    TriageBundle::Reset();
    Check(!TriageBundle::ThrowSiteNoted(), "Reset clears the throw site");
    const std::string dir3 = TriageBundle::Write(root, files, &err);
    Check(!dir3.empty() && ReadFile(dir3 + "/backtrace.txt").rfind("writer thread ", 0) == 0, "without a noted site the writer's thread is traced");
    Check(TriageBundle::Write("/nonexistent/no/such/root", files, &err).empty() && !err.empty(), "an unwritable root fails with an error");

    if (g_failures) {
        std::printf("test_triage_bundle: %d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("test_triage_bundle: all checks passed\n");
    return 0;
}
