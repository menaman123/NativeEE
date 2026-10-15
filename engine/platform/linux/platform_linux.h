// platform/linux/platform_linux.h -- the Linux Platform (glibc, x86-64 and
// aarch64): steady-clock time, pthreads with names (no QoS class exists; CFS
// schedules the emulation threads evenly and the interactive pin is a name
// only), stdio files, page size, ucontext fibers (posix/), the /proc footprint
// and executable path. Reached through engine/platform/host/platform.h only;
// surface/input/audio arrive with the SDL folder.
#pragma once

#include <cstdint>
#include <string>

#include "engine/platform/platform.h"

namespace ps2r::platform {

class PlatformLinux {
public:
    // The filled-in Platform instance (static storage).
    static const Platform& Instance();
    // Instance() + Register().
    static void RegisterSelf();

    // Name the calling thread (15 characters survive) -- the Linux half of the
    // macOS interactive-QoS pin. Idempotent, first call wins per thread.
    static void PinCurrentThreadInteractive(const char* name);

    // Resident set in bytes from /proc/self/statm; 0 when unreadable.
    static uint64_t PhysFootprintBytes();
    // readlink(/proc/self/exe); empty + *err on failure.
    static std::string ExecutablePath(std::string* err);
};

}  // namespace ps2r::platform
