// core/log/ring_log.h -- always-on per-thread lock-free binary event rings
// (the flight recorder every build carries).
//
// Each emitting thread owns one SPSC ring of fixed 24-byte POD events
// (ring_format.h). Emit is the ONLY hot-path operation: one plain u32 mask
// test, a thread_local Ring* load, 24 bytes of plain stores, one release store
// of the cursor. No locks, no formatting, no IO, no steady-state allocation (a
// thread's ring is allocated ONCE at registration). Overwrite-oldest: drops =
// total_emitted - retained, reported at dump time.
//
// Channel masks + capacity come from Config (log.channels / log.ring_events)
// through Configure(), called once before guest threads start. Index 0 of the
// mask pair is the normal tier, index 1 the verbose tier (a verbose event needs
// its channel bit in BOTH); the verbose tier is a diagnostics-build capability.
//
// Dumps produce .rlog binaries (ring_format.h): DumpAllFd/DumpAllToPath are
// async-signal-safe (open/write/fsync only); DumpTextTailFd is the signal-safe
// text tail; RingDecoder holds the offline text form.
#pragma once

#include <atomic>
#include <cstdint>

#include "engine/core/log/ring_format.h"
#include "engine/core/log/ring_tags.h"

namespace ps2r {
namespace ringlog {

constexpr uint32_t kMaxRings      = 32;
constexpr uint32_t kDefaultEvents = 65536;   // per-thread, pow2
constexpr uint32_t kMinEvents     = 1024;
constexpr uint32_t kMaxEvents     = 1u << 22;

struct Ring {
    Event*                buf = nullptr;      // capacity events, pow2
    uint32_t              capacity = 0;       // pow2
    char                  name[kNameMax + 1] = {0};
    std::atomic<uint64_t> wpos{0};            // producer: relaxed load + release
                                              // store; dumper: acquire load
};

// Memory gauge ids carried by MEM_GAUGE / MEM_GAUGE_COUNT (frozen schema).
enum GaugeId : uint16_t {
    GL_TEX_CACHE      = 1,
    GL_CLUT_TEX_CACHE = 2,
    GL_UPLOAD_CACHE   = 3,
    GL_DECODE_CACHE   = 4,
    GL_L2_CACHE       = 5,
    GL_TARGET_FBOS    = 6,
    RENDER_QUEUE      = 7,
    FMV_BUFFERS       = 8,
    VU_EXEC_CACHE     = 9,
    SNAP_BUFFERS      = 10,
    VU1_PIPELINE      = 11,
    GIF_ARBITER       = 12,
    GL_TEX_POOL       = 13,
    GL_TEX_LIVE       = 14,
    GAUGE_COUNT_      = 15,
};

struct Gauge {
    std::atomic<uint64_t> bytes{0};
    std::atomic<uint64_t> count{0};
};

// Hook the EE layer installs so the fatal path can print the guest register
// block of the last dispatch context without this module knowing its layout.
using GuestContextDumpFn = void (*)(int fd, void* ctx);

class RingLog {
public:
    // One-time configuration before guest threads exist: channel masks
    // (normal, verbose), per-thread capacity (rounded to pow2 within
    // [kMinEvents, kMaxEvents]) and the directory crash dumps go to.
    static void Configure(uint32_t mask_normal, uint32_t mask_verbose,
                          uint32_t capacity, const char* log_dir);

    // "all" | "none" | "0" | comma list of perf,ee,gfx,input,mem -> mask.
    static uint32_t ParseChannelList(const char* list, uint32_t dflt);

    // Register the calling thread with a name (<= 15 chars); allocates the
    // ring (the ONE allocation). A second call renames. Null only when
    // kMaxRings is exhausted.
    static Ring* RegisterThread(const char* name);
    // Slow path for unregistered threads (registers as "t<N>").
    static Ring* RegisterThreadAuto();

