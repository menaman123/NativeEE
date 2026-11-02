// Pure checks of the menu's panel logic: SavestateRows (listing/decoding of
// reproduction files), TriageSection::Report (the clipboard report) and the
// picture rect the presenter fits the anamorphic frame at (video.aspect). No
// window, GL context or SDL device is created.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include "app/gui/fps_overlay.h"
#include "app/gui/savestate_rows.h"
#include "app/gui/session_gui.h"
#include "app/gui/triage_section.h"
#include "engine/core/savestate/savestate_format.h"

namespace {
int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

using ps2r::app::SavestateRows;
using ps2r::app::SessionControls;
using ps2r::app::TriageSection;

void WriteHeaderFile(const std::filesystem::path& path, bool valid, uint32_t frame, uint64_t wall_ms, size_t padding) {
    ps2r::savestate::FileHeader header{};
    std::memcpy(header.magic, ps2r::savestate::kMagic, sizeof(header.magic));
    header.version = valid ? ps2r::savestate::kMinFileVersion : ps2r::savestate::kMaxFileVersion + 7;
    header.vsync_frame = frame;
    header.wall_ms = wall_ms;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    const std::string pad(padding, '\0');
    out.write(pad.data(), static_cast<std::streamsize>(pad.size()));
}

void TestNames() {
    CHECK(SavestateRows::IsSavestateName("reproduction-1.ps2st"));
    CHECK(SavestateRows::IsSavestateName("UPPER.PS2ST"));
    CHECK(!SavestateRows::IsSavestateName("reproduction-1.ps2st.tmp"));
    CHECK(!SavestateRows::IsSavestateName(".ps2st"));
    CHECK(!SavestateRows::IsSavestateName("notes.txt"));
    CHECK(!SavestateRows::IsSavestateName(""));
}

void TestHeaderDecode() {
    ps2r::savestate::FileHeader header{};
    std::memcpy(header.magic, ps2r::savestate::kMagic, sizeof(header.magic));
    header.version = ps2r::savestate::kMaxFileVersion;
    header.vsync_frame = 4242;
    header.wall_ms = 1700000000123ull;
    unsigned char bytes[sizeof(header)];
    std::memcpy(bytes, &header, sizeof(header));
    uint32_t frame = 0; uint64_t wall = 0;
    CHECK(SavestateRows::DecodeHeader(bytes, sizeof(bytes), frame, wall) && frame == 4242 && wall == 1700000000123ull);
    CHECK(!SavestateRows::DecodeHeader(bytes, sizeof(bytes) - 1, frame, wall));
    bytes[0] = 'X';
    CHECK(!SavestateRows::DecodeHeader(bytes, sizeof(bytes), frame, wall));
    std::memcpy(bytes, &header, sizeof(header));
    header.version = 0;
    std::memcpy(bytes, &header, sizeof(header));
    CHECK(!SavestateRows::DecodeHeader(bytes, sizeof(bytes), frame, wall));
    CHECK(!SavestateRows::DecodeHeader(nullptr, 64, frame, wall));
}

void TestScan(const std::filesystem::path& scratch) {
    const auto dir = scratch / "reproductions";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    WriteHeaderFile(dir / "older.ps2st", true, 100, 1000, 10);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));   // distinct second-resolution mtimes
    WriteHeaderFile(dir / "newer.ps2st", true, 200, 2000, 20);
    WriteHeaderFile(dir / "broken.ps2st", false, 300, 3000, 0);
    WriteHeaderFile(dir / "transient.ps2st.tmp", true, 400, 4000, 0);
    std::ofstream(dir / "notes.txt") << "not a state";
    std::filesystem::create_directories(dir / "folder.ps2st");   // a directory is never a row
    SavestateRows rows;
    std::string error;
    CHECK(rows.Scan(dir.string(), error) && error.empty());
    CHECK(rows.Scanned() && !rows.Truncated() && rows.Directory() == dir.string());
    CHECK(rows.Rows().size() == 3);
    if (rows.Rows().size() == 3) {
        // newest first; broken and newer share a stamp, so the name orders them
        CHECK(rows.Rows()[0].name == "broken.ps2st" && !rows.Rows()[0].valid);
        CHECK(rows.Rows()[1].name == "newer.ps2st" && rows.Rows()[1].valid && rows.Rows()[1].frame == 200 &&
              rows.Rows()[1].wall_ms == 2000 && rows.Rows()[1].size == 52);
        CHECK(rows.Rows()[2].name == "older.ps2st" && rows.Rows()[2].valid && rows.Rows()[2].frame == 100);
        CHECK(rows.Rows()[1].path == (dir / "newer.ps2st").string());
    }
    // a missing folder: no rows, explicit error, scanned state still true
    CHECK(!rows.Scan((scratch / "absent").string(), error) && !error.empty() && rows.Rows().empty() && rows.Scanned());
    CHECK(!rows.Scan("", error) && error.find("No reproduction folder") != std::string::npos);
    // deterministic sort for equal stamps
    std::vector<SavestateRows::Row> unsorted(3);
    unsorted[0].name = "b"; unsorted[0].mtime = 5;
    unsorted[1].name = "a"; unsorted[1].mtime = 5;
    unsorted[2].name = "c"; unsorted[2].mtime = 9;
    SavestateRows::SortNewestFirst(unsorted);
    CHECK(unsorted[0].name == "c" && unsorted[1].name == "a" && unsorted[2].name == "b");
    std::filesystem::remove_all(dir);
}

