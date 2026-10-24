// tests/core/test_verify_units.cpp -- core/verify + core/triage/InputLog
// against the OLD tests/test_verify_units.cpp vectors (downsample rule, audio
// windows, recorder ranges) plus run.meta key set/order, FrameCap, the pad
// script replayer, and a VerifyHost leg written end-to-end (line formats and
// the silence-window value the OLD coldboot baseline pins).
//
// argv[1] = scratch dir.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "engine/core/config/config.h"
#include "engine/core/config/deterministic_profile.h"
#include "engine/core/triage/input_log.h"
#include "engine/core/verify/audio_hasher.h"
#include "engine/core/verify/frame_cap.h"
#include "engine/core/verify/frame_hasher.h"
#include "engine/core/verify/memory_checkpointer.h"
#include "engine/core/verify/run_meta.h"
#include "engine/core/verify/verify_host.h"

using namespace ps2r;
using namespace ps2r::verify;

static int g_fail = 0;
#define CHECK(x)                                                                  \
    do {                                                                          \
        if (!(x)) {                                                               \
            std::fprintf(stderr, "CHECK failed %s:%d: %s\n", __FILE__, __LINE__, #x); \
            g_fail++;                                                             \
        }                                                                         \
    } while (0)

static std::string g_scratch;

static std::vector<std::string> ReadLines(const std::string& path) {
    std::vector<std::string> out;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return out;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), f)) {
        std::string l(buf);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        out.push_back(l);
    }
    std::fclose(f);
    return out;
}

// OLD TestFrameHasherDownsample, verbatim vectors.
static void TestFrameHasherDownsample() {
    uint8_t phys[4 * 4 * 4];
    for (int ty = 0; ty < 4; ty++) {
        const int by = 4 - 1 - ty;
        for (int x = 0; x < 4; x++) {
            uint8_t* p = phys + (by * 4 + x) * 4;
            p[0] = (uint8_t)x;
            p[1] = (uint8_t)ty;
            p[2] = (uint8_t)(x * 16 + ty);
            p[3] = 0xAA;
        }
    }
    uint8_t native[2 * 2 * 4];
    for (int ty = 0; ty < 2; ty++) {
        const int by = 2 - 1 - ty;
        for (int x = 0; x < 2; x++) {
            uint8_t* p = native + (by * 2 + x) * 4;
            p[0] = (uint8_t)(x * 2);
            p[1] = (uint8_t)(ty * 2);
            p[2] = (uint8_t)((x * 2) * 16 + (ty * 2));
            p[3] = 0x55;
        }
    }
    const uint64_t h_ds = FrameHasher::HashNativeScale(phys, 4, 4, 2);
    const uint64_t h_native = FrameHasher::HashNativeScale(native, 2, 2, 1);
    CHECK(h_ds == h_native);
    CHECK(h_ds != 0);
    CHECK(FrameHasher::HashNativeScale(phys, 4, 4, 1) != h_ds);
    phys[(3 * 4 + 0) * 4 + 1] ^= 1;   // top-down (0,0) is sampled at scale 2
    CHECK(FrameHasher::HashNativeScale(phys, 4, 4, 2) != h_ds);
    CHECK(FrameHasher::HashNativeScale(nullptr, 4, 4, 1) == 0);
    CHECK(FrameHasher::HashNativeScale(phys, 0, 4, 1) == 0);
    // A black 640x448 native frame is a fixed constant (the Phase-1 stub lane).
    std::vector<uint8_t> black(640 * 448 * 4, 0);
    const uint64_t hb = FrameHasher::HashNativeScale(black.data(), 640, 448, 1);
    CHECK(hb != 0 && hb == FrameHasher::HashNativeScale(black.data(), 640, 448, 1));
}