    // ---- THE emit path -------------------------------------------------
    static void Emit(Channel ch, uint16_t tag, uint64_t a0 = 0, uint64_t a1 = 0) {
        if (MaskOn(ch, 0)) Record(ch, tag, a0, a1, 0);
    }
    // Verbose tier: additionally requires the channel in the verbose mask.
    static void EmitV(Channel ch, uint16_t tag, uint64_t a0 = 0, uint64_t a1 = 0) {
        if (MaskOn(ch, EVF_VERBOSE)) Record(ch, tag, a0, a1, EVF_VERBOSE);
    }
    static bool ChannelOn(Channel ch) { return (g_mask[0] >> ch) & 1u; }
    static bool MaskOn(Channel ch, uint8_t flags) {
        return (g_mask[flags & EVF_VERBOSE] & (1u << ch)) != 0;
    }

    // Presented-frame counter stamped into every event (the present path
    // increments it; relaxed reads everywhere).
    static uint32_t Frame() { return g_frame.load(std::memory_order_relaxed); }
    static void SetFrame(uint32_t f) { g_frame.store(f, std::memory_order_relaxed); }
    static void BumpFrame() { g_frame.fetch_add(1, std::memory_order_relaxed); }

    // ---- dumps ----------------------------------------------------------
    // Async-signal-safe binary dump of ALL rings to an fd (raw writes only).
    static void DumpAllFd(int fd, uint32_t reason);
    // open(O_CREAT|O_TRUNC) + DumpAllFd + fsync/close. Signal-safe.
    static bool DumpAllToPath(const char* path, uint32_t reason);
    // Signal-safe TEXT tail: last `per_ring` events of each ring.
    static void DumpTextTailFd(int fd, uint32_t per_ring);
    // Normal shutdown: <log_dir>/ring_exit.rlog.
    static bool ExitDump();

    // Display-snap request flag: set by dump/bundle callers, consumed by the
    // present path at the next present and acked through SnapDone.
    static std::atomic<uint32_t>& SnapRequest() { return g_snap_request; }
    static std::atomic<uint32_t>& SnapDone() { return g_snap_done; }

    // ---- manual-dispatch record (EE channel) ----------------------------
    // Records the dispatcher entry on the verbose tier and keeps the ctx
    // pointer unconditionally for the fatal dump's guest register block.
    static void RecordManualDispatch(uint32_t addr, void* ctx, uint32_t a0_reg,
                                     uint32_t ra_reg);
    static void SetGuestContextDumper(GuestContextDumpFn fn);

    // ---- generated-code fast gate ------------------------------------------
    // The generated code tests a plain bool before each INDIRECT_JUMP record
    // (thousands of sites per frame). That bool is owned by the EE layer (the
    // seam that also forwards the record here); it is bound once and kept equal
    // to "EE channel in the verbose tier": latched at Bind and on every
    // Configure, so the call order of the two does not matter. Null unbinds.
    static void BindGeneratedGate(bool* gate);

    // ---- MEM channel gauges ----------------------------------------------
    static void GaugeSet(GaugeId id, uint64_t bytes, uint64_t count);
    static void GaugeAdd(GaugeId id, int64_t dbytes, int64_t dcount);
    // Census point: emits MEM_GAUGE + MEM_GAUGE_COUNT per nonzero gauge, then
    // RSS_SAMPLE with the caller-supplied process memory numbers.
    static void SampleGauges(uint64_t footprint_bytes, uint64_t resident_bytes);

    // Introspection (tests, startup dump).
    static uint32_t MaskNormal() { return g_mask[0]; }
    static uint32_t MaskVerbose() { return g_mask[1]; }
    static uint32_t Capacity();
    static uint32_t RingCount();
    static uint32_t SlotsExhausted();
    static const char* LogDir();
    // Could-not-jump REPORT: flushes the runtime log, dumps every ring to
    // <log_dir>/ring_crash.rlog and writes the text report (pc, frame, guest
    // GPRs of the last dispatch ctx, ring text tail) to stderr and
    // <log_dir>/crash_report.txt. Raw fds only after the flush. It RETURNS: the
    // terminal decision (parking the guest thread) belongs to the caller.
    static void FatalNoJumpReport(uint32_t pc);

private:
    static void Record(Channel ch, uint16_t tag, uint64_t a0, uint64_t a1, uint8_t flags);

    static uint32_t              g_mask[2];
    static std::atomic<uint32_t> g_frame;
    static std::atomic<uint32_t> g_snap_request;
    static std::atomic<uint32_t> g_snap_done;
};

}  // namespace ringlog
}  // namespace ps2r