void TestReport() {
    SessionControls controls;
    SessionControls::Capabilities caps;
    caps.volume = caps.pause = true;
    SessionControls::RuntimeState state;
    state.frame = 777;
    state.paused = true;
    state.vsync = true;
    state.fps_cap = 0;
    state.window_width = 1280;
    state.window_height = 896;
    state.input_name = "Controller";
    state.recording = true;
    state.recording_path = "/tmp/play/session-1.padlog";
    state.restore_path = "/tmp/logs/reproductions/reproduction-1.ps2st";
    state.reproductions_path = "/tmp/logs/reproductions";
    state.game_saves_path = "/tmp/play/saves";
    controls.Publish(caps, state);
    const auto id = controls.Submit(SessionControls::Action::TogglePause);
    SessionControls::Request taken;
    CHECK(controls.TakeNext(taken) && taken.id == id);
    CHECK(controls.Finish(id, SessionControls::Phase::Succeeded, "Game paused."));
    const auto report = TriageSection::Report(controls.Read());
    CHECK(report.find("game tick: 777") != std::string::npos);
    CHECK(report.find("state: paused") != std::string::npos);
    CHECK(report.find("presenter: vsync on, uncapped, window 1280x896") != std::string::npos);
    CHECK(report.find("recording: on, /tmp/play/session-1.padlog") != std::string::npos);
    CHECK(report.find("last saved reproduction: /tmp/logs/reproductions/reproduction-1.ps2st") != std::string::npos);
    CHECK(report.find("game saves folder: /tmp/play/saves") != std::string::npos);
    CHECK(report.find(std::string("  #1 ") + SessionControls::ActionLabel(SessionControls::Action::TogglePause) + ": " +
                      SessionControls::PhaseLabel(SessionControls::Phase::Succeeded) + " -- Game paused.") != std::string::npos);
    CHECK(report.find("pending commands:\n  (none)") != std::string::npos);
    // A newly written reproduction becomes the selection (SessionControls::Publish).
    CHECK(report.find("selected reproduction: /tmp/logs/reproductions/reproduction-1.ps2st") != std::string::npos);
    if (g_failures) std::printf("---- report ----\n%s----\n", report.c_str());
}
}  // namespace

// SessionGui::PictureRect: the frame is anamorphic (512x448 for any TV), so a
// selected ratio fits a box of THAT shape (the 4:3 TV's 896x672 in the 960x672
// default window; a 16:9 window filled at 16:9), centred; ratio 0 keeps the
// pixel shape (the pre-A1 presentation, 768x672).
void TestPictureRect() {
    float x, y, w, h;
    ps2r::app::SessionGui::PictureRect(960, 672, 512, 448, 0.0, x, y, w, h);
    CHECK(w == 768 && h == 672 && x == 96 && y == 0);
    ps2r::app::SessionGui::PictureRect(960, 672, 512, 448, 4.0 / 3.0, x, y, w, h);
    CHECK(w == 896 && h == 672 && x == 32 && y == 0);
    ps2r::app::SessionGui::PictureRect(1920, 1080, 512, 448, 16.0 / 9.0, x, y, w, h);
    CHECK(w == 1920 && h == 1080 && x == 0 && y == 0);
    ps2r::app::SessionGui::PictureRect(1920, 1080, 512, 448, 4.0 / 3.0, x, y, w, h);
    CHECK(w == 1440 && h == 1080 && x == 240 && y == 0);
    ps2r::app::SessionGui::PictureRect(1920, 1200, 512, 448, 16.0 / 9.0, x, y, w, h);
    CHECK(w == 1920 && h == 1080 && x == 0 && y == 60);
    ps2r::app::SessionGui::PictureRect(2560, 1080, 512, 448, 21.0 / 9.0, x, y, w, h);
    CHECK(w == 2520 && h == 1080 && x == 20 && y == 0);
}