// OLD TestAudioHasherWindows, verbatim vectors + the frozen silence value.
static void TestAudioHasherWindows() {
    std::vector<std::pair<uint64_t, uint64_t>> got;
    auto sink = [&got](uint64_t w, uint64_t h) { got.push_back({w, h}); };
    std::vector<float> samples(AudioHasher::kWindowFrames * 2);
    for (size_t i = 0; i < samples.size(); i++) samples[i] = (float)i * 0.001f;

    AudioHasher a(sink);
    a.Push(samples.data(), AudioHasher::kWindowFrames);
    CHECK(got.size() == 1 && got[0].first == 0);
    const uint64_t h_once = got[0].second;

    got.clear();
    AudioHasher b(sink);
    b.Push(samples.data(), 1000);
    b.Push(samples.data() + 2000, AudioHasher::kWindowFrames - 1000);
    CHECK(got.size() == 1 && got[0].second == h_once);

    got.clear();
    AudioHasher c(sink);
    c.Push(samples.data(), 100);
    c.Finish();
    CHECK(got.size() == 1 && got[0].second != h_once);
    c.Finish();   // nothing pending: no second window
    CHECK(got.size() == 1);

    got.clear();
    AudioHasher d(sink);
    for (int i = 0; i < 3; i++) d.Push(samples.data(), AudioHasher::kWindowFrames);
    CHECK(got.size() == 3 && got[2].first == 2 && d.WindowsEmitted() == 3);

    // 800 stereo frames of silence per boundary, 6 boundaries per window:
    // the OLD coldboot baseline's window 0 (PHASE1_PLAN section 5).
    got.clear();
    AudioHasher s(sink);
    std::vector<float> silence(800 * 2, 0.0f);
    for (int i = 0; i < 6; i++) s.Push(silence.data(), 800);
    CHECK(got.size() == 1 && got[0].second == 0x0a6fb8af536edb25ull);
    std::printf("silence window 0 = 0x%016llx\n", (unsigned long long)got[0].second);
}

static void TestMemoryCheckpointer() {
    std::vector<uint8_t> ram(4096, 0), spad(64, 1);
    const std::vector<MemorySpan> spans = {{ram.data(), ram.size()}, {spad.data(), spad.size()}};
    MemoryCheckpointer mc;
    uint64_t h = 0, h2 = 0;
    CHECK(mc.MaybeHash(0, spans, &h) && h != 0);
    CHECK(!mc.MaybeHash(1, spans, &h2) && !mc.MaybeHash(59, spans, &h2));
    CHECK(mc.MaybeHash(60, spans, &h2) && h2 == h);
    // Span order matters and equals one chained hash.
    std::vector<uint8_t> joined(ram);
    joined.insert(joined.end(), spad.begin(), spad.end());
    CHECK(MemoryCheckpointer::Hash({{joined.data(), joined.size()}}) == h);
    CHECK(MemoryCheckpointer::Hash({{spad.data(), spad.size()}, {ram.data(), ram.size()}}) != h);
    spad[3] = 2;
    CHECK(MemoryCheckpointer::Hash(spans) != h);
}

