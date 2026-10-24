// platform/host/platform.cpp -- the host:: entries forwarded to the target
// folder this build is for. The ONLY place that picks a target by OS.
#include "engine/platform/host/platform.h"

#include <chrono>

#if PS2R_HOST_MACOS
#include "engine/platform/macos/platform_macos.h"
namespace ps2r::platform::host {
namespace { using Target = PlatformMacos; }
}  // namespace ps2r::platform::host
#elif PS2R_HOST_LINUX
#include "engine/platform/linux/platform_linux.h"
namespace ps2r::platform::host {
namespace { using Target = PlatformLinux; }
}  // namespace ps2r::platform::host
#elif PS2R_HOST_WINDOWS
#include "engine/platform/windows/platform_windows.h"
namespace ps2r::platform::host {
namespace { using Target = PlatformWindows; }
}  // namespace ps2r::platform::host
#endif

namespace ps2r::platform::host {

const Platform& Instance() { return Target::Instance(); }
void RegisterSelf() { Target::RegisterSelf(); }
const char* Name() { return Target::Instance().caps.name; }
void PinCurrentThreadInteractive(const char* name, ThreadRole role) { Target::PinCurrentThreadInteractive(name, role); }
void RaiseTimerResolution() { ps2r::host::RaiseTimerResolution(); }

void SleepUntil(std::chrono::steady_clock::time_point deadline) {
    // The arm sleeps a relative span; the loop covers an early wake (a coarse
    // timer rounds down, a signal interrupts) and never returns before the deadline.
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return;
        ps2r::host::SleepForNs(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now).count()));
    }
}

const CpuFeatures& Cpu() {
    static const CpuFeatures f = [] {
        CpuFeatures c;
        ps2r::host::CpuDetect(&c.sse4_2, &c.avx2, &c.fma3, &c.neon, &c.arch);
        return c;
    }();
    return f;
}

std::string CpuFeaturesCensus() {
    const CpuFeatures& c = Cpu();
    std::string s = "arch=";
    s += c.arch;
    s += ",sse4_2="; s += c.sse4_2 ? '1' : '0';
    s += ",avx2=";   s += c.avx2 ? '1' : '0';
    s += ",fma3=";   s += c.fma3 ? '1' : '0';
    s += ",neon=";   s += c.neon ? '1' : '0';
    return s;
}
uint64_t PhysFootprintBytes() { return Target::PhysFootprintBytes(); }
std::string ExecutablePath(std::string* err) { return Target::ExecutablePath(err); }

uint64_t NowNs() {
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

void WriteBacktrace(int fd, int skip_frames) {
    // Through the host arm (execinfo on POSIX, CaptureStackBackTrace + DbgHelp on
    // Windows): this TU names no OS header.
    void* frames[64];
    const int n = ps2r::host::Backtrace(frames, 64);
    if (n > skip_frames) ps2r::host::BacktraceSymbolsFd(frames + skip_frames, n - skip_frames, fd);
}

}  // namespace ps2r::platform::host
