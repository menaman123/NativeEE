// platform/platform.h -- the ONE platform interface (ARCHITECTURE section 16).
// A capability-honest struct of function pointers plus PlatformCaps; no
// #ifdef exists outside engine/platform/. Each target folder fills one
// instance and registers it. Entry points a target cannot provide point at
// the shared `unimplemented` stubs, which report and return failure -- never
// crash silently.
//
// Phase 1 implements now_ns, thread_create (name + QoS), cpu_relax and open;
// surface / input / audio / fiber / timer / map entries are stubs until the
// subsystems that need them land (Phases 3, 6, 7).
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace ps2r::platform {

struct VideoConfig { int width = 0, height = 0; bool headless = true; const char* title = ""; };
struct Surface     { void* handle = nullptr; };
struct InputState  { uint16_t wire_buttons = 0xFFFF; uint8_t lx = 0x80, ly = 0x80, rx = 0x80, ry = 0x80; };
struct AudioSpec   { int sample_rate = 48000; int channels = 2; int frames = 800; };
struct AudioSink   { void* handle = nullptr; };
struct File        { std::FILE* fp = nullptr; };
enum class FileMode { Read, Write, Append };
struct ThreadDesc  { const char* name = ""; bool interactive_qos = false; size_t stack = 0; };
struct Thread      { void* handle = nullptr; };
struct Fiber       { void* handle = nullptr; };
struct Timer       { void* handle = nullptr; };
enum class MapFlags { None };
enum class Prot { None, Read, ReadWrite, ReadExec };

struct PlatformCaps {
    const char* name = "";
    size_t      memory_budget_mb = 0;
    int         gles_level = 0;
    bool        has_fibers = false;
    bool        has_mmap = false;
    bool        has_window = false;
    bool        has_audio = false;
};

struct Platform {
    // window/surface
    Surface   (*create_surface)(const VideoConfig&) = nullptr;
    // input
    void      (*poll_input)(InputState&) = nullptr;
    // audio out
    AudioSink (*audio_open)(const AudioSpec&) = nullptr;
    // file IO
    File      (*open)(const char* path, FileMode) = nullptr;
    // threads / coroutines
    Thread    (*thread_create)(void (*fn)(void*), void*, const ThreadDesc&) = nullptr;
    void      (*thread_join)(Thread) = nullptr;
    Fiber     (*fiber_create)(void (*fn)(void*), void*, size_t stack) = nullptr;
    void      (*fiber_switch)(Fiber) = nullptr;
    Fiber     (*fiber_current)() = nullptr;   // the calling context's own handle (switch-back target)
    void      (*fiber_destroy)(Fiber) = nullptr;
    // timers: the single TimeDomain source
    uint64_t  (*now_ns)() = nullptr;
    Timer     (*timer_create)(uint64_t period_ns, void (*cb)(void*), void*) = nullptr;
    void      (*cpu_relax)() = nullptr;
    // memory mapping
    void*     (*map_reserve)(size_t bytes, MapFlags) = nullptr;
    bool      (*map_protect)(void* p, size_t bytes, Prot) = nullptr;
    size_t    page_size = 0;
    PlatformCaps caps;
};

// Shared stubs a target uses for entry points it does not provide: each
// prints "platform: <entry> unimplemented on <name>" once and fails.
Surface   UnimplementedSurface(const VideoConfig&);
void      UnimplementedPollInput(InputState&);
AudioSink UnimplementedAudioOpen(const AudioSpec&);
Fiber     UnimplementedFiberCreate(void (*)(void*), void*, size_t);
void      UnimplementedFiberSwitch(Fiber);
Fiber     UnimplementedFiberCurrent();
void      UnimplementedFiberDestroy(Fiber);
Timer     UnimplementedTimerCreate(uint64_t, void (*)(void*), void*);
void*     UnimplementedMapReserve(size_t, MapFlags);
bool      UnimplementedMapProtect(void*, size_t, Prot);

// The registered platform (set once by the composition root).
void Register(const Platform* p);
const Platform& Current();
bool Registered();

}  // namespace ps2r::platform