// OLD recorder ranges, with one-frame finalization for repeated samples.
static void TestInputLog() {
    triage::InputLog rec;
    std::vector<std::string> out;
    const uint8_t center[4] = {0x80, 0x80, 0x80, 0x80};
    rec.Feed(10, 0x4000, center, &out);
    CHECK(out.empty());
    rec.Feed(15, 0x4000, center, &out);
    CHECK(out.empty());
    rec.Feed(20, 0x0000, center, &out);
    CHECK(out.empty());
    rec.Flush(20, &out);
    CHECK(out.size() == 1 && out[0] == "10-19:CROSS");
    out.clear();
    const uint8_t lx0[4] = {0x00, 0x80, 0x80, 0x80};
    rec.Feed(30, 0, lx0, &out);
    rec.Feed(35, 0, center, &out);
    rec.Flush(35, &out);
    CHECK(out.size() == 1 && out[0] == "30-34:LX=0");
    out.clear();
    rec.Feed(40, 0x0001, center, &out);
    rec.Flush(45, &out);
    CHECK(out.size() == 1 && out[0] == "40-45:SELECT");
    for (int bit = 0; bit < 16; bit++) CHECK(triage::InputLog::ButtonToken(bit) != nullptr);
    CHECK(std::string(triage::InputLog::ButtonToken(3)) == "START" && triage::InputLog::ButtonMask("R1") == 0x0800);
    CHECK(triage::InputLog::ButtonMask("NOPE") == 0 && triage::InputLog::AxisToken(4) == nullptr);
    CHECK(triage::InputLog::HeaderLine(7, "T") == "# padlog v1 start_frame=7 title=T");
    // The script clock: the header names it (`clock=frame` | `clock=tick`); a header
    // without one is the tick clock (every earlier recording), and Key() picks the
    // boundary's index for the loaded clock.
    CHECK(triage::InputLog::HeaderLine(7, "T", triage::InputLog::Clock::Frame) ==
          "# padlog v1 start_frame=7 clock=frame title=T");
    CHECK(triage::InputLog::HeaderLine(7, "T", triage::InputLog::Clock::Tick) ==
          "# padlog v1 start_frame=7 clock=tick title=T");
    {
        triage::InputLog::Clock clock = triage::InputLog::Clock::Frame;
        CHECK(triage::InputLog::ParseHeader("# padlog v1 start_frame=7 title=T", &clock) && clock == triage::InputLog::Clock::Tick);
        CHECK(triage::InputLog::ParseHeader("# padlog v1 start_frame=7 clock=frame title=T", &clock) && clock == triage::InputLog::Clock::Frame);
        CHECK(triage::InputLog::ParseHeader("# padlog v1 start_frame=7 clock=tick title=T", &clock) && clock == triage::InputLog::Clock::Tick);
        CHECK(!triage::InputLog::ParseHeader("# restore_tick=5 previous_segment=x", &clock));
        triage::InputLog by_tick, by_frame;
        CHECK(by_tick.GetClock() == triage::InputLog::Clock::Tick && by_tick.Key(900, 300) == 900);
        by_frame.SetClock(triage::InputLog::Clock::Frame);
        CHECK(by_frame.Key(900, 300) == 300);
        const std::string fpath = g_scratch + "/frame.padlog";
        FILE* ff = std::fopen(fpath.c_str(), "w");
        std::fprintf(ff, "# restore_tick=5 previous_segment=x\n# padlog v1 start_frame=1 clock=frame title=T\n300-300:CROSS\n");
        std::fclose(ff);
        std::string ferr;
        CHECK(by_tick.LoadFile(fpath, &ferr) && by_tick.GetClock() == triage::InputLog::Clock::Frame && by_tick.Key(900, 300) == 300);
    }

    // Replayer: the recorder's lines round-trip through the loader.
    const std::string path = g_scratch + "/in.padlog";
    FILE* f = std::fopen(path.c_str(), "w");
    std::fprintf(f, "# padlog v1 start_frame=10 title=T\r\n10-19:CROSS \n\n30-34:LX=0\n40-45:SELECT\n50-51:BOGUS\n60-61:RX=300\n");
    std::fclose(f);
    triage::InputLog rep;
    std::string err;
    CHECK(rep.LoadFile(path, &err));
    CHECK(rep.Presses().size() == 2 && rep.Axes().size() == 1);
    CHECK(!rep.LoadFile(g_scratch + "/missing.padlog", &err) && err.find("cannot open") == 0);
    uint16_t wire = 0xFFFF;   // nothing pressed (active-low)
    uint8_t axes[4] = {0x80, 0x80, 0x80, 0x80};
    CHECK(rep.Apply(15, &wire, axes) && wire == (uint16_t)~0x4000 && axes[0] == 0x80);
    wire = 0xFFFF;
    CHECK(!rep.Apply(25, &wire, axes) && wire == 0xFFFF);
    CHECK(rep.Apply(32, &wire, axes) && axes[0] == 0 && wire == 0xFFFF);
    wire = 0xFFFF;
    CHECK(rep.Apply(45, &wire, axes) && wire == (uint16_t)~0x0001);
    triage::InputLog spec;
    CHECK(spec.ParseSpec("1-2:START,3-4:RY=255,5-6:L1,junk") == 3);
    CHECK(spec.Presses()[0].mask == 0x0008 && spec.Presses()[1].mask == 0x0400 && spec.Axes()[0].value == 255);
    triage::InputLog empty;
    CHECK(empty.Empty() && empty.ParseSpec("") == 0);
}

static void TestInputRecordingContinuation() {
    triage::InputLog recorder;
    std::vector<std::string> lines;
    const uint8_t center[4] = {128, 128, 128, 128};
    const uint8_t moved[4] = {200, 0, 128, 128};
    // A discarded same-frame sample must not manufacture a press or stick move.
    recorder.Feed(0, 0x4000, moved, &lines);
    recorder.Feed(0, 0, center, &lines);
    recorder.Feed(1, 0x4008, moved, &lines);
    recorder.Feed(2, 0, center, &lines);
    recorder.Feed(2, 0x4008, moved, &lines);
    recorder.Flush(2, &lines);
    // Periodic flush must not lose a button held through the flush boundary.
    recorder.Feed(3, 0x4008, moved, &lines);
    recorder.Feed(4, 0, center, &lines);
    recorder.Flush(4, &lines);
    const size_t count = lines.size();
    recorder.Flush(4, &lines);
    CHECK(count == lines.size());
    std::string spec;
    for (const std::string& line : lines) spec += (spec.empty() ? "" : ",") + line;
    triage::InputLog replay;
    CHECK(replay.ParseSpec(spec) == lines.size());
    for (uint32_t frame = 0; frame <= 4; ++frame) {
        const bool held = frame >= 1 && frame <= 3;
        uint16_t wire = 0xffff;
        uint8_t axes[4] = {128, 128, 128, 128};
        replay.Apply(frame, &wire, axes);
        CHECK(wire == (held ? static_cast<uint16_t>(~0x4008) : 0xffff));
        for (int axis = 0; axis != 4; ++axis)
            CHECK(axes[axis] == (held ? moved[axis] : center[axis]));
    }
}

