// platform/windows/platform_windows.h -- the Windows Platform (Track B2): the Platform
// struct filled over the host shim arm (engine/platform/host/host_windows.cpp): QPC
// time, _beginthreadex threads with the interactive pin, native fibers, stdio files,
// the system page size. Surface / input / audio arrive from platform/sdl exactly as on
// macOS; the crash handler is the SEH/vectored-handler counterpart of macos/crash_handler
// (not written yet -- feasibility note in README.md).
#pragma once

#include <cstdint>
#include <string>

#include "engine/platform/platform.h"

namespace ps2r::platform {

class PlatformWindows {
public:
    // The filled-in Platform instance (static storage).
    static const Platform& Instance();
    // Instance() + Register().
    static void RegisterSelf();

    // Name the calling thread and raise it to ABOVE_NORMAL (the Win32 counterpart of the
    // USER_INTERACTIVE QoS pin); idempotent, first call wins per thread.
    static void PinCurrentThreadInteractive(const char* name);
    // GetProcessMemoryInfo private usage (not phys_footprint: platform-keyed pins).
    static uint64_t PhysFootprintBytes();
    // GetModuleFileNameW of the main module, UTF-8; empty + *err on failure.
    static std::string ExecutablePath(std::string* err);
};

}  // namespace ps2r::platform
