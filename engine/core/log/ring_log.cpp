// core/log/ring_log.cpp -- ring registry, emit slow path, signal-safe dumps,
// the could-not-jump report and the generated-code gate latch.
#include "engine/core/log/ring_log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "engine/core/log/logger.h"
#include "engine/core/log/ring_decoder.h"
#include "engine/platform/host/platform.h"

namespace ps2r {
namespace ringlog {

namespace {

Ring                  s_rings[kMaxRings];
std::atomic<uint32_t> s_ring_count{0};
std::atomic<uint32_t> s_slots_exhausted{0};
// A dumper must not touch a slot whose buffer is still being initialised by a
// racing RegisterThread.
std::atomic<bool>     s_ring_ready[kMaxRings];
uint32_t              s_capacity = kDefaultEvents;
thread_local Ring*    t_ring = nullptr;
char                  s_log_dir[512] = "logs";

std::atomic<void*>              s_last_ctx{nullptr};
std::atomic<GuestContextDumpFn> s_ctx_dumper{nullptr};
Gauge                           s_gauges[GAUGE_COUNT_];
bool*                           s_generated_gate = nullptr;  // BindGeneratedGate

uint32_t RoundPow2(uint32_t v) {
    uint32_t p = 1;
    while (p < v && p < kMaxEvents) p <<= 1;
    return p;
}

// ---- async-signal-safe write helpers -----------------------------------
void sw(int fd, const char* s) {
    if (fd < 0 || !s) return;
    size_t n = 0;
    while (s[n]) n++;
    host::SSize r = host::FdWrite(fd, s, n);
    (void)r;
}
void swraw(int fd, const void* p, size_t n) {
    if (fd < 0 || !p || !n) return;
    host::SSize r = host::FdWrite(fd, p, n);
    (void)r;
}
void swhex(int fd, uint64_t v) {
    char buf[16];
    int i = 16;
    if (v == 0) {
        buf[--i] = '0';
    } else {
        while (v && i > 0) {
            buf[--i] = "0123456789abcdef"[v & 0xf];
            v >>= 4;
        }
    }
    sw(fd, "0x");
    swraw(fd, &buf[i], (size_t)(16 - i));
}
void swdec(int fd, uint64_t v) {
    char buf[24];
    int i = 24;
    if (v == 0) buf[--i] = '0';
    while (v && i > 0) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    swraw(fd, &buf[i], (size_t)(24 - i));
}

}  // namespace

uint32_t              RingLog::g_mask[2] = {(1u << CH_COUNT) - 1, 0};
std::atomic<uint32_t> RingLog::g_frame{0};
std::atomic<uint32_t> RingLog::g_snap_request{0};
std::atomic<uint32_t> RingLog::g_snap_done{0};

uint32_t RingLog::ParseChannelList(const char* v, uint32_t dflt) {
    if (!v || !*v) return dflt;
    if (std::strcmp(v, "all") == 0 || std::strcmp(v, "ALL") == 0) return (1u << CH_COUNT) - 1;
    if (std::strcmp(v, "none") == 0 || std::strcmp(v, "NONE") == 0 || std::strcmp(v, "0") == 0)
        return 0;
    uint32_t m = 0;
    const char* p = v;
    while (*p) {
        const char* e = p;
        while (*e && *e != ',') e++;
        size_t len = (size_t)(e - p);
        auto is = [&](const char* s) { return len == std::strlen(s) && host::StrNCaseCmp(p, s, len) == 0; };
        if (is("perf"))       m |= 1u << CH_PERF;
        else if (is("ee"))    m |= 1u << CH_EE;
        else if (is("gfx"))   m |= 1u << CH_GFX;
        else if (is("input")) m |= 1u << CH_INPUT;
        else if (is("mem"))   m |= 1u << CH_MEM;
        else if (len)
            std::fprintf(stderr, "ringlog: unknown channel token: %.*s\n", (int)len, p);
        p = *e ? e + 1 : e;
    }
    return m;
}

void RingLog::Configure(uint32_t mask_normal, uint32_t mask_verbose, uint32_t capacity,
                        const char* log_dir) {
    g_mask[0] = mask_normal & ((1u << CH_COUNT) - 1);
    // A verbose event needs its channel in BOTH tiers.
    g_mask[1] = g_mask[0] & mask_verbose;
    if (capacity >= kMinEvents && capacity <= kMaxEvents) s_capacity = RoundPow2(capacity);
    if (log_dir && *log_dir) std::snprintf(s_log_dir, sizeof(s_log_dir), "%s", log_dir);
    // Generated-code fast gate: INDIRECT_JUMP rides the verbose tier of the
    // EE channel (it fires thousands of times per frame in the dispatch hot
    // path and would flush the ring's flight-recorder depth on the normal tier).
    if (s_generated_gate) *s_generated_gate = ((g_mask[1] >> CH_EE) & 1u) != 0;
}

void RingLog::BindGeneratedGate(bool* gate) {
    s_generated_gate = gate;
    if (gate) *gate = ((g_mask[1] >> CH_EE) & 1u) != 0;
}

Ring* RingLog::RegisterThread(const char* name) {
    if (t_ring) {   // re-register = rename only
        std::snprintf(t_ring->name, sizeof(t_ring->name), "%s", name ? name : "?");
        return t_ring;
    }
    uint32_t idx = s_ring_count.fetch_add(1, std::memory_order_relaxed);
    if (idx >= kMaxRings) {
        s_slots_exhausted.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    Ring& r = s_rings[idx];
    r.capacity = s_capacity;
    r.buf = new Event[r.capacity]();   // the ONE allocation, per thread
    std::snprintf(r.name, sizeof(r.name), "%s", name ? name : "?");
    s_ring_ready[idx].store(true, std::memory_order_release);
    t_ring = &r;
    return t_ring;
}

Ring* RingLog::RegisterThreadAuto() {
    char nm[16];
    std::snprintf(nm, sizeof(nm), "t%u", s_ring_count.load(std::memory_order_relaxed));
    return RegisterThread(nm);
}

void RingLog::Record(Channel ch, uint16_t tag, uint64_t a0, uint64_t a1, uint8_t flags) {
    Ring* r = t_ring;
    if (!r) {
        r = RegisterThreadAuto();
        if (!r) return;                      // slots exhausted (counted)
    }
    const uint64_t s = r->wpos.load(std::memory_order_relaxed);   // owner-only
    Event& e = r->buf[s & (r->capacity - 1)];
    e.tag     = tag;
    e.channel = (uint8_t)ch;
    e.flags   = flags;
    e.frame   = g_frame.load(std::memory_order_relaxed);
    e.a0      = a0;
    e.a1      = a1;
    r->wpos.store(s + 1, std::memory_order_release);
}

// ---- binary dump (signal-safe) ------------------------------------------
void RingLog::DumpAllFd(int fd, uint32_t reason) {
    if (fd < 0) return;
    const uint32_t count = std::min(s_ring_count.load(std::memory_order_acquire), kMaxRings);

    // Snapshot cursors first so the header's totals match the sections.
    uint64_t wpos[kMaxRings];
    uint32_t retained[kMaxRings];
    bool     ready[kMaxRings];
    uint64_t total_drops = 0;
    uint32_t ready_count = 0;
    for (uint32_t i = 0; i < count; i++) {
        ready[i] = s_ring_ready[i].load(std::memory_order_acquire);
        if (!ready[i]) {
            wpos[i] = 0;
            retained[i] = 0;
            continue;
        }
        wpos[i] = s_rings[i].wpos.load(std::memory_order_acquire);
        retained[i] = (uint32_t)std::min<uint64_t>(wpos[i], s_rings[i].capacity);
        total_drops += wpos[i] - retained[i];
        ready_count++;
    }

    FileHeader fh{};
    std::memcpy(fh.magic, kMagic, 8);
    fh.version         = kVersion;
    fh.event_size      = sizeof(Event);
    fh.ring_count      = ready_count;
    fh.reason          = reason;
    fh.vsync_frame     = g_frame.load(std::memory_order_relaxed);
    fh.slots_exhausted = s_slots_exhausted.load(std::memory_order_relaxed);
    // time(2) seconds is enough resolution for a dump stamp and is signal-safe.
    fh.wall_ms     = (uint64_t)::time(nullptr) * 1000ull;
    fh.total_drops = total_drops;
    swraw(fd, &fh, sizeof(fh));

    for (uint32_t i = 0; i < count; i++) {
        if (!ready[i]) continue;
        Ring& r = s_rings[i];
        RingHeader rh{};
        rh.name_len      = (uint32_t)std::strlen(r.name);
        rh.retained      = retained[i];
        rh.total_emitted = wpos[i];
        swraw(fd, &rh, sizeof(rh));
        swraw(fd, r.name, rh.name_len);
        if (!retained[i]) continue;
        const uint64_t start     = wpos[i] - retained[i];
        const uint32_t mask      = r.capacity - 1;
        const uint32_t first_idx = (uint32_t)(start & mask);
        const uint32_t seg1      = std::min<uint32_t>(retained[i], r.capacity - first_idx);
        swraw(fd, &r.buf[first_idx], (size_t)seg1 * sizeof(Event));
        if (seg1 < retained[i]) swraw(fd, &r.buf[0], (size_t)(retained[i] - seg1) * sizeof(Event));
    }
}

bool RingLog::DumpAllToPath(const char* path, uint32_t reason) {
    int fd = host::FdOpen(path, host::kOpenWrite | host::kOpenCreate | host::kOpenTruncate);
    if (fd < 0) return false;
    DumpAllFd(fd, reason);
    host::FdSync(fd);
    host::FdClose(fd);
    return true;
}

void RingLog::DumpTextTailFd(int fd, uint32_t per_ring) {
    if (fd < 0) return;
    const uint32_t count = std::min(s_ring_count.load(std::memory_order_acquire), kMaxRings);
    sw(fd, "[RINGLOG] tail (last ");
    swdec(fd, per_ring);
    sw(fd, " events/ring; full record in the .rlog dump)\n");
    for (uint32_t i = 0; i < count; i++) {
        if (!s_ring_ready[i].load(std::memory_order_acquire)) continue;
        Ring& r = s_rings[i];
        const uint64_t w        = r.wpos.load(std::memory_order_acquire);
        const uint64_t retained = std::min<uint64_t>(w, r.capacity);
        const uint64_t n        = std::min<uint64_t>(retained, per_ring);
        sw(fd, "[RINGLOG] ring=");
        sw(fd, r.name);
        sw(fd, " emitted=");
        swdec(fd, w);
        sw(fd, " showing=");
        swdec(fd, n);
        sw(fd, "\n");
        for (uint64_t s = w - n; s < w; s++) {
            const Event& e = r.buf[s & (r.capacity - 1)];
            const TagInfo* ti = FindTag(e.tag);
            sw(fd, "RL f=");
            swdec(fd, e.frame);
            sw(fd, " ");
            sw(fd, r.name);
            sw(fd, " ");
            sw(fd, RingDecoder::ChannelName(e.channel));
            sw(fd, " ");
            sw(fd, ti ? ti->name : "UNKNOWN_TAG");
            sw(fd, " seq=");
            swdec(fd, s);
            sw(fd, " a0=");
            swhex(fd, e.a0);
            sw(fd, " a1=");
            swhex(fd, e.a1);
            sw(fd, "\n");
        }
    }
}

bool RingLog::ExitDump() {
    std::string path = std::string(s_log_dir) + "/ring_exit.rlog";
    return DumpAllToPath(path.c_str(), DUMP_EXIT);
}

// ---- manual dispatcher record ---------------------------------------------
void RingLog::RecordManualDispatch(uint32_t addr, void* ctx, uint32_t a0_reg, uint32_t ra_reg) {
    // The ctx store stays UNCONDITIONAL: it is the fatal dump's guest register
    // source, independent of ring emission.
    s_last_ctx.store(ctx, std::memory_order_relaxed);
    EmitV(CH_EE, TAG_MANUAL_DISPATCH, addr, ((uint64_t)a0_reg << 32) | ra_reg);
}

void RingLog::SetGuestContextDumper(GuestContextDumpFn fn) {
    s_ctx_dumper.store(fn, std::memory_order_release);
}

// ---- MEM gauges ---------------------------------------------------------------
void RingLog::GaugeSet(GaugeId id, uint64_t bytes, uint64_t count) {
    s_gauges[id].bytes.store(bytes, std::memory_order_relaxed);
    s_gauges[id].count.store(count, std::memory_order_relaxed);
}

void RingLog::GaugeAdd(GaugeId id, int64_t dbytes, int64_t dcount) {
    s_gauges[id].bytes.fetch_add((uint64_t)dbytes, std::memory_order_relaxed);
    s_gauges[id].count.fetch_add((uint64_t)dcount, std::memory_order_relaxed);
}

void RingLog::SampleGauges(uint64_t footprint_bytes, uint64_t resident_bytes) {
    if (!ChannelOn(CH_MEM)) return;
    for (uint16_t i = 1; i < GAUGE_COUNT_; i++) {
        const uint64_t b = s_gauges[i].bytes.load(std::memory_order_relaxed);
        const uint64_t c = s_gauges[i].count.load(std::memory_order_relaxed);
        if (b == 0 && c == 0) continue;
        Emit(CH_MEM, TAG_MEM_GAUGE, i, b);
        Emit(CH_MEM, TAG_MEM_GAUGE_COUNT, i, c);
    }
    Emit(CH_MEM, TAG_RSS_SAMPLE, footprint_bytes, resident_bytes);
}

uint32_t RingLog::Capacity() { return s_capacity; }
uint32_t RingLog::RingCount() { return std::min(s_ring_count.load(), kMaxRings); }
uint32_t RingLog::SlotsExhausted() { return s_slots_exhausted.load(); }
const char* RingLog::LogDir() { return s_log_dir; }

void RingLog::FatalNoJumpReport(uint32_t pc) {
    // Flush the buffered runtime log FIRST; after this only raw fds are used.
    log::Logger::Instance().Flush();

    char rlog[600];
    std::snprintf(rlog, sizeof(rlog), "%s/ring_crash.rlog", s_log_dir);
    DumpAllToPath(rlog, DUMP_FATAL_NOJUMP);

    char report[600];
    std::snprintf(report, sizeof(report), "%s/crash_report.txt", s_log_dir);
    int crash_fd = host::FdOpen(report, host::kOpenWrite | host::kOpenCreate | host::kOpenAppend);
    int fds[2] = {2 /*stderr*/, crash_fd};
    for (int fi = 0; fi < 2; ++fi) {
        int fd = fds[fi];
        if (fd < 0) continue;
        sw(fd, "\n[FATAL-NOJUMP] could-not-jump pc=");
        swhex(fd, pc);
        sw(fd, " vf=");
        swdec(fd, g_frame.load(std::memory_order_relaxed));
        sw(fd, "\n[FATAL-NOJUMP] ring dump: ");
        sw(fd, rlog);
        sw(fd, " (decode with ring_decode)\n");
        void* ctx = s_last_ctx.load(std::memory_order_relaxed);
        GuestContextDumpFn dumper = s_ctx_dumper.load(std::memory_order_acquire);
        if (ctx && dumper) {
            sw(fd, "[FATAL-NOJUMP] guest GPRs (last dispatch ctx):\n");
            dumper(fd, ctx);
        } else {
            sw(fd, "[FATAL-NOJUMP] no guest ctx captured\n");
        }
        DumpTextTailFd(fd, 48);
        if (fd != 2) host::FdSync(fd);
    }
    if (crash_fd >= 0) host::FdClose(crash_fd);
    // No exit here: the caller parks the guest thread (the process never runs
    // static destructors on this path either -- a park is not a return).
}

}  // namespace ringlog
}  // namespace ps2r