static void TestIncrementalRecording() {
    Config c;
    c.harness.out_dir = g_scratch + "/recording";
    c.harness.record_input = true;
    VerifyHost host;
    std::string err;
    CHECK(host.Arm(c, "test", "TITLE", &err));
    // Hold through two periodic writes; the disk prefix is available before exit.
    for (uint32_t frame = 100; frame <= 230; ++frame)
        host.OnPadServe(frame, static_cast<uint16_t>(~0x4008), 200, 128, 128, 128);
    const auto prefix = ReadLines(host.LegPath("input.padlog"));
    CHECK(prefix.size() == 7);
    triage::InputLog interrupted;
    CHECK(interrupted.LoadFile(host.LegPath("input.padlog"), &err));
    uint16_t wire = 0xffff;
    uint8_t axes[4] = {128, 128, 128, 128};
    CHECK(interrupted.Apply(220, &wire, axes) && wire == static_cast<uint16_t>(~0x4008) && axes[0] == 200);
    host.FlushInput();
    host.FlushInput();
    host.OnPadServe(231, 0xffff, 128, 128, 128, 128);
    host.Finalize();
    triage::InputLog complete;
    CHECK(complete.LoadFile(host.LegPath("input.padlog"), &err));
    for (uint32_t frame = 100; frame <= 231; ++frame) {
        wire = 0xffff;
        for (uint8_t& axis : axes) axis = 128;
        complete.Apply(frame, &wire, axes);
        CHECK(wire == (frame < 231 ? static_cast<uint16_t>(~0x4008) : 0xffff));
        CHECK(axes[0] == (frame < 231 ? 200 : 128));
    }
}

static void TestRunMetaAndFrameCap() {
    const std::string path = g_scratch + "/run.meta";
    RunMeta::Identity id;
    id.build_id = "NATIVEEE-BUILD-ID test";
    id.profile = "deterministic";
    id.env_ps2 = "a=1;profile=deterministic";
    id.env_hash = 0x1234;
    RunMeta m(path, id);
    m.Set("park", "0");
    m.OnPresent("none");
    std::vector<std::string> lines = ReadLines(path);
    const char* keys[] = {"build_id=", "gl_renderer=", "tree=", "profile=", "env_ps2=", "env_hash=", "frames=",
                          "wall_ms=", "finalized="};
    CHECK(lines.size() == 10);
    for (int i = 0; i < 9 && i < (int)lines.size(); i++) CHECK(lines[i].rfind(keys[i], 0) == 0);
    CHECK(lines.size() > 9 && lines[9] == "park=0");
    CHECK(lines.size() > 2 && lines[2] == "tree=new" && lines[1] == "gl_renderer=none");
    CHECK(lines.size() > 5 && lines[5] == "env_hash=0x0000000000001234" && lines[6] == "frames=0" && lines[8] == "finalized=0");   // early stamp precedes the count, as OLD
    for (int i = 0; i < 70; i++) m.OnPresent(nullptr);
    m.Finalize();
    m.Finalize();
    lines = ReadLines(path);
    CHECK(lines.size() > 8 && lines[6] == "frames=71" && lines[8] == "finalized=1");
    CHECK(m.Render().find("frames=71\n") != std::string::npos);

    ps2r::verify::FrameCap cap;
    int fin = 0;
    cap.AddFinalizer([&] { fin++; });
    CHECK(!cap.Armed());
    for (int i = 0; i < 5; i++) CHECK(!cap.OnBoundary());
    cap.Arm(8);
    CHECK(cap.Armed() && !cap.OnBoundary() && !cap.OnBoundary() && cap.OnBoundary() && cap.Count() == 8);
    cap.RunFinalizers();
    cap.RunFinalizers();
    CHECK(fin == 1);
}

