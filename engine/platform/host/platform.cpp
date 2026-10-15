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
void PinCurrentThreadInteractive(const char* name) { Target::PinCurrentThreadInteractive(name); }
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
