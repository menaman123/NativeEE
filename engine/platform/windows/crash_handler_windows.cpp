// platform/windows/crash_handler_windows.cpp -- the SEH arm of engine/platform/crash_handler.h.
// Windows has no signals for faults: an access violation, an illegal instruction, an FP or
// integer fault or a stack overflow is a structured exception that reaches the process's
// unhandled-exception filter when nothing else catches it. The filter does what the POSIX
// handler does -- a header line, the backtrace (host::Backtrace: CaptureStackBackTrace +
// DbgHelp symbolisation), the ring-log crash snapshot -- and then CONTINUE_SEARCH so the
// default action follows (Windows Error Reporting, the debugger, the 0xC000.... exit status).
// abort() is not an exception: the CRT's SIGABRT handler is installed for it like POSIX's.
#include "engine/platform/crash_handler.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <csignal>
#include <cstdio>
#include <cstring>

#include "engine/core/log/ring_log.h"
#include "engine/platform/host/platform.h"

namespace ps2r::platform {

namespace {
bool g_installed = false;
char g_ring_path[512] = {0};
LPTOP_LEVEL_EXCEPTION_FILTER g_previous = nullptr;

void WriteStr(int fd, const char* s) { ps2r::host::FdWrite(fd, s, std::strlen(s)); }

const char* ExceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INVALID_OPERATION: return "FLT_INVALID_OPERATION";
        case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
        case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
        default: return "?";
    }
}

void Report(const char* what) {
    WriteStr(2, "\n=== fatal exception ");
    WriteStr(2, what);
    WriteStr(2, " ===\n");
    CrashHandler::WriteBacktrace(2, 2);
    if (g_ring_path[0]) ringlog::RingLog::DumpAllToPath(g_ring_path, ringlog::DUMP_CRASH);
}

LONG WINAPI OnException(EXCEPTION_POINTERS* info) {
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_ILLEGAL_INSTRUCTION: case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_FLT_DIVIDE_BY_ZERO: case EXCEPTION_FLT_INVALID_OPERATION:
        case EXCEPTION_IN_PAGE_ERROR: case EXCEPTION_PRIV_INSTRUCTION: case EXCEPTION_DATATYPE_MISALIGNMENT: {
            char line[96];
            std::snprintf(line, sizeof line, "%s (0x%08lX)", ExceptionName(code), static_cast<unsigned long>(code));
            Report(line);
            break;
        }
        default:
            break;   // C++ exceptions, breakpoints, debugger notifications: not ours
    }
    return g_previous ? g_previous(info) : EXCEPTION_CONTINUE_SEARCH;
}
}  // namespace

void CrashHandler::WriteBacktrace(int fd, int skip_frames) {
    void* frames[64];
    const int n = ps2r::host::Backtrace(frames, 64);
    if (n > skip_frames) ps2r::host::BacktraceSymbolsFd(frames + skip_frames, n - skip_frames, fd);
}

void CrashHandler::OnSignal(int sig) {
    Report(sig == SIGABRT ? "SIGABRT" : sig == SIGSEGV ? "SIGSEGV" : sig == SIGILL ? "SIGILL" : sig == SIGFPE ? "SIGFPE" : "?");
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

void CrashHandler::Install(const char* ring_dump_path) {
    if (ring_dump_path) {
        std::strncpy(g_ring_path, ring_dump_path, sizeof(g_ring_path) - 1);
        g_ring_path[sizeof(g_ring_path) - 1] = 0;
    }
    if (g_installed) return;
    g_installed = true;
    g_previous = SetUnhandledExceptionFilter(&OnException);
    // abort() goes through the CRT, not SEH: no message box / WER dialog in the harness, our
    // report instead (then the default action).
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, &CrashHandler::OnSignal);
    // The CRT maps a handful of faults to signals only when a handler is installed; the SEH
    // filter above is the primary route, these are the belt.
    std::signal(SIGSEGV, &CrashHandler::OnSignal);
    std::signal(SIGILL, &CrashHandler::OnSignal);
    std::signal(SIGFPE, &CrashHandler::OnSignal);
}

bool CrashHandler::Installed() { return g_installed; }

}  // namespace ps2r::platform
