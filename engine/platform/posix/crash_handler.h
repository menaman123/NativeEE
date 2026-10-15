// platform/posix/crash_handler.h -- fatal-signal handler (macOS + glibc Linux): on SIGSEGV / SIGBUS
// / SIGILL / SIGFPE / SIGABRT it writes the ring-log snapshot through the
// signal-safe RingLog dump, prints a backtrace, restores the default action
// and re-raises so the process still dies with the original signal (core
// dumps and exit status intact). This is the crash-class triage trigger.
#pragma once

#include <cstddef>

namespace ps2r::platform {

class CrashHandler {
public:
    // Install for the fatal signal set. `ring_dump_path` (may be null) is the
    // .rlog the ring writes on a crash. Idempotent.
    static void Install(const char* ring_dump_path);
    static bool Installed();
    // Signal-safe: writes the current backtrace to fd 2.
    static void WriteBacktrace(int fd, int skip_frames);

private:
    static void OnSignal(int sig);
};

}  // namespace ps2r::platform
