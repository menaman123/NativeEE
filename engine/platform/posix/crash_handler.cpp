// platform/posix/crash_handler.cpp -- execinfo backtrace + sigaction; identical on Darwin and glibc.
#include "engine/platform/posix/crash_handler.h"

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

#include <cstring>

#include "engine/core/log/ring_log.h"

namespace ps2r::platform {

namespace {
bool g_installed = false;
char g_ring_path[512] = {0};
const int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

void WriteStr(int fd, const char* s) { (void)!write(fd, s, std::strlen(s)); }
}  // namespace

void CrashHandler::WriteBacktrace(int fd, int skip_frames) {
    void* frames[64];
    const int n = backtrace(frames, 64);
    if (n > skip_frames) backtrace_symbols_fd(frames + skip_frames, n - skip_frames, fd);
}

void CrashHandler::OnSignal(int sig) {
    WriteStr(2, "\n=== fatal signal ");
    WriteStr(2, sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL"
             : sig == SIGFPE ? "SIGFPE" : sig == SIGABRT ? "SIGABRT" : "?");
    WriteStr(2, " ===\n");
    WriteBacktrace(2, 2);
    if (g_ring_path[0]) ringlog::RingLog::DumpAllToPath(g_ring_path, ringlog::DUMP_CRASH);
    signal(sig, SIG_DFL);
    raise(sig);
}

void CrashHandler::Install(const char* ring_dump_path) {
    if (ring_dump_path) {
        std::strncpy(g_ring_path, ring_dump_path, sizeof(g_ring_path) - 1);
        g_ring_path[sizeof(g_ring_path) - 1] = 0;
    }
    if (g_installed) return;
    g_installed = true;
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &CrashHandler::OnSignal;
    sa.sa_flags = SA_RESETHAND | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    for (int s : kSignals) sigaction(s, &sa, nullptr);
}

bool CrashHandler::Installed() { return g_installed; }

}  // namespace ps2r::platform
