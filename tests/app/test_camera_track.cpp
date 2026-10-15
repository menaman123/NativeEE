// tests/app/test_camera_track.cpp -- the presenter's camera transform source
// (Charter 2 A3): the camera position is the world->camera matrix's inverse
// translation; a sample is pending until its boundary promotes it; a present
// between two boundary-stamped samples interpolates at its wall phase; a present
// in the same tick as the previous one is a duplicate; the trace holds one line
// per present since the first sample.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

#include "app/host/camera_track.h"

namespace {
int g_failures = 0;
#define CHECK(cond, what)                                                   \
    do {                                                                    \
        if (cond) std::printf("  PASS %s\n", what);                         \
        else { std::printf("  FAIL %s\n", what); ++g_failures; }            \
    } while (0)

// A world->camera matrix (row vectors) for a camera at `p` looking down the
// title's rows: identity rotation, translation row 3 = -p.
void LookFrom(float px, float py, float pz, float* m) {
    for (int i = 0; i < 16; ++i) m[i] = 0;
    m[0] = m[5] = m[10] = m[15] = 1;
    m[12] = -px; m[13] = -py; m[14] = -pz;
}
bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
}  // namespace

int main() {
    using ps2r::app::CameraTrack;
    using namespace std::chrono;
    float m[16], pos[3];
    // A rotated camera: rows = a 90-degree yaw; the inverse translation is -t * R^-1.
    for (int i = 0; i < 16; ++i) m[i] = 0;
    m[2] = 1; m[5] = 1; m[8] = -1; m[15] = 1;   // x' = -z, z' = x
    m[12] = 3; m[13] = -2; m[14] = 5;
    CHECK(CameraTrack::CameraPosition(m, pos) && Near(pos[0], -5) && Near(pos[1], 2) && Near(pos[2], 3),
          "camera position = -t * R^-1 for a yawed matrix (-5, 2, 3)");
    for (int i = 0; i < 16; ++i) m[i] = 0;
    CHECK(!CameraTrack::CameraPosition(m, pos), "a singular matrix yields no position");

    CameraTrack track;
    const std::string trace = "test_camera_track.trace";
    track.SetTrace(trace);
    const auto t0 = CameraTrack::Clock::time_point(seconds(100));
    CameraTrack::Presented p = track.Present(t0);
    CHECK(!p.valid && track.Presents() == 1, "a present before any sample is invalid (unsampled)");
    LookFrom(0, 0, 0, m);
    track.NoteCamera(m);
    p = track.Present(t0 + milliseconds(1));
    CHECK(!p.valid, "a PENDING sample (no boundary yet) is not presented");
    track.NoteBoundaryAt(t0);                       // tick 1 at t0: camera (0,0,0)
    LookFrom(1, 0, 0, m);
    track.NoteCamera(m);
    track.NoteBoundaryAt(t0 + microseconds(16667));  // tick 2: camera (1,0,0)
    p = track.Present(t0 + microseconds(16667 + 8333));   // half way to the next tick
    CHECK(p.valid && p.interpolated && Near(p.position[0], 0.5f) && Near(p.latest[0], 1.0f) && !p.duplicate,
          "a present half a tick after tick 2 shows the camera half way from tick 1 to tick 2 (0.5)");
    p = track.Present(t0 + microseconds(16667 + 12500));
    CHECK(p.valid && p.duplicate && Near(p.position[0], 0.75f), "the next present in the same tick is a duplicate at 0.75");
    p = track.Present(t0 + microseconds(16667 * 3));
    CHECK(p.valid && !p.interpolated && Near(p.position[0], 1.0f) && p.phase == 1.0, "past the next edge the phase clamps to 1 (the latest tick's camera)");
    LookFrom(2, 0, 0, m);
    track.NoteCamera(m);
    track.NoteBoundaryAt(t0 + microseconds(16667 * 2));   // tick 3: (2,0,0)
    p = track.Present(t0 + microseconds(16667 * 2 + 4167));
    CHECK(p.valid && !p.duplicate && Near(p.position[0], 1.25f), "a new tick: the first present interpolates 1 -> 2 at a quarter (1.25)");
    track.NoteBoundaryAt(t0 + microseconds(16667 * 3));   // a boundary without a camera sample changes nothing
    CHECK(track.Samples() == 3, "a boundary with no pending sample promotes nothing (3 samples)");
    const std::string census = track.Census();
    CHECK(census.find("samples=3,presents=6,interpolated=3,duplicates=2,unsampled=2") == 0, ("census " + census).c_str());
    std::ifstream in(trace);
    std::string line;
    int lines = 0;
    while (std::getline(in, line)) if (!line.empty() && line[0] != '#') ++lines;
    CHECK(lines == 4, "the trace holds one line per present since the first promoted sample (4)");
    std::remove(trace.c_str());
    std::printf("CameraTrack: %s\n", g_failures ? "FAIL" : "PASS");
    return g_failures ? 1 : 0;
}