// The FPS counter's statistics (video.fps_overlay; app/gui/fps_overlay): the figure over a
// 0.5 s window, the frame time, the gap history, the colour thresholds and the glyph layout.
void TestFpsHistory() {
    using ps2r::app::FpsHistory;
    constexpr uint64_t kField = 16666667;   // 60 Hz in ns
    FpsHistory h;
    CHECK(h.Compute(0).fps == 0.0f && h.Compute(0).gap_count == 0);
    // A steady 60 Hz train: 60.0 within rounding whatever the window's phase; frame time 16.7 ms.
    uint64_t t = 1000000000;
    for (int i = 0; i < 90; ++i) { h.NoteShown(t); t += kField; }
    FpsHistory::Stats s = h.Compute(t - kField);
    CHECK(s.fps > 59.9f && s.fps < 60.1f);
    CHECK(s.frame_ms > 16.6f && s.frame_ms < 16.8f);
    CHECK(s.gap_count == 89);
    CHECK(s.gaps_ms[0] > 16.6f && s.gaps_ms[88] < 16.8f);
    s = h.Compute(t - kField + 7000000);   // a different phase inside the window: the same figure
    CHECK(s.fps > 59.9f && s.fps < 60.1f);
    // A 30 Hz stretch: the window's figure falls to 30, the history shows the 33 ms gaps.
    for (int i = 0; i < 80; ++i) { h.NoteShown(t); t += 2 * kField; }
    s = h.Compute(t - 2 * kField);
    CHECK(s.fps > 29.9f && s.fps < 30.1f);
    CHECK(s.frame_ms > 33.2f && s.frame_ms < 33.4f);
    CHECK(s.gap_count == FpsHistory::kHistory);                   // the ring is full
    CHECK(s.gaps_ms[FpsHistory::kHistory - 1] > 33.0f);           // the newest gap last
    CHECK(s.gaps_ms[0] > 16.6f && s.gaps_ms[0] < 16.8f);          // the oldest kept is from the 60 Hz stretch
    CHECK(h.Shown() == 170);
    // One 100 ms hitch inside a 60 Hz window: (n - 1) / span dips, the hitch is in the history.
    FpsHistory g;
    t = 5000000000ull;
    for (int i = 0; i < 20; ++i) { g.NoteShown(t); t += kField; }
    t += 100000000 - kField;
    g.NoteShown(t);
    for (int i = 0; i < 10; ++i) { t += kField; g.NoteShown(t); }
    s = g.Compute(t);
    CHECK(s.fps > 40.0f && s.fps < 55.0f);
    bool hitch = false;
    for (size_t i = 0; i < s.gap_count; ++i) hitch = hitch || (s.gaps_ms[i] > 99.0f && s.gaps_ms[i] < 101.0f);
    CHECK(hitch);
    // The pictures stopped: a stale newest picture reads 0.
    CHECK(g.Compute(t + 2000000000ull).fps == 0.0f);
    // Colours: green at 60 (>= 59.5), amber 50..59.5, red below -- and the gap form agrees.
    CHECK(ps2r::app::FpsColour(60.0f) == ps2r::app::FpsColour(59.6f));
    CHECK(ps2r::app::FpsColour(59.4f) == ps2r::app::FpsColour(50.0f));
    CHECK(ps2r::app::FpsColour(59.4f) != ps2r::app::FpsColour(60.0f));
    CHECK(ps2r::app::FpsColour(49.9f) != ps2r::app::FpsColour(50.0f));
    CHECK(ps2r::app::GapColour(16.67f) == ps2r::app::FpsColour(60.0f));
    CHECK(ps2r::app::GapColour(18.0f) == ps2r::app::FpsColour(55.0f));
    CHECK(ps2r::app::GapColour(33.3f) == ps2r::app::FpsColour(30.0f));
    // The glyph layout: every digit, the point and the labels have a width; a 28 px cell lays "60"
    // out wider than "6"; an unknown character takes half a cell.
    const float one = ps2r::app::FpsTextWidth("6", 28.0f);
    CHECK(one > 14.0f && one < 28.0f);
    CHECK(ps2r::app::FpsTextWidth("60", 28.0f) > one * 1.8f);
    CHECK(ps2r::app::FpsTextWidth("16.7", 14.0f) > ps2r::app::FpsTextWidth("167", 14.0f));
    CHECK(ps2r::app::FpsTextWidth("fps", 14.0f) > 0.0f && ps2r::app::FpsTextWidth("ms", 14.0f) > 0.0f);
    CHECK(ps2r::app::FpsTextWidth("?", 28.0f) > 6.0f && ps2r::app::FpsTextWidth("?", 28.0f) < 11.0f);
    // The panel's height with no draw list: positive and scaled by the cell.
    CHECK(ps2r::app::DrawFpsOverlay(nullptr, 1000.0f, 10.0f, 28.0f, s, ps2r::app::FpsOverlayTheme{}) > 28.0f);
    CHECK(ps2r::app::DrawFpsOverlay(nullptr, 1000.0f, 10.0f, 56.0f, s, ps2r::app::FpsOverlayTheme{}) >
          ps2r::app::DrawFpsOverlay(nullptr, 1000.0f, 10.0f, 28.0f, s, ps2r::app::FpsOverlayTheme{}) * 1.5f);
}

int main(int argc, char** argv) {
    const std::filesystem::path scratch = argc > 1 ? argv[1] : std::filesystem::temp_directory_path() / "ps2r_session_panels";
    std::filesystem::create_directories(scratch);
    TestNames();
    TestHeaderDecode();
    TestScan(scratch);
    TestReport();
    TestPictureRect();
    TestFpsHistory();
    std::printf("test_session_panels: %s (%d failures)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