static void TestRecordingSegments() {
    Config config;
    config.harness.out_dir = g_scratch + "/segments";
    std::filesystem::remove_all(config.harness.out_dir);
    config.harness.record_input = true;
    VerifyHost host;
    std::string error;
    CHECK(host.Arm(config, "test", "TITLE", &error));
    host.OnPadServe(400, uint16_t(~0x4000), 200, 128, 128, 128);
    host.OnPadServe(401, 0xffff, 128, 128, 128, 128);
    host.FlushInput();
    const auto first = ReadLines(host.InputPath());
    CHECK(!first.empty());
    // No rewind without an explicit segment; old bytes remain intact.
    bool rejected = false;
    try { host.OnPadServe(100, 0xffff, 128, 128, 128, 128); } catch (...) { rejected = true; }
    CHECK(rejected && ReadLines(host.InputPath()) == first);
    // Occupied names cannot be overwritten. Exhaustion leaves the old writer
    // usable and its existing bytes untouched.
    for (unsigned index = 1; index <= 64; ++index) {
        char name[48]; std::snprintf(name, sizeof(name), "input-%06u.padlog", index);
        FILE* file = std::fopen(host.LegPath(name).c_str(), "w");
        CHECK(file != nullptr);
        if (file) { std::fputs("previous attempt\n", file); std::fclose(file); }
    }
    CHECK(!host.BeginInputSegment(100, &error));
    CHECK(host.InputPath() == host.LegPath("input.padlog") && ReadLines(host.InputPath()) == first);
    std::filesystem::remove(host.LegPath("input-000002.padlog"));
    CHECK(host.BeginInputSegment(100, &error));
    CHECK(host.InputPath() == host.LegPath("input-000002.padlog"));
    host.OnPadServe(100, uint16_t(~0x0008), 128, 128, 90, 128);
    host.OnPadServe(102, 0xffff, 128, 128, 128, 128);
    host.FlushInput();
    CHECK(ReadLines(host.LegPath("input.padlog")) == first);
    CHECK(ReadLines(host.LegPath("input-000001.padlog")) == std::vector<std::string>{"previous attempt"});
    triage::InputLog replay;
    CHECK(replay.LoadFile(host.InputPath(), &error));
    uint16_t buttons = 0xffff;
    uint8_t axes[4] = {128, 128, 128, 128};
    CHECK(replay.Apply(101, &buttons, axes) && buttons == uint16_t(~0x0008) && axes[2] == 90);
    CHECK(host.Meta()->Render().find("pad_record_segment=2\n") != std::string::npos);
    host.Finalize();
    CHECK(!host.BeginInputSegment(80, &error) && !host.RecordingInput());
}

static void TestExplicitRecordingPath() {
    // `--record-input <file>`: the first segment is written to the named file,
    // its parent directory is created, run.meta names it, and an existing file
    // is never overwritten. Later restore segments keep the leg-local names.
    Config config;
    config.harness.out_dir = g_scratch + "/explicit_leg";
    const std::string destination = g_scratch + "/explicit_pads/session-1.padlog";
    std::filesystem::remove_all(config.harness.out_dir);
    std::filesystem::remove_all(g_scratch + "/explicit_pads");
    config.harness.record_input = true;
    config.harness.record_input_path = destination;
    {
        VerifyHost host;
        std::string error;
        CHECK(host.Arm(config, "test", "TITLE", &error));
        CHECK(host.InputPath() == destination);
        CHECK(host.Meta()->Render().find("pad_record_path=" + destination + "\n") != std::string::npos);
        host.OnPadServe(10, uint16_t(~0x4000), 128, 128, 128, 128);
        host.OnPadServe(12, 0xffff, 128, 128, 128, 128);
        host.FlushInput();
        const auto lines = ReadLines(destination);
        CHECK(lines.size() >= 2 && !lines[0].empty() && lines[0][0] == '#');
        CHECK(!std::filesystem::exists(host.LegPath("input.padlog")));
        // A restore segment continues inside the leg directory, naming the
        // explicit file as its predecessor.
        CHECK(host.BeginInputSegment(5, &error));
        CHECK(host.InputPath() == host.LegPath("input-000001.padlog"));
        const auto segment = ReadLines(host.InputPath());
        CHECK(!segment.empty() && segment[0].find("previous_segment=" + destination) != std::string::npos);
        host.Finalize();
        CHECK(ReadLines(destination) == lines);
    }
    {
        // The same destination refuses: a second session needs a fresh file.
        VerifyHost host;
        std::string error;
        CHECK(!host.Arm(config, "test", "TITLE", &error));
        CHECK(error.find("never overwritten") != std::string::npos);
    }
}

