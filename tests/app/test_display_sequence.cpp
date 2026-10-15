// tests/app/test_display_sequence.cpp -- ctest DisplaySequenceSchedule (Charter 3 item 4,
// Bernoulli): the harness's scripted display changes, pure. "<frame>:<mode>[,...]" parses
// into the Display page's requests (mode -> SetDisplayMode 0..2, scaleN -> SetScale N,
// vsync/novsync -> SetVSync 1/0); a malformed item is skipped and counted; OnBoundary submits
// each step ONCE at the first boundary >= its frame, in order, remembers the first
// submission's boundary and pad reading, and the census renders the phase.
#include <cstdio>
#include <string>
#include <vector>

#include "app/host/display_sequence.h"

using namespace ps2r::app;
using Action = SessionControls::Action;

static int g_checks = 0, g_fails = 0;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(cond)) {                                                      \
            g_fails++;                                                      \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);   \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
        }                                                                   \
    } while (0)

int main() {
    // Every mode name -> its request.
    Action a{};
    int v = -1;
    CHECK(DisplaySequence::ModeOf("windowed", a, v) && a == Action::SetDisplayMode && v == 0, "windowed");
    CHECK(DisplaySequence::ModeOf("fullscreen", a, v) && a == Action::SetDisplayMode && v == 1, "fullscreen");
    CHECK(DisplaySequence::ModeOf("borderless", a, v) && a == Action::SetDisplayMode && v == 2, "borderless");
    CHECK(DisplaySequence::ModeOf("scale1", a, v) && a == Action::SetScale && v == 1, "scale1");
    CHECK(DisplaySequence::ModeOf("scale4", a, v) && a == Action::SetScale && v == 4, "scale4");
    CHECK(DisplaySequence::ModeOf("vsync", a, v) && a == Action::SetVSync && v == 1, "vsync");
    CHECK(DisplaySequence::ModeOf("novsync", a, v) && a == Action::SetVSync && v == 0, "novsync");
    CHECK(!DisplaySequence::ModeOf("scale5", a, v), "scale5 is not a mode");
    CHECK(!DisplaySequence::ModeOf("scale", a, v), "scale without a factor is not a mode");
    CHECK(!DisplaySequence::ModeOf("maximized", a, v), "maximized is not a mode");
    CHECK(!DisplaySequence::ModeOf("", a, v), "empty is not a mode");

    // The parse: order kept, frames as written, the malformed skipped and counted.
    DisplaySequence seq("600:borderless,720:windowed,840:fullscreen,960:scale2,1080:vsync,1200:novsync");
    CHECK(seq.Size() == 6 && seq.Skipped() == 0, "6 steps, 0 skipped (%zu/%d)", seq.Size(), seq.Skipped());
    CHECK(seq.PresenterSteps() == 5, "5 presenter steps (the scale step runs on the consumer): %d", seq.PresenterSteps());
    CHECK(seq.Steps()[0].frame == 600 && seq.Steps()[0].action == Action::SetDisplayMode && seq.Steps()[0].value == 2 &&
              seq.Steps()[0].mode == "borderless", "step 0");
    CHECK(seq.Steps()[3].frame == 960 && seq.Steps()[3].action == Action::SetScale && seq.Steps()[3].value == 2, "step 3");
    CHECK(seq.Steps()[5].frame == 1200 && seq.Steps()[5].action == Action::SetVSync && seq.Steps()[5].value == 0, "step 5");
    DisplaySequence bad("abc,0:windowed,300,300:,:windowed,300:maximized,400:borderless");
    CHECK(bad.Size() == 1 && bad.Skipped() == 6, "one good item of seven (%zu/%d)", bad.Size(), bad.Skipped());
    DisplaySequence none("");
    CHECK(none.Empty() && none.Census(0, 0) == "off", "empty -> off (%s)", none.Census(0, 0).c_str());

    // The schedule: nothing before the frame; each step once at the first boundary
    // >= its frame; two steps due at the same boundary both go, in order; the first
    // submission's boundary and pad reading are remembered.
    std::vector<std::string> submitted;
    auto submit = [&](const DisplaySequence::Step& s) { submitted.push_back(s.mode); return uint64_t(submitted.size()); };
    DisplaySequence sched("10:borderless,20:scale2,20:vsync,30:windowed");
    CHECK(sched.OnBoundary(9, 100, submit) == 0 && submitted.empty(), "nothing before frame 10");
    CHECK(sched.FirstFrame() == 0 && sched.Submitted() == 0, "nothing submitted yet");
    CHECK(sched.OnBoundary(10, 100, submit) == 1 && submitted.size() == 1 && submitted[0] == "borderless", "borderless at 10");
    CHECK(sched.FirstFrame() == 10 && sched.PaddedAtFirst() == 100, "first submission remembered (%llu, %llu)",
          (unsigned long long)sched.FirstFrame(), (unsigned long long)sched.PaddedAtFirst());
    CHECK(sched.OnBoundary(11, 120, submit) == 0 && submitted.size() == 1, "borderless once only");
    CHECK(sched.OnBoundary(25, 120, submit) == 2 && submitted.size() == 3 && submitted[1] == "scale2" && submitted[2] == "vsync",
          "the two steps due at 20 both go at the first boundary >= 20 (25), in order");
    CHECK(sched.OnBoundary(30, 120, submit) == 1 && submitted.back() == "windowed", "windowed at 30");
    CHECK(sched.OnBoundary(31, 120, submit) == 0 && sched.Submitted() == 4, "all four submitted once");
    CHECK(sched.FirstFrame() == 10 && sched.PaddedAtFirst() == 100, "the first submission's readings keep");
    const std::string census = sched.Census(100, 31000);
    CHECK(census == "steps=4,presenter_steps=3,submitted=4,skipped=0,first_frame=10,padded_at_first=100,padded_after=0,work_max_ms_after=31,"
                    "swap_padded=0,swap_closed=0,swap_open=3",
          "census: %s", census.c_str());
    const std::string padded = sched.Census(43780, 920142);
    CHECK(padded.find("padded_after=43680") != std::string::npos && padded.find("work_max_ms_after=920") != std::string::npos,
          "the s8 red reads in the census: %s", padded.c_str());

    // The swap windows: a presenter step's window opens at its submission, closes
    // kSettleBoundaries after the first boundary that saw the request finished;
    // the ring's pad growth over it is swap_padded. A scale step has no window; a
    // refused submission (id 0) has none; a pad outside every window is not counted.
    std::vector<uint64_t> done;
    auto finished = [&](uint64_t id) { for (uint64_t d : done) if (d == id) return true; return false; };
    auto submit_ids = [&](const DisplaySequence::Step& s) { return s.mode == "vsync" ? uint64_t(0) : uint64_t(s.frame); };
    DisplaySequence win("10:borderless,20:scale2,30:vsync,40:windowed");
    win.OnBoundary(10, 100, submit_ids, finished);   // borderless submitted, id 10, pad 100
    CHECK(win.SwapOpen() == 1 && win.SwapClosed() == 0, "one window open after the first submission (%d/%d)", win.SwapOpen(), win.SwapClosed());
    win.OnBoundary(15, 500, submit_ids, finished);   // in flight: pads grow
    done.push_back(10);
    win.OnBoundary(20, 900, submit_ids, finished);   // finished seen at 20 -> settles until 26; scale2 submitted (no window)
    CHECK(win.SwapOpen() == 1 && win.SwapClosed() == 0, "settling keeps the window open (%d/%d)", win.SwapOpen(), win.SwapClosed());
    win.OnBoundary(25, 950, submit_ids, finished);
    CHECK(win.SwapClosed() == 0, "not yet closed at 25");
    win.OnBoundary(26, 1000, submit_ids, finished);
    CHECK(win.SwapClosed() == 1 && win.SwapOpen() == 0 && win.SwapPadded() == 900,
          "closed at 26 with the pad growth 100 -> 1000 (%d/%d/%llu)", win.SwapClosed(), win.SwapOpen(), (unsigned long long)win.SwapPadded());
    win.OnBoundary(30, 1000, submit_ids, finished);   // vsync REFUSED (id 0): no window
    CHECK(win.SwapOpen() == 0, "a refused submission opens no window");
    win.OnBoundary(40, 1000, submit_ids, finished);   // windowed, id 40
    win.OnBoundary(45, 1000, submit_ids, finished);   // a pad-free swap
    done.push_back(40);
    win.OnBoundary(50, 1000, submit_ids, finished);
    win.OnBoundary(56, 1000, submit_ids, finished);
    CHECK(win.SwapClosed() == 2 && win.SwapPadded() == 900, "the pad-free window adds nothing (%d/%llu)", win.SwapClosed(), (unsigned long long)win.SwapPadded());
    win.OnBoundary(80, 5000, submit_ids, finished);   // a pad of the title's own late tick, outside every window
    CHECK(win.SwapPadded() == 900 && win.Census(5000, 0).find("swap_padded=900,swap_closed=2,swap_open=0") != std::string::npos,
          "outside-window pads are not the swap's: %s", win.Census(5000, 0).c_str());
    // A window never reported finished stays open (the census shows it).
    DisplaySequence open("10:fullscreen");
    open.OnBoundary(10, 0, submit_ids, [](uint64_t) { return false; });
    open.OnBoundary(500, 0, submit_ids, [](uint64_t) { return false; });
    CHECK(open.SwapOpen() == 1 && open.SwapClosed() == 0, "an unreported swap stays open");

    std::printf("%s: %d checks, %d failures\n", g_fails ? "FAIL" : "PASS", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
