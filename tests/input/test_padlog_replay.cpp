// tests/input/test_padlog_replay.cpp -- ctest PadlogReplay. A synthetic padlog
// spec is turned, frame by frame, into the NormalizedInput the mapper would
// need (buttons = wire bits active-high, axis byte b -> (b - 127.5) / 127.5),
// run through InputMapper::Map -> PadSample -> ToInputState (wire) and fed to
// the triage InputLog recorder; after Flush the recorded tokens must be
// byte-identical to the spec's tokens. This proves the host half produces
// exactly what the pad protocol will stamp, without the pad protocol.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/core/triage/input_log.h"
#include "engine/platform/sdl/input/input_backend.h"
#include "engine/platform/sdl/input/input_mapper.h"
#include "engine/platform/sdl/input/pad_sample.h"

using namespace ps2r::platform::sdl;
using ps2r::triage::InputLog;

static int g_fails = 0;

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            g_fails++;                                                      \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);   \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
        }                                                                   \
    } while (0)

// The state the spec prescribes for frame f: active-high button mask and the
// four axis bytes (the LAST matching axis segment wins, as the replayer does).
static void SpecStateAt(const InputLog& script, uint32_t f, uint16_t& buttons, uint8_t axes[4]) {
    buttons = 0;
    for (int a = 0; a < 4; a++) axes[a] = 0x80;
    for (const InputLog::Press& p : script.Presses())
        if (f >= p.f1 && f <= p.f2) buttons |= p.mask;
    for (const InputLog::Axis& a : script.Axes())
        if (f >= a.f1 && f <= a.f2) axes[a.axis] = a.value;
}

static float AxisByteToFloat(uint8_t b) { return ((float)b - 127.5f) / 127.5f; }

static std::vector<std::string> SplitTokens(const std::string& spec) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < spec.size()) {
        const size_t comma = spec.find(',', pos);
        out.push_back(spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

// Every pad byte survives the inverse-StickToByte -> StickToByte round trip.
static void TestAxisByteRoundTrip() {
    int bad = 0;
    for (int b = 0; b < 256; b++)
        if (StickToByte(AxisByteToFloat((uint8_t)b)) != (uint8_t)b) bad++;
    CHECK(bad == 0, "%d axis bytes do not round-trip", bad);
}

static void TestReplay(const std::string& spec, uint32_t frames) {
    InputLog script;
    const size_t accepted = script.ParseSpec(spec);
    const std::vector<std::string> want = SplitTokens(spec);
    CHECK(accepted == want.size(), "spec '%s': %zu tokens accepted of %zu", spec.c_str(), accepted, want.size());

    InputMapper mapper;
    InputLog recorder;
    std::vector<std::string> got;
    for (uint32_t f = 0; f < frames; f++) {
        uint16_t buttons = 0;
        uint8_t axes[4];
        SpecStateAt(script, f, buttons, axes);

        NormalizedInput in;   // zero-delta mouse, no mouse buttons
        in.buttons = buttons;   // LogicalButton bits ARE the wire bit positions
        in.lx = AxisByteToFloat(axes[0]);
        in.ly = AxisByteToFloat(axes[1]);
        in.rx = AxisByteToFloat(axes[2]);
        in.ry = AxisByteToFloat(axes[3]);

        const PadSample sample = mapper.Map(in, 0.f);
        const ps2r::platform::InputState wire = ToInputState(sample);

        // What the pad protocol serves: the wire word back to active-high.
        const uint16_t served_buttons = (uint16_t)~wire.wire_buttons;
        const uint8_t served_axes[4] = {wire.lx, wire.ly, wire.rx, wire.ry};
        CHECK(served_buttons == buttons, "frame %u buttons 0x%04x want 0x%04x", f, served_buttons, buttons);
        for (int a = 0; a < 4; a++)
            CHECK(served_axes[a] == axes[a], "frame %u axis %d 0x%02x want 0x%02x", f, a, served_axes[a], axes[a]);
        recorder.Feed(f, served_buttons, served_axes, &got);
    }
    recorder.Flush(frames - 1, &got);

    std::vector<std::string> want_sorted = want, got_sorted = got;
    std::sort(want_sorted.begin(), want_sorted.end());
    std::sort(got_sorted.begin(), got_sorted.end());
    CHECK(got_sorted == want_sorted, "spec '%s': recorded %zu tokens, want %zu", spec.c_str(), got.size(), want.size());
    for (size_t i = 0; i < want_sorted.size() && i < got_sorted.size(); i++)
        CHECK(got_sorted[i] == want_sorted[i], "token %zu: got '%s' want '%s'", i, got_sorted[i].c_str(),
              want_sorted[i].c_str());

    // The recording re-parses to the same script.
    InputLog again;
    std::string joined;
    for (const std::string& t : got) joined += (joined.empty() ? "" : ",") + t;
    CHECK(again.ParseSpec(joined) == accepted, "recording re-parses to %zu tokens", accepted);
    CHECK(again.Presses().size() == script.Presses().size() && again.Axes().size() == script.Axes().size(),
          "re-parsed press/axis counts match");
    std::printf("  spec   : %s\n  record : %s\n", spec.c_str(), joined.c_str());
}

int main() {
    TestAxisByteRoundTrip();
    // The PHASE7_PLAN section 4.5 spec plus a range that closes only at Flush
    // and a second axis segment on another stick.
    TestReplay("10-20:CROSS,15-30:LX=200,25-25:START,35-39:SQUARE,5-8:RY=0", 40);
    // Rails, a full wire word, adjacent segments on one axis.
    TestReplay("0-3:LY=255,4-7:LY=0,2-5:L2,2-5:R2,2-5:SELECT,2-5:L3,6-6:TRIANGLE", 12);
    if (g_fails) {
        std::printf("test_padlog_replay: FAIL (%d)\n", g_fails);
        return 1;
    }
    std::printf("test_padlog_replay: PASS (axis round trip, 2 synthetic padlogs byte-identical)\n");
    return 0;
}