static void TestOutputSegments() {
    Config config;
    config.harness.out_dir = g_scratch + "/output_segments";
    std::filesystem::remove_all(config.harness.out_dir);
    config.harness.record_input = true;
    VerifyHost host;
    std::string error;
    CHECK(host.Arm(config, "segment-build", "TITLE", &error, {{"codec", "pinned"}}));
    const std::string first_dir = host.LegDir();
    host.OnPadServe(400, uint16_t(~0x4000), 200, 128, 128, 128);
    host.OnPadServe(401, 0xffff, 128, 128, 128, 128);
    host.FlushInput();
    const auto first_input = ReadLines(host.InputPath());
    std::vector<float> samples(800 * 2, 0.25f);
    host.OnPresent(400, nullptr, 0, 0, 1, "test-renderer");
    CHECK(host.OnBoundary(samples.data(), 800, {}) == 0);
    // A partial audio window belongs to its preceding epoch, not the resumed one.
    CHECK(ReadLines(host.LegPath("audio.ahash")).empty());
    for (unsigned index = 1; index <= 64; ++index) {
        char name[48]; std::snprintf(name, sizeof(name), "/output-%06u", index);
        std::filesystem::create_directory(first_dir + name);
    }
    CHECK(!host.BeginOutputSegment(100, 90, &error));
    CHECK(!host.OutputFailed() && host.RecordingInput()); // reservation failure changes no old writer
    CHECK(host.LegDir() == first_dir && host.OutputSegment() == 0 && host.Boundaries() == 1);
    CHECK(host.Meta()->Render().find("finalized=0\n") != std::string::npos);
    CHECK(ReadLines(host.InputPath()) == first_input && ReadLines(host.LegPath("audio.ahash")).empty());
    std::filesystem::remove(first_dir + "/output-000002");
    CHECK(host.BeginOutputSegment(100, 90, &error));
    CHECK(host.LegDir() == first_dir + "/output-000002");
    CHECK(host.OutputSegment() == 2 && host.SegmentStartBoundary() == 1 && host.Boundaries() == 1);
    CHECK(host.PresentKey(500, 1) == 101 && host.PresentKey(500, 2) == 102); // segment, not initial boot anchor
    CHECK(host.Meta()->Render().find("output_phase=awaiting_resume\n") != std::string::npos);
    CHECK(host.Meta()->Render().find("codec=pinned\n") != std::string::npos);
    CHECK(host.Meta()->Render().find("gl_renderer=test-renderer\n") != std::string::npos);
    CHECK(host.Meta()->Render().find("output_restore_tick=90\n") != std::string::npos);
    const auto old_frames = ReadLines(first_dir + "/frames.fhash");
    const auto old_audio = ReadLines(first_dir + "/audio.ahash");
    const auto old_memory = ReadLines(first_dir + "/mem.mhash");
    const auto finished_meta = ReadLines(first_dir + "/run.meta");
    CHECK(old_audio.size() == 1 && old_memory.size() == 1 && finished_meta.size() > 8);
    CHECK(finished_meta[6] == "frames=1" && finished_meta[8] == "finalized=1");
    host.OnPadServe(90, uint16_t(~0x0008), 128, 128, 90, 128);
    host.OnPadServe(92, 0xffff, 128, 128, 128, 128);
    for (unsigned i = 0; i < 6; ++i) {
        const auto boundary = host.OnBoundary(samples.data(), 800, {});
        CHECK(boundary == i + 1);
        host.OnPresent(host.PresentKey(500, boundary), nullptr, 0, 0, 1, "test-renderer");
    }
    CHECK(host.Meta()->Render().find("output_phase=active\n") != std::string::npos);
    host.FlushInput();
    CHECK(ReadLines(first_dir + "/frames.fhash") == old_frames);
    CHECK(ReadLines(first_dir + "/audio.ahash") == old_audio);
    CHECK(ReadLines(first_dir + "/mem.mhash") == old_memory);
    CHECK(ReadLines(first_dir + "/input.padlog") == first_input);
    CHECK(ReadLines(first_dir + "/run.meta") == finished_meta);
    auto audio = ReadLines(host.LegPath("audio.ahash"));
    CHECK(audio.size() == 1 && audio[0].rfind("0,0x", 0) == 0 && audio != old_audio);
    const auto memory = ReadLines(host.LegPath("mem.mhash"));
    CHECK(memory.size() == 1 && memory[0].rfind("0,0x", 0) == 0);
    triage::InputLog replay;
    CHECK(replay.LoadFile(host.InputPath(), &error));
    uint16_t buttons = 0xffff; uint8_t axes[4] = {128,128,128,128};
    CHECK(replay.Apply(91, &buttons, axes) && buttons == uint16_t(~0x0008) && axes[2] == 90);
    const auto selected_dir = host.LegDir();
    CHECK(!host.BeginOutputSegment(80, UINT64_MAX, &error) && host.LegDir() == selected_dir);
    CHECK(!host.BeginOutputSegment(UINT64_MAX, 70, &error) && host.LegDir() == selected_dir);
    std::filesystem::remove(first_dir + "/output-000003");
    CHECK(host.BeginOutputSegment(80, 70, &error));
    CHECK(host.LegDir() == first_dir + "/output-000003"); // siblings, never nested timeline trees
    CHECK(host.SegmentStartBoundary() == 7 && host.Boundaries() == 7);
    CHECK(host.PresentKey(500, 7) == 81);
    CHECK(ReadLines(selected_dir + "/audio.ahash") == audio);
    host.Finalize();
    CHECK(!host.BeginOutputSegment(1, 1, &error));
    CHECK(ReadLines(first_dir + "/run.meta") == finished_meta);
    VerifyHost quiet;
    CHECK(quiet.OnBoundary(nullptr, 0, {}) == 0);
    CHECK(quiet.BeginOutputSegment(1, 1, &error) && quiet.OutputSegment() == 1);
    CHECK(quiet.SegmentStartBoundary() == 1 && quiet.OnBoundary(nullptr, 0, {}) == 1);
    CHECK(quiet.PresentKey(500, 1) == 2); // disabled hashing uses the same clock contract
}

