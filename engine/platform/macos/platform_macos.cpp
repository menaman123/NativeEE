// platform/macos/platform_macos.cpp
#include "engine/platform/macos/platform_macos.h"
#include "engine/platform/posix/fiber_posix.h"

#include <mach-o/dyld.h>
#include <mach/mach.h>
#include <pthread.h>
#include <unistd.h>

#include <chrono>
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

void* ThreadTrampoline(void* p) {
    ThreadStart* s = static_cast<ThreadStart*>(p);
    if (s->desc.name && *s->desc.name) pthread_setname_np(s->desc.name);
    if (s->desc.interactive_qos) PlatformMacos::PinCurrentThreadInteractive(nullptr);
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
    p.caps.name = "macos";
    p.caps.memory_budget_mb = 2048;
    p.caps.gles_level = 3;
    p.caps.has_fibers = true;    // ucontext (posix/fiber_posix); the IOP kernel's coroutines
    p.caps.has_mmap = false;     // Phase 3
    p.caps.has_window = false;   // Phase 6 (sdl/)
    p.caps.has_audio = false;    // Phase 7 (sdl/)
    return p;
}

}  // namespace

const Platform& PlatformMacos::Instance() {
    static const Platform p = Build();
    return p;
}

void PlatformMacos::RegisterSelf() { Register(&Instance()); }

void PlatformMacos::PinCurrentThreadInteractive(const char* name, ThreadRole role) {
    static thread_local bool pinned = false;
    if (name && *name) pthread_setname_np(name);
    if (pinned) return;
    pinned = true;
    (void)role;   // every role is the USER_INTERACTIVE class here; the ranks are Win32's
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
}

uint64_t PlatformMacos::PhysFootprintBytes() {
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
        return 0;
    return static_cast<uint64_t>(info.phys_footprint);
}

std::string PlatformMacos::ExecutablePath(std::string* err) {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size + 1, '\0');
    if (size == 0 || _NSGetExecutablePath(buffer.data(), &size) != 0) {
        if (err) *err = "_NSGetExecutablePath failed";
        return std::string();
    }
    buffer.resize(std::strlen(buffer.c_str()));
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::canonical(buffer, ec);
    return ec ? buffer : canonical.string();
}

}  // namespace ps2r::platform
