// core/triage/triage_bundle.h -- TriageBundle: the crash path's attribution
// record (ALL-ON, NO EXCEPTIONS, CUTOVER.md 2026-10-09). When a leg parks on an
// uncaught exception or a fatal park -- play mode and harness mode alike -- the
// composition root writes, before the process terminates,
//
//   <harness.out_dir | paths.log_dir>/triage/<UTC timestamp>/
//     census.txt             the run.meta rows (the census; same rows as run.meta)
//     drawlist_last200.txt   DrawlistRing::Dump(): the last 200 frames of draw IR
//     fault.txt              the park (reason/pc/tid/detail), the error text, the
//                            faulting draw and its producer (the Graphics ledger)
//     backtrace.txt          backtrace_symbols_fd() of the frames noted on the
//                            THROWING thread (NoteThrowSite) -- else of the writer
//     run.meta               the finalized run.meta text (park=exception)
//
// so a crash in the user's play session is attributable without a repro leg.
// Compiled into every build; the files are plain text. The class holds no state
// beyond the noted throw site (a fixed frame array, first fault wins) and the
// terminate hook; it never allocates on the signal path (it is not a signal
// handler -- CrashHandler owns the fatal-signal set).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ps2r::triage {

class TriageBundle {
public:
    struct Files {
        std::string census;      // census.txt
        std::string drawlist;    // drawlist_last200.txt
        std::string fault;       // fault.txt
        std::string run_meta;    // run.meta
    };
    // The park/exception as the root knows it when it writes the bundle.
    struct Fault {
        std::string park;        // run.meta park= ("exception", a park reason)
        uint32_t pc = 0;
        int tid = 0;
        std::string detail;      // run.meta park_detail= (the error text)
        std::string last_draw;   // DrawlistRing::Describe(Last()) or "none"
        std::string producer;    // the Graphics ledger's fault/refusal sites
        std::string extra;       // any further attribution lines
    };

    // Records the CURRENT thread's backtrace frames as the throw site (the
    // first call wins until Reset); call it on the thread that caught or
    // parked, before unwinding further. `what` is kept for fault.txt.
    static void NoteThrowSite(const char* what);
    static bool ThrowSiteNoted();
    static void Reset();

    // Renders fault.txt from a Fault (the park line first, then the error text,
    // the faulting draw, the producer sites, the noted throw site's what).
    static std::string RenderFault(const Fault& fault);

    // Creates <root>/triage/<UTC timestamp>/ and writes the five files; the
    // backtrace is written with backtrace_symbols_fd() from the noted frames
    // (or the calling thread's when none were noted). Returns the directory,
    // or "" with *err set when the directory or a file could not be created.
    static std::string Write(const std::string& root, const Files& files, std::string* err);

    // "YYYYMMDDTHHMMSSZ" (UTC).
    static std::string UtcTimestamp();

    // std::terminate hook: an exception no handler caught (another thread's,
    // or main's rethrow) runs `hook(what)` on the THROWING thread -- after
    // NoteThrowSite -- then aborts. Install once from the root; the hook is
    // cleared by ClearTerminateHook when the root returns.
    static void InstallTerminateHook(std::function<void(const char*)> hook);
    static void ClearTerminateHook();
};

}  // namespace ps2r::triage