static void TestPresentationKeys() {
    VerifyHost host;
    CHECK(host.PresentKey(0, 0) == 1 && host.PresentKey(0, 1) == 2);
    CHECK(host.PresentKey(1500, 0) == 1 && host.PresentKey(1500, 1) == 1501);
    CHECK(host.PresentKey(1500, 40) == 1540);
    CHECK(host.PresentKey(0, UINT32_MAX - 1) == UINT32_MAX);
    CHECK(host.PresentKey(1500, UINT32_MAX - 1500) == UINT32_MAX);
    unsigned refused = 0;
    try { host.PresentKey(0, UINT32_MAX); } catch (const std::runtime_error&) { ++refused; }
    try { host.PresentKey(1500, UINT32_MAX - 1499); } catch (const std::runtime_error&) { ++refused; }
    try { host.PresentKey(0, UINT64_MAX); } catch (const std::runtime_error&) { ++refused; }
    CHECK(refused == 3);
    for (unsigned i = 0; i < 8; ++i) host.OnBoundary(nullptr, 0, {});
    std::string error;
    CHECK(host.BeginOutputSegment(400, 300, &error));
    CHECK(host.PresentKey(1500, 8) == 401 && host.PresentKey(1500, 12) == 405);
    try { host.PresentKey(1500, 7); } catch (const std::runtime_error&) { ++refused; }
    try { host.PresentKey(1500, UINT64_MAX); } catch (const std::runtime_error&) { ++refused; }
    CHECK(refused == 5);
    CHECK(host.BeginOutputSegment(UINT32_MAX - 1, 300, &error));
    CHECK(host.PresentKey(1500, 8) == UINT32_MAX);
    try { host.PresentKey(1500, 9); } catch (const std::runtime_error&) { ++refused; }
    CHECK(refused == 6);
}

static void TestFailedOutputTransition() {
    Config config;
    config.harness.out_dir = g_scratch + "/failed_output_transition";
    std::filesystem::remove_all(config.harness.out_dir);
    config.harness.record_input = true;
    VerifyHost host;
    std::string error;
    CHECK(host.Arm(config, "failure-build", "TITLE", &error));
    host.OnPadServe(400, uint16_t(~0x4000), 128, 128, 128, 128);
    host.OnPresent(400, nullptr, 0, 0, 1, "fixture");
    std::vector<float> samples(800 * 2, 0.25f);
    host.OnBoundary(samples.data(), 800, {});
    const std::string first = host.LegDir(), metadata = host.LegPath("run.meta"), input_path = host.InputPath();
    // A real filesystem failure after the old tails finish, without changing
    // production I/O or requiring a privileged/full-disk fixture.
    CHECK(std::filesystem::remove(metadata));
    CHECK(std::filesystem::create_directory(metadata));
    CHECK(!host.BeginOutputSegment(100, 90, &error));
    CHECK(host.OutputFailed() && !host.OutputError().empty() && !host.RecordingInput());
    CHECK(error == host.OutputError());
    CHECK(host.OutputSegment() == 0 && host.LegDir() == first && host.Boundaries() == 1);
    CHECK(!std::filesystem::exists(first + "/output-000001"));
    const auto input = ReadLines(input_path), audio = ReadLines(host.LegPath("audio.ahash"));
    const auto frames = ReadLines(host.LegPath("frames.fhash"));
    CHECK(!input.empty() && audio.size() == 1 && frames.size() == 1);
    unsigned refused = 0;
    try { host.OnPresent(401, nullptr, 0, 0, 1, "fixture"); } catch (const std::runtime_error&) { ++refused; }
    try { host.OnBoundary(samples.data(), 800, {}); } catch (const std::runtime_error&) { ++refused; }
    try { host.OnPadServe(401, 0xffff, 128, 128, 128, 128); } catch (const std::runtime_error&) { ++refused; }
    try { host.FlushInput(); } catch (const std::runtime_error&) { ++refused; }
    CHECK(refused == 4 && host.Boundaries() == 1);
    CHECK(!host.BeginOutputSegment(100, 90, &error));
    CHECK(!host.BeginInputSegment(90, &error));
    CHECK(std::filesystem::remove(metadata));
    host.Finalize(); host.Finalize(); // close remains safe after the failed transition
    CHECK(host.Meta()->Render().find("output_phase=failed\n") != std::string::npos);
    CHECK(ReadLines(input_path) == input && ReadLines(host.LegPath("audio.ahash")) == audio);
    CHECK(ReadLines(host.LegPath("frames.fhash")) == frames);
}

