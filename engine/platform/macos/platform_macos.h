// platform/macos/platform_macos.h -- the macOS Platform: steady-clock time,
// pthreads with names and USER_INTERACTIVE QoS for the emulation threads
// (always on -- the opt-out was a development A/B), stdio files, page size,
// ucontext fibers (posix/), the mach footprint and the dyld executable path.
// Reached through engine/platform/host/platform.h only; surface/input/audio
// arrive with the SDL folder.
#pragma once

#include <cstdint>
#include <string>

#include "engine/platform/platform.h"

namespace ps2r::platform {

class PlatformMacos {
public:
    // The filled-in Platform instance (static storage).
    static const Platform& Instance();
    // Instance() + Register().
    static void RegisterSelf();

    // Pin the calling thread to USER_INTERACTIVE QoS; idempotent, first call
    // wins per thread. Also names the thread (16 chars max on Darwin).
    static void PinCurrentThreadInteractive(const char* name);

    // task_info(TASK_VM_INFO).phys_footprint; 0 when the query fails.
    static uint64_t PhysFootprintBytes();
    // _NSGetExecutablePath, canonicalised; empty + *err on failure.
    static std::string ExecutablePath(std::string* err);
};

}  // namespace ps2r::platform
