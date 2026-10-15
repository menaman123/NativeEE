#pragma once
// LegCloser: closes a harness leg exactly once -- records the park keys
// (park / park_pc / park_tid / park_detail), the MMIO sink census
// (unhandled_mmio + the page list) and the syscall counters in run.meta,
// finalizes the VerifyHost lanes, hands a FAULT (any park but "none") to the
// fault sink (the composition root's TriageBundle writer), PRINTS the census
// (the run.meta rows) to stdout as a `census:` block -- normal exit and crash
// path alike -- and flushes the logs. Shared by the GuestPark sink (a park on
// any guest thread), the exception paths and the normal return path.
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

namespace ps2r::verify { class VerifyHost; }
class MmioRouter;

class LegCloser {
public:
    // (park, pc, tid, detail) of a fault; runs after run.meta carries the park
    // keys and is finalized, before the census is printed.
    using FaultSink = std::function<void(const char* park, uint32_t pc, int tid, const char* detail)>;

    LegCloser(ps2r::verify::VerifyHost& host, MmioRouter& mmio) : host_(host), mmio_(mmio) {}
    void SetFaultSink(FaultSink sink) { fault_sink_ = std::move(sink); }
    void Close(const char* park, uint32_t pc, int tid, const char* detail);
    bool Closed() const { return closed_.load(); }

private:
    ps2r::verify::VerifyHost& host_;
    MmioRouter& mmio_;
    FaultSink fault_sink_;
    std::atomic<bool> closed_{false};
    std::mutex mu_;
};
