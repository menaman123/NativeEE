// platform/windows/platform_windows.cpp -- see platform_windows.h. Compiled on _WIN32
// only (engine/platform/CMakeLists.txt); every OS call goes through host/platform.h's
// Windows arm so this file names no Win32 API itself.
#include "engine/platform/windows/platform_windows.h"

#include <cstdio>
#include <thread>

#include "engine/platform/host/platform.h"

namespace ps2r::platform {
namespace {

// Inside ps2r::platform a bare `host::` names ps2r::platform::host (the target-folder API
// this file implements), not the shim arm; the arm is spelled in full.
namespace arm = ::ps2r::host;

using StartFn = void (*)(void*);
struct ThreadStart { StartFn fn; void* arg; ThreadDesc desc; };

void ThreadTrampoline(void* p) {
    ThreadStart s = *static_cast<ThreadStart*>(p);
    delete static_cast<ThreadStart*>(p);
    if (s.desc.name && *s.desc.name) arm::SetCurrentThreadName(s.desc.name);
    if (s.desc.interactive_qos) arm::PinCurrentThreadInteractive();
    s.fn(s.arg);
}

Thread ThreadCreate(void (*fn)(void*), void* arg, const ThreadDesc& desc) {
    auto* s = new ThreadStart{fn, arg, desc};
    arm::NativeThread t;
    Thread out;
    if (arm::ThreadCreate(&t, &ThreadTrampoline, s, desc.stack)) out.handle = t.handle;
    else delete s;
    return out;
}
void ThreadJoin(Thread t) { arm::ThreadJoin(arm::NativeThread{t.handle}); }

Fiber FiberCreate(void (*fn)(void*), void* arg, size_t stack) {
    arm::Coroutine c;
    Fiber f;
    if (arm::CoroutineCreate(&c, fn, arg, stack)) f.handle = c.handle;
    return f;
}
void FiberSwitch(Fiber to) { arm::CoroutineSwitch(arm::Coroutine{to.handle}); }
Fiber FiberCurrent() { return Fiber{arm::CoroutineCurrent().handle}; }
void FiberDestroy(Fiber f) { arm::CoroutineDestroy(arm::Coroutine{f.handle}); }

File Open(const char* path, FileMode mode) {
    const char* m = mode == FileMode::Read ? "rb" : mode == FileMode::Write ? "wb" : "ab";
    File f;
    f.fp = std::fopen(path, m);
    return f;
}

void CpuRelax() { std::this_thread::yield(); }

Platform Build() {
    Platform p;
    p.create_surface = &UnimplementedSurface;
    p.poll_input = &UnimplementedPollInput;
    p.audio_open = &UnimplementedAudioOpen;
    p.open = &Open;
    p.thread_create = &ThreadCreate;
    p.thread_join = &ThreadJoin;
    p.fiber_create = &FiberCreate;
    p.fiber_switch = &FiberSwitch;
    p.fiber_current = &FiberCurrent;
    p.fiber_destroy = &FiberDestroy;
    p.now_ns = &arm::MonotonicNowNs;
    p.timer_create = &UnimplementedTimerCreate;
    p.cpu_relax = &CpuRelax;
    p.map_reserve = &UnimplementedMapReserve;
    p.map_protect = &UnimplementedMapProtect;
    p.page_size = 4096;   // the x64 / arm64 Windows page size (dwPageSize)
    p.caps.name = "windows";
    p.caps.memory_budget_mb = 2048;
    p.caps.gles_level = 3;
    p.caps.has_fibers = true;    // native fibers (host_windows.cpp)
    p.caps.has_mmap = false;
    p.caps.has_window = false;   // platform/sdl
    p.caps.has_audio = false;    // platform/sdl
    return p;
}

}  // namespace

const Platform& PlatformWindows::Instance() {
    static const Platform p = Build();
    return p;
}

void PlatformWindows::RegisterSelf() {
    Register(&Instance());
    arm::RequestFineTimerResolution();   // 1 ms scheduler granularity for the process (host_windows.cpp)
}

void PlatformWindows::PinCurrentThreadInteractive(const char* name, ThreadRole role) {
    static thread_local bool pinned = false;
    if (name && *name) arm::SetCurrentThreadName(name);
    if (pinned) return;
    pinned = true;
    arm::PinCurrentThreadInteractive(role);
}

uint64_t PlatformWindows::PhysFootprintBytes() { return arm::ProcessFootprintBytes(); }

std::string PlatformWindows::ExecutablePath(std::string* err) { return arm::ExecutableImagePath(err); }

}  // namespace ps2r::platform
