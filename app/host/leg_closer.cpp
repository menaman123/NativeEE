#include "app/host/leg_closer.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "engine/core/log/logger.h"
#include "engine/core/log/ring_log.h"
#include "engine/core/verify/verify_host.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/mem/mmio_router.h"

namespace {
std::string Hex(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}
}  // namespace

void LegCloser::Close(const char* park, uint32_t pc, int tid, const char* detail) {
    std::lock_guard<std::mutex> lock(mu_);
    if (closed_.exchange(true)) return;
    if (auto* meta = host_.Meta()) {
        meta->Set("park", park ? park : "none");
        meta->Set("park_pc", Hex(pc));
        meta->Set("park_tid", std::to_string(tid));
        meta->Set("park_detail", detail ? detail : "");
        meta->Set("unhandled_mmio", std::to_string(mmio_.UnhandledPageCount()));
        std::string pages;
        for (uint32_t page : mmio_.UnhandledPages()) pages += (pages.empty() ? "" : ",") + Hex(page);
        meta->Set("unhandled_mmio_pages", pages);
        const MmioRouter::FirstUnhandled& first = mmio_.FirstUnhandledAccess();
        meta->Set("unhandled_mmio_first", first.seen ? (std::string(first.write ? "w" : "r") + std::to_string(first.size) + "@" + Hex(first.addr) +
                                                           (first.write ? "=" + Hex(first.value) : ""))
                                                    : "none");
        meta->Set("syscalls_unknown", std::to_string(SyscallTable::Instance().UnknownCount()));
        meta->Set("syscalls_not_implemented", std::to_string(SyscallTable::Instance().NotImplementedCount()));
    }
    host_.Finalize();
    const bool fault = park && std::strcmp(park, "none") != 0;
    if (fault && fault_sink_) fault_sink_(park, pc, tid, detail ? detail : "");
    // The census at exit (ALL-ON, NO EXCEPTIONS (3)): the run.meta rows, on
    // stdout, normal exit and crash path alike, so a play session's last
    // output is readable against the per-toggle census expectations.
    std::fputs("census:\n", stdout);
    if (auto* meta = host_.Meta()) {
        const std::string text = meta->Render();
        size_t start = 0;
        while (start < text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            std::fputs("  ", stdout);
            std::fwrite(text.data() + start, 1, end - start, stdout);
            std::fputc('\n', stdout);
            start = end + 1;
        }
    } else {
        std::fprintf(stdout, "  park=%s\n  park_detail=%s\n  (no run.meta: the harness was not armed)\n", park ? park : "none",
                     detail ? detail : "");
    }
    std::fflush(stdout);
    ps2r::ringlog::RingLog::ExitDump();
    ps2r::log::Logger::Instance().Flush();
}
