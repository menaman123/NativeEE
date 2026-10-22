// engine/platform/crash_handler.h -- the fatal-fault handler, one class with a per-OS arm:
//   posix/crash_handler.cpp     SIGSEGV / SIGBUS / SIGILL / SIGFPE / SIGABRT via sigaction
//                               (macOS + glibc Linux): backtrace to fd 2, the ring-log crash
//                               snapshot, then the default action re-raised (core dump and exit
//                               status intact).
//   windows/crash_handler_windows.cpp
//                               the SEH counterpart: SetUnhandledExceptionFilter for
//                               ACCESS_VIOLATION / ILLEGAL_INSTRUCTION / the FP and integer faults /
//                               STACK_OVERFLOW (+ the CRT's abort() through _set_abort_behavior and
//                               a SIGABRT handler), backtrace via host::Backtrace (CaptureStackBackTrace
//                               + DbgHelp), the same ring-log snapshot, then EXCEPTION_CONTINUE_SEARCH
//                               so Windows Error Reporting / the debugger see the original fault.
// Callers (the title boot, the tests) include THIS header; the arm is CMake's choice.
#pragma once

#include <cstddef>

namespace ps2r::platform {

class CrashHandler {
public:
    // Install for the fatal fault set. `ring_dump_path` (may be null) is the .rlog the ring
    // writes on a crash. Idempotent; a later call only updates the path.
    static void Install(const char* ring_dump_path);
    static bool Installed();
    // Signal-safe where the OS has such a notion: writes the current backtrace to fd 2.
    static void WriteBacktrace(int fd, int skip_frames);

private:
    static void OnSignal(int sig);
};

}  // namespace ps2r::platform