static void TestVerifyHostLeg() {
    Config c;
    DeterministicProfile::Apply(c);
    c.meta.profile = "deterministic";
    c.harness.out_dir = g_scratch + "/leg";
    c.harness.headless = true;
    c.harness.record_input = true;
    VerifyHost host;
    std::string err;
    CHECK(host.Arm(c, "NATIVEEE-BUILD-ID t", "TITLE", &err));
    CHECK(host.Armed() && host.LegPath("x") == c.harness.out_dir + "/x");
    std::vector<uint8_t> black(64 * 64 * 4, 0);
    std::vector<uint8_t> ram(1024, 0);
    const std::vector<MemorySpan> spans = {{ram.data(), ram.size()}};
    std::vector<float> silence(800 * 2, 0.0f);
    for (uint32_t n = 0; n < 120; n++) {
        host.OnPresent(n + 1, black.data(), 64, 64, 1, "none");
        CHECK(host.OnBoundary(silence.data(), 800, spans) == n);
    }
    host.OnPresent(121, nullptr, 0, 0, 1, "none");
    host.OnPadServe(5, 0xFFFF, 0x80, 0x80, 0x80, 0x80);
    host.OnPadServe(6, (uint16_t)~0x4000, 0x80, 0x80, 0x80, 0x80);
    host.OnPadServe(9, 0xFFFF, 0x80, 0x80, 0x80, 0x80);
    host.Finalize();
    host.Finalize();
    const std::vector<std::string> fr = ReadLines(host.LegPath("frames.fhash"));
    const std::vector<std::string> au = ReadLines(host.LegPath("audio.ahash"));
    const std::vector<std::string> me = ReadLines(host.LegPath("mem.mhash"));
    const std::vector<std::string> rm = ReadLines(host.LegPath("run.meta"));
    const std::vector<std::string> pad = ReadLines(host.LegPath("input.padlog"));
    CHECK(fr.size() == 121 && fr[0].rfind("1,0x", 0) == 0 && fr[120] == "121,0x0000000000000000");
    CHECK(au.size() == 20 && au[0] == "0,0x0a6fb8af536edb25" && au[19].rfind("19,0x", 0) == 0);
    CHECK(me.size() == 2 && me[0].rfind("0,0x", 0) == 0 && me[1].rfind("60,0x", 0) == 0);
    CHECK(rm.size() >= 9 && rm[2] == "tree=new" && rm[3] == "profile=deterministic" && rm[6] == "frames=121" &&
          rm[8] == "finalized=1");
    CHECK(rm.size() >= 5 && rm[4] == "env_ps2=" + c.Fingerprint());
    CHECK(pad.size() == 2 && pad[0] == "# padlog v1 start_frame=5 clock=frame title=TITLE" && pad[1] == "6-8:CROSS");
    CHECK(std::find(rm.begin(), rm.end(), "pad_record_clock=frame") != rm.end());

    // Disarmed host: nothing written, entry points inert.
    Config off;
    VerifyHost quiet;
    CHECK(quiet.Arm(off, "b", "t", &err) && !quiet.Armed());
    quiet.OnPresent(1, nullptr, 0, 0, 1, nullptr);
    CHECK(quiet.OnBoundary(nullptr, 0, {}) == 0);
    quiet.Finalize();
}

int main(int argc, char** argv) {
    g_scratch = argc > 1 ? argv[1] : ".";
    TestFrameHasherDownsample();
    TestAudioHasherWindows();
    TestMemoryCheckpointer();
    TestInputLog();
    TestInputRecordingContinuation();
    TestIncrementalRecording();
    TestRecordingSegments();
    TestExplicitRecordingPath();
    TestOutputSegments();
    TestPresentationKeys();
    TestFailedOutputTransition();
    TestRunMetaAndFrameCap();
    TestVerifyHostLeg();
    std::printf("test_verify_units: %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
