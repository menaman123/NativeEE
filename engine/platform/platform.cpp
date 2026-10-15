// platform/platform.cpp -- registry + shared unimplemented stubs.
#include "engine/platform/platform.h"

namespace ps2r::platform {

namespace {
const Platform* g_platform = nullptr;

void ReportOnce(const char* entry, bool* done) {
    if (*done) return;
    *done = true;
    std::fprintf(stderr, "platform: %s unimplemented on %s\n", entry, g_platform ? g_platform->caps.name : "?");
}
}  // namespace

Surface UnimplementedSurface(const VideoConfig&) {
    static bool done = false;
    ReportOnce("create_surface", &done);
    return Surface{};
}
void UnimplementedPollInput(InputState& s) {
    static bool done = false;
    ReportOnce("poll_input", &done);
    s = InputState{};
}
AudioSink UnimplementedAudioOpen(const AudioSpec&) {
    static bool done = false;
    ReportOnce("audio_open", &done);
    return AudioSink{};
}
Fiber UnimplementedFiberCreate(void (*)(void*), void*, size_t) {
    static bool done = false;
    ReportOnce("fiber_create", &done);
    return Fiber{};
}
void UnimplementedFiberSwitch(Fiber) {
    static bool done = false;
    ReportOnce("fiber_switch", &done);
}
Fiber UnimplementedFiberCurrent() {
    static bool done = false;
    ReportOnce("fiber_current", &done);
    return Fiber{};
}
void UnimplementedFiberDestroy(Fiber) {
    static bool done = false;
    ReportOnce("fiber_destroy", &done);
}
Timer UnimplementedTimerCreate(uint64_t, void (*)(void*), void*) {
    static bool done = false;
    ReportOnce("timer_create", &done);
    return Timer{};
}
void* UnimplementedMapReserve(size_t, MapFlags) {
    static bool done = false;
    ReportOnce("map_reserve", &done);
    return nullptr;
}
bool UnimplementedMapProtect(void*, size_t, Prot) {
    static bool done = false;
    ReportOnce("map_protect", &done);
    return false;
}

void Register(const Platform* p) { g_platform = p; }
const Platform& Current() {
    static const Platform empty{};
    return g_platform ? *g_platform : empty;
}
bool Registered() { return g_platform != nullptr; }

}  // namespace ps2r::platform
