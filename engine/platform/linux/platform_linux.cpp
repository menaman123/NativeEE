// platform/linux/platform_linux.cpp
#include "engine/platform/linux/platform_linux.h"
#include "engine/platform/posix/fiber_posix.h"

#include <pthread.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

namespace ps2r::platform {

namespace {

using ThreadFn = void (*)(void*);
struct ThreadStart {
    ThreadFn   fn;
    void*      arg;
    ThreadDesc desc;
};

// pthread_setname_np(3) on glibc refuses names past 15 characters + NUL
// (ERANGE) instead of truncating like Darwin: truncate here so the same
// ThreadDesc names work on both targets.
void SetThreadName(const char* name) {
    if (!name || !*name) return;
    char buf[16];
    std::strncpy(buf, name, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    pthread_setname_np(pthread_self(), buf);
}

void* ThreadTrampoline(void* p) {
    ThreadStart* s = static_cast<ThreadStart*>(p);
    if (s->desc.name && *s->desc.name) SetThreadName(s->desc.name);
    if (s->desc.interactive_qos) PlatformLinux::PinCurrentThreadInteractive(nullptr);
    ThreadFn fn = s->fn;
    void* arg = s->arg;
    delete s;
    fn(arg);
    return nullptr;
}

uint64_t NowNs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

Thread ThreadCreate(void (*fn)(void*), void* arg, const ThreadDesc& desc) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (desc.stack) pthread_attr_setstacksize(&attr, desc.stack);
    pthread_t tid{};
    ThreadStart* s = new ThreadStart{fn, arg, desc};
    Thread t;
    if (pthread_create(&tid, &attr, &ThreadTrampoline, s) == 0) {
        t.handle = reinterpret_cast<void*>(tid);
    } else {
        delete s;
    }
    pthread_attr_destroy(&attr);
    return t;
}

void ThreadJoin(Thread t) {
    if (t.handle) pthread_join(reinterpret_cast<pthread_t>(t.handle), nullptr);
}

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
    p.fiber_create = &FiberPosix::Create;
    p.fiber_switch = &FiberPosix::Switch;
    p.fiber_current = &FiberPosix::Current;
    p.fiber_destroy = &FiberPosix::Destroy;
    p.now_ns = &NowNs;
    p.timer_create = &UnimplementedTimerCreate;
    p.cpu_relax = &CpuRelax;
    p.map_reserve = &UnimplementedMapReserve;
    p.map_protect = &UnimplementedMapProtect;
    p.page_size = (size_t)sysconf(_SC_PAGESIZE);
    p.caps.name = "linux";
    p.caps.memory_budget_mb = 2048;
    p.caps.gles_level = 3;
    p.caps.has_fibers = true;    // ucontext (posix/fiber_posix); the IOP kernel's coroutines
    p.caps.has_mmap = false;
    p.caps.has_window = false;   // sdl/
    p.caps.has_audio = false;    // sdl/
    return p;
}

}  // namespace

const Platform& PlatformLinux::Instance() {
    static const Platform p = Build();
    return p;
}

void PlatformLinux::RegisterSelf() { Register(&Instance()); }

void PlatformLinux::PinCurrentThreadInteractive(const char* name, ThreadRole role) {
    static thread_local bool pinned = false;
    SetThreadName(name);
    if (pinned) return;
    pinned = true;
    (void)role;   // no ranks here (below); the roles are Win32's
    // No QoS class on Linux: CFS has no performance/efficiency-core placement
    // to pin against, and raising a thread's priority needs CAP_SYS_NICE. The
    // scheduling intent (every emulation thread in the same class as main)
    // already holds under the default policy.
}

uint64_t PlatformLinux::PhysFootprintBytes() {
    std::FILE* f = std::fopen("/proc/self/statm", "r");
    if (!f) return 0;
    unsigned long size = 0, resident = 0;
    const int n = std::fscanf(f, "%lu %lu", &size, &resident);
    std::fclose(f);
    if (n != 2) return 0;
    return static_cast<uint64_t>(resident) * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
}

std::string PlatformLinux::ExecutablePath(std::string* err) {
    std::error_code ec;
    const std::filesystem::path target = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        if (err) *err = "/proc/self/exe: " + ec.message();
        return std::string();
    }
    return target.string();
}

}  // namespace ps2r::platform
