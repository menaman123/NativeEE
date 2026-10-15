// core/triage/triage_bundle.cpp -- TriageBundle (see the header).
#include "engine/core/triage/triage_bundle.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <mutex>
#include <thread>

#include "engine/platform/host/platform.h"

namespace ps2r::triage {

namespace {

constexpr int kMaxFrames = 128;

struct ThrowSite {
    std::mutex mu;
    bool noted = false;
    void* frames[kMaxFrames];
    int count = 0;
    char what[1024] = {0};
    char thread[64] = {0};
};
ThrowSite& Site() {
    static ThrowSite site;
    return site;
}

struct TerminateHook {
    std::mutex mu;
    std::function<void(const char*)> hook;
    std::terminate_handler previous = nullptr;
    std::atomic<bool> running{false};
};
TerminateHook& Hook() {
    static TerminateHook hook;
    return hook;
}

std::string ThreadName() {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%llx", static_cast<unsigned long long>(
        std::hash<std::thread::id>{}(std::this_thread::get_id())));
    return buf;
}

bool WriteText(const std::string& path, const std::string& text, std::string* err) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        if (err) *err = path + ": cannot create";
        return false;
    }
    const bool ok = text.empty() || std::fwrite(text.data(), 1, text.size(), f) == text.size();
    std::fclose(f);
    if (!ok && err) *err = path + ": short write";
    return ok;
}

bool MakeDir(const std::string& path) { return host::MakeDirectory(path.c_str()); }

void OnTerminate() {
    TerminateHook& h = Hook();
    std::string what = "std::terminate (no active exception)";
    if (std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            what = ex.what();
        } catch (...) {
            what = "std::terminate (non-std exception)";
        }
    }
    TriageBundle::NoteThrowSite(what.c_str());
    std::function<void(const char*)> hook;
    {
        std::lock_guard<std::mutex> lock(h.mu);
        hook = h.hook;
    }
    // A hook that itself terminates must not recurse into the bundle writer.
    if (hook && !h.running.exchange(true)) hook(what.c_str());
    std::fprintf(stderr, "nativeee: terminating: %s\n", what.c_str());
    std::abort();
}

}  // namespace

void TriageBundle::NoteThrowSite(const char* what) {
    void* frames[kMaxFrames];
    const int n = host::Backtrace(frames, kMaxFrames);
    ThrowSite& s = Site();
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.noted) return;
    s.noted = true;
    s.count = n;
    std::memcpy(s.frames, frames, sizeof(void*) * static_cast<size_t>(n > 0 ? n : 0));
    std::strncpy(s.what, what ? what : "", sizeof s.what - 1);
    std::strncpy(s.thread, ThreadName().c_str(), sizeof s.thread - 1);
}

bool TriageBundle::ThrowSiteNoted() {
    ThrowSite& s = Site();
    std::lock_guard<std::mutex> lock(s.mu);
    return s.noted;
}

void TriageBundle::Reset() {
    ThrowSite& s = Site();
    std::lock_guard<std::mutex> lock(s.mu);
    s.noted = false;
    s.count = 0;
    s.what[0] = 0;
    s.thread[0] = 0;
}

std::string TriageBundle::UtcTimestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    host::GmTimeUtc(static_cast<int64_t>(now), &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

std::string TriageBundle::RenderFault(const Fault& fault) {
    char pc[32];
    std::snprintf(pc, sizeof pc, "0x%x", fault.pc);
    std::string out;
    out += "park=" + fault.park + " park_pc=" + pc + " park_tid=" + std::to_string(fault.tid) + "\n";
    out += "error: " + (fault.detail.empty() ? std::string("(none)") : fault.detail) + "\n";
    out += "faulting_draw: " + (fault.last_draw.empty() ? std::string("none") : fault.last_draw) + "\n";
    out += "producer: " + (fault.producer.empty() ? std::string("none") : fault.producer) + "\n";
    if (!fault.extra.empty()) out += fault.extra + (fault.extra.back() == '\n' ? "" : "\n");
    ThrowSite& s = Site();
    std::lock_guard<std::mutex> lock(s.mu);
    out += std::string("throw_site: ") + (s.noted ? std::string("noted on thread ") + s.thread + " what=" + s.what
                                                 : std::string("not noted (backtrace.txt is the writer's thread)")) + "\n";
    return out;
}

std::string TriageBundle::Write(const std::string& root, const Files& files, std::string* err) {
    const std::string base = root.empty() ? std::string(".") : root;
    if (!MakeDir(base) || !MakeDir(base + "/triage")) {
        if (err) *err = base + "/triage: cannot create";
        return "";
    }
    std::string dir = base + "/triage/" + UtcTimestamp();
    // Two bundles in one second (a test's synthetic fault after a real one):
    // suffix the second so neither overwrites the other.
    for (int n = 1; n < 100; ++n) {
        if (!host::IsDirectory(dir.c_str())) break;
        dir = base + "/triage/" + UtcTimestamp() + "-" + std::to_string(n);
    }
    if (!MakeDir(dir)) {
        if (err) *err = dir + ": cannot create";
        return "";
    }
    if (!WriteText(dir + "/census.txt", files.census.empty() ? "census: (no run.meta)\n" : files.census, err)) return "";
    if (!WriteText(dir + "/drawlist_last200.txt", files.drawlist.empty() ? "drawlist ring: (none)\n" : files.drawlist, err)) return "";
    if (!WriteText(dir + "/fault.txt", files.fault.empty() ? "park=unknown\n" : files.fault, err)) return "";
    if (!WriteText(dir + "/run.meta", files.run_meta.empty() ? "finalized=0\n" : files.run_meta, err)) return "";
    {
        const std::string path = dir + "/backtrace.txt";
        const int fd = host::FdOpen(path.c_str(), host::kOpenWrite | host::kOpenCreate | host::kOpenTruncate);
        if (fd < 0) {
            if (err) *err = path + ": cannot create";
            return "";
        }
        ThrowSite& s = Site();
        std::lock_guard<std::mutex> lock(s.mu);
        if (s.noted && s.count > 0) {
            const std::string head = std::string("throwing thread ") + s.thread + ": " + s.what + "\n";
            (void)host::FdWrite(fd, head.data(), head.size());
            host::BacktraceSymbolsFd(s.frames, s.count, fd);
        } else {
            const std::string head = "writer thread " + ThreadName() + " (no throw site noted)\n";
            (void)host::FdWrite(fd, head.data(), head.size());
            void* frames[kMaxFrames];
            const int n = host::Backtrace(frames, kMaxFrames);
            host::BacktraceSymbolsFd(frames, n, fd);
        }
        host::FdClose(fd);
    }
    return dir;
}

void TriageBundle::InstallTerminateHook(std::function<void(const char*)> hook) {
    TerminateHook& h = Hook();
    std::lock_guard<std::mutex> lock(h.mu);
    h.hook = std::move(hook);
    h.running.store(false);
    const std::terminate_handler prev = std::set_terminate(&OnTerminate);
    if (prev != &OnTerminate) h.previous = prev;
}

void TriageBundle::ClearTerminateHook() {
    TerminateHook& h = Hook();
    std::lock_guard<std::mutex> lock(h.mu);
    h.hook = nullptr;
    if (h.previous) std::set_terminate(h.previous);
    h.previous = nullptr;
}

}  // namespace ps2r::triage
