// ps2r_replay_host -- the tier-1 verification harness of recomp-agent.
//
// Replays the recorded calls of ONE function (a RACORP1 corpus, protocol/corpus-format.md) against a CANDIDATE body
// on a scratch memory image, and reports per call whether the candidate's effects equal the recording's.
//
//   ps2r_replay_host --corpus FILE --fn ADDR [--candidate LIB [--symbol NAME]] [--callees auto|recorded|original]
//                    [--max-records N] [--frame BYTES] [--lenient-callees] [--ignore-v1] [--verbose N] [--report FILE]
//
// Per record:  the scratch RAM/scratchpad/I-O table is seeded with the record's READ set (the bytes as the game first
// saw them) and the stack arguments; the registers get a0..a3, t0..t3, sp, ra; the candidate runs through the
// engine's runtime function map (update_function_impl: the same seam a live hot swap uses); then the record's WRITE
// set (final values) is compared byte for byte against the image, v0/v1 against the registers, and the sequence of
// callees against the recording's. The function's own stack frame [sp - frame, sp) is not a memory effect (a
// hand-written body spills nothing), so its bytes are excluded from the comparison. Reads of bytes the recording did
// not capture are reported as UNCOVERED (the recording is incomplete there), never as a failure.
//
// Callees (functions the body calls):  `recorded` applies the callee's own record from the same corpus (matched on
// callee, caller == the function under test, a0..a3; sp and ra break ties) -- its write set and v0/v1 -- without
// running it; `original` runs the generated body of the callee on the scratch image; `auto` (default) takes the
// recorded form when exactly one record matches and the original body otherwise. Either way the callee's accesses
// are attributed to the callee (probe depth), as the tracer did. Kernel syscalls reached inside a callee's body have
// no effect here and are listed per record.
//
// Without --candidate the ORIGINAL generated body is replayed (the identity run: the harness's own first gate).
// The report is one JSON document on stdout (or --report FILE); exit status 0 = every record passed, 1 = a failure,
// 2 = a usage / load error.
#include <dlfcn.h>

#include <algorithm>
#include <cfenv>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "racorp.h"
#include "replay_state.h"

#include "engine/ee/cpu/cpu_state.h"
#include "engine/ee/cpu/ps2_float.h"
#include "engine/ee/recomp_abi/host_app/syscalls.h"   // g_ee_backedge_budget
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "recompiled.h"   // the generated surface: recomp_call, update_function_impl, g_recomp_fn_* (the build's gen dir)

namespace replay {

// ---------------------------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------------------------
State& S() {
    static State* s = [] {
        auto* p = new State();
        p->ram_state.assign(MainMemory::kSize, 0);
        p->spad_state.assign(Scratchpad::kSize, 0);
        return p;
    }();
    return *s;
}

uint64_t ReplayMmio::Read(uint32_t addr, unsigned size) {
    uint64_t v = 0;
    const uint32_t norm = Normalise(addr);
    for (unsigned i = 0; i < size; ++i) {
        auto it = bytes.find(norm + i);
        if (it != bytes.end()) v |= static_cast<uint64_t>(it->second) << (8 * i);
    }
    return v;
}
void ReplayMmio::Write(uint32_t addr, unsigned size, uint64_t value) {
    const uint32_t norm = Normalise(addr);
    for (unsigned i = 0; i < size; ++i) {
        bytes[norm + i] = static_cast<uint8_t>(value >> (8 * i));
        state[norm + i] |= kWritten;
    }
}

uint8_t* State::StatePtr(uint32_t norm) {
    if (IsRam(norm)) return ram_state.data() + norm;
    if (IsSpad(norm)) return spad_state.data() + Scratchpad::Offset(norm);
    return nullptr;
}

void State::MarkRange(uint32_t norm, uint32_t len, uint8_t bits) {
    undo.emplace_back(norm, len);
    for (uint32_t i = 0; i < len; ++i) {
        if (uint8_t* st = StatePtr(norm + i)) *st |= bits;
        else mmio_handler.state[norm + i] |= bits;
    }
}

void State::Poke(uint32_t norm, const uint8_t* src, uint32_t len, uint8_t bits) {
    for (uint32_t i = 0; i < len; ++i) {
        const uint32_t a = norm + i;
        if (IsRam(a)) ram.Data()[a] = src[i];
        else if (IsSpad(a)) spad.Data()[Scratchpad::Offset(a)] = src[i];
        else mmio_handler.bytes[a] = src[i];
    }
    MarkRange(norm, len, bits);
}

bool State::Peek(uint32_t norm, uint8_t* out, uint32_t len) {
    for (uint32_t i = 0; i < len; ++i) {
        const uint32_t a = norm + i;
        if (IsRam(a)) out[i] = ram.Data()[a];
        else if (IsSpad(a)) out[i] = spad.Data()[Scratchpad::Offset(a)];
        else {
            auto it = mmio_handler.bytes.find(a);
            out[i] = it == mmio_handler.bytes.end() ? 0 : it->second;
        }
    }
    return true;
}

bool State::Known(uint32_t norm) {
    if (uint8_t* st = StatePtr(norm)) return *st != 0;
    auto it = mmio_handler.state.find(norm);
    return it != mmio_handler.state.end() && it->second != 0;
}

void State::Reset() {
    for (const auto& [a, n] : undo) {
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t x = a + i;
            if (IsRam(x)) { ram.Data()[x] = 0; ram_state[x] = 0; }
            else if (IsSpad(x)) { spad.Data()[Scratchpad::Offset(x)] = 0; spad_state[Scratchpad::Offset(x)] = 0; }
        }
    }
    undo.clear();
    mmio_handler.bytes.clear();
    mmio_handler.state.clear();
    depth = 0;
    callee_seq.clear();
    syscalls.clear();
    callees_unmatched.clear();
    own_read_bytes = own_read_seeded = uncovered_reads = 0;
    uncovered_first.clear();
    own_writes.clear();
    trips = 0;
    vu0_calls = 0;
    abort_reason.clear();
}

void State::OnRead(uint32_t addr, uint32_t n) {
    if (depth != 1) return;   // a callee's read: the tracer credited it to the callee, so do we
    const uint32_t norm = Normalise(addr);
    own_read_bytes += n;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t a = norm + i;
        uint8_t* st = StatePtr(a);
        uint8_t cur = st ? *st : [&] { auto it = mmio_handler.state.find(a); return it == mmio_handler.state.end() ? uint8_t(0) : it->second; }();
        if (cur & (kSeeded | kWritten)) {
            if ((cur & kSeeded) && !(cur & kReadByCandidate)) ++own_read_seeded;   // a recorded byte, first touch
        } else {
            ++uncovered_reads;
            if (uncovered_first.size() < 8) uncovered_first.push_back(a);
        }
        if (!(cur & kReadByCandidate)) {
            if (st) *st |= kReadByCandidate; else mmio_handler.state[a] |= kReadByCandidate;
            if (!cur) undo.emplace_back(a, 1);   // an untouched byte now carries state: reset must clear it
        }
    }
}

void State::OnWrite(uint32_t addr, uint32_t n) {
    const uint32_t norm = Normalise(addr);
    MarkRange(norm, n, kWritten);
    if (depth == 1) own_writes.emplace_back(norm, n);
}

// ---------------------------------------------------------------------------------------------------------------
// Options and report
// ---------------------------------------------------------------------------------------------------------------
enum class CalleeMode { Auto, Recorded, Original };

struct Options {
    std::string corpus;
    uint32_t fn = 0;
    std::string candidate;        // path of the candidate shared object; empty = identity (the generated body)
    std::string symbol = "ps2r_candidate";
    CalleeMode callees = CalleeMode::Auto;
    size_t max_records = 0;       // 0 = all
    uint32_t frame = 0x4000;      // the function's own stack frame below sp (excluded from the comparison)
    bool lenient_callees = false;
    bool ignore_v1 = false;       // a 32-bit result leaves v1 unspecified; compare v0 only
    // The recording holds v0/v1 at EXIT only. A function that does not write v1 (or v0, when it returns nothing)
    // leaves the CALLER's value there, which the record has no entry value for. `seed` puts the recorded exit value
    // in the register at entry (a pass-through then passes; a wrong computation still fails); `zero` leaves 0.
    bool seed_v1 = true;
    bool seed_v0 = false;
    size_t verbose = 0;           // per-record lines to include (failures first)
    std::string report;
    uint64_t max_trips = 200000;
};

struct RecordResult {
    size_t index = 0;
    uint32_t frame = 0;
    uint32_t count = 1;
    bool pass = true;
    std::string reason;           // the first failure, one sentence
    uint32_t addr = 0;            // the first differing address (when a byte failed)
    uint8_t expected = 0, got = 0;
    bool has_addr = false;
    uint64_t uncovered = 0;
    uint32_t uncovered_first = 0;
    bool has_uncovered_first = false;
    std::vector<uint32_t> callees_seen;
    std::vector<uint32_t> syscalls;
    std::vector<uint32_t> callees_unmatched;
    uint32_t vu0 = 0;
    uint64_t own_read_bytes = 0, own_read_seeded = 0;
    uint64_t write_bytes_compared = 0;
    uint64_t write_bytes_frame = 0;   // recorded writes inside the own frame (not compared)
    std::vector<uint64_t> args;
};

std::string Hex(uint64_t v) {
    char b[32];
    std::snprintf(b, sizeof b, "0x%" PRIx64, v);
    return b;
}
std::string Hex8(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}
std::string Json(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o + "\"";
}
template <typename T>
std::string JsonList(const std::vector<T>& v, std::string (*f)(T)) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) o += ", ";
        o += f(v[i]);
    }
    return o + "]";
}
std::string HexQ(const std::vector<uint32_t>& v) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) o += ", ";
        o += Json(Hex8(v[i]));
    }
    return o + "]";
}

// ---------------------------------------------------------------------------------------------------------------
// The callee router: the recorded form of a callee's effect, or its original body
// ---------------------------------------------------------------------------------------------------------------
struct CalleeIndex {
    // (callee fn) -> records of that callee whose caller is the function under test
    std::unordered_map<uint32_t, std::vector<const racorp::Record*>> by_fn;
};

struct Driver {
    Options opt;
    racorp::Corpus corpus;
    CalleeIndex callees;
    std::vector<const racorp::Record*> targets;
    void (*candidate)(CpuContext&, uint32_t) = nullptr;
    void* candidate_lib = nullptr;
    uint32_t target_id = 0xFFFFFFFFu;
    // census of how callees were served
    uint64_t callee_recorded = 0, callee_original = 0, callee_ambiguous = 0, callee_unmatched = 0;
    std::set<uint32_t> callee_addrs_installed;

    void IndexCallees() {
        for (const auto& r : corpus.records)
            if (r.caller == opt.fn) callees.by_fn[r.fn].push_back(&r);
    }

    // Serve one call of callee `cfn` from inside the candidate: the recorded form when it is unambiguous.
    // Returns true when served from the recording (the body must not run).
    bool ServeRecorded(uint32_t cfn, CpuContext& ctx) {
        auto it = callees.by_fn.find(cfn);
        if (it == callees.by_fn.end()) { ++callee_unmatched; S().callees_unmatched.push_back(cfn); return false; }
        const uint64_t a0 = ctx.cpuRegs.GPR.r[4].UD[0], a1 = ctx.cpuRegs.GPR.r[5].UD[0];
        const uint64_t a2 = ctx.cpuRegs.GPR.r[6].UD[0], a3 = ctx.cpuRegs.GPR.r[7].UD[0];
        const uint32_t sp = ctx.cpuRegs.GPR.r[29].UL[0], ra = ctx.cpuRegs.GPR.r[31].UL[0];
        std::vector<const racorp::Record*> m;
        for (const auto* r : it->second) {
            if (r->args.size() < 4) continue;
            if (r->args[0] == a0 && r->args[1] == a1 && r->args[2] == a2 && r->args[3] == a3) m.push_back(r);
        }
        if (m.empty()) { ++callee_unmatched; S().callees_unmatched.push_back(cfn); return false; }
        // Collapse records with identical effects (the same ret and write set), then break ties on sp, then ra.
        auto same_effect = [](const racorp::Record* x, const racorp::Record* y) {
            if (x->v0 != y->v0 || x->v1 != y->v1 || x->writes.size() != y->writes.size()) return false;
            for (size_t i = 0; i < x->writes.size(); ++i)
                if (x->writes[i].addr != y->writes[i].addr || x->writes[i].bytes != y->writes[i].bytes) return false;
            return true;
        };
        auto distinct = [&](const std::vector<const racorp::Record*>& v) {
            std::vector<const racorp::Record*> d;
            for (const auto* r : v) {
                bool dup = false;
                for (const auto* q : d) if (same_effect(q, r)) { dup = true; break; }
                if (!dup) d.push_back(r);
            }
            return d;
        };
        std::vector<const racorp::Record*> d = distinct(m);
        if (d.size() > 1) {
            std::vector<const racorp::Record*> by_sp;
            for (const auto* r : m) if (r->sp == sp) by_sp.push_back(r);
            if (!by_sp.empty()) d = distinct(by_sp);
            if (d.size() > 1) {
                std::vector<const racorp::Record*> by_ra;
                for (const auto* r : by_sp.empty() ? m : by_sp) if (r->ra == ra) by_ra.push_back(r);
                if (!by_ra.empty()) d = distinct(by_ra);
            }
        }
        if (d.size() != 1) { ++callee_ambiguous; return false; }
        const racorp::Record* r = d[0];
        auto& s = S();
        for (const auto& w : r->writes) s.Poke(w.addr, w.bytes.data(), static_cast<uint32_t>(w.bytes.size()), kWritten);
        ctx.cpuRegs.GPR.r[2].UD[0] = r->v0;
        ctx.cpuRegs.GPR.r[3].UD[0] = r->v1;
        ++callee_recorded;
        return true;
    }

    void InstallCalleeRouting() {
        if (opt.callees == CalleeMode::Original) return;
        std::set<uint32_t> addrs;
        for (const auto* r : targets)
            for (uint32_t c : r->callees) addrs.insert(c);
        for (uint32_t cfn : addrs) {
            const uint32_t cid = recomp_fn_id_for(cfn);
            if (cid == 0xFFFFFFFFu) continue;
            if (cfn == opt.fn) continue;   // recursion: the candidate handles itself
            void (*raw)(CpuContext&, uint32_t) = g_recomp_fn_raw[cid];
            Driver* self = this;
            std::function<void(CpuContext&, uint32_t)> stub = [self, cfn, raw](CpuContext& ctx, uint32_t entry) {
                if (self->ServeRecorded(cfn, ctx)) return;
                if (self->opt.callees == CalleeMode::Recorded) return;   // strict: unmatched callee has no effect
                ++self->callee_original;
                if (raw) raw(ctx, entry);
            };
            if (update_function_impl(cfn, stub)) callee_addrs_installed.insert(cfn);
        }
    }

    bool LoadCandidate(std::string* err) {
        if (opt.candidate.empty()) return true;
        candidate_lib = dlopen(opt.candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!candidate_lib) { *err = std::string("dlopen: ") + dlerror(); return false; }
        void* sym = dlsym(candidate_lib, opt.symbol.c_str());
        if (!sym) { *err = "the candidate library has no symbol " + opt.symbol; return false; }
        candidate = reinterpret_cast<void (*)(CpuContext&, uint32_t)>(sym);
        void (*cand)(CpuContext&, uint32_t) = candidate;
        if (!update_function_impl(opt.fn, [cand](CpuContext& ctx, uint32_t entry) { cand(ctx, entry); })) {
            *err = "update_function_impl refused " + Hex8(opt.fn) + " (not a generated function start)";
            return false;
        }
        return true;
    }

    RecordResult Replay(const racorp::Record& r) {
        auto& s = S();
        RecordResult res;
        res.index = r.index;
        res.frame = r.frame;
        res.count = r.count;
        res.args = r.args;
        s.Reset();
        s.target_fn = opt.fn;
        s.max_trips = opt.max_trips;
        // 1. seed the image: the read set (first-seen values), then the stack arguments
        for (const auto& run : r.reads) s.Poke(run.addr, run.bytes.data(), static_cast<uint32_t>(run.bytes.size()), kSeeded);
        // 1b. poison the recorded-write bytes the function did not read: a store the candidate forgets then differs
        {
            std::vector<uint8_t> poison;
            for (const auto& run : r.writes) {
                poison.resize(run.bytes.size());
                bool any_seeded = false;
                for (size_t i = 0; i < run.bytes.size(); ++i) {
                    const uint32_t a = run.addr + static_cast<uint32_t>(i);
                    const uint8_t* st = s.StatePtr(a);
                    const uint8_t cur = st ? *st : [&] { auto it = s.mmio_handler.state.find(a); return it == s.mmio_handler.state.end() ? uint8_t(0) : it->second; }();
                    if (cur & kSeeded) any_seeded = true;
                    poison[i] = static_cast<uint8_t>(~run.bytes[i]);
                }
                if (!any_seeded) {
                    s.Poke(run.addr, poison.data(), static_cast<uint32_t>(poison.size()), kPoison);
                    continue;
                }
                for (size_t i = 0; i < run.bytes.size(); ++i) {
                    const uint32_t a = run.addr + static_cast<uint32_t>(i);
                    const uint8_t* st = s.StatePtr(a);
                    const uint8_t cur = st ? *st : [&] { auto it = s.mmio_handler.state.find(a); return it == s.mmio_handler.state.end() ? uint8_t(0) : it->second; }();
                    if (cur & kSeeded) continue;
                    s.Poke(a, &poison[i], 1, kPoison);
                }
            }
        }
        if (r.args.size() >= 12) {
            for (int i = 0; i < 4; ++i) {
                const uint32_t v = static_cast<uint32_t>(r.args[8 + i]);
                uint8_t b[4];
                std::memcpy(b, &v, 4);
                s.Poke(Normalise(r.sp + 0x10 + 4 * i), b, 4, kSeeded);
            }
        }
        // 2. the registers
        CpuContext ctx;
        std::memset(&ctx, 0, sizeof ctx);
        for (int i = 0; i < 4 && i < static_cast<int>(r.args.size()); ++i) ctx.cpuRegs.GPR.r[4 + i].UD[0] = r.args[i];
        for (int i = 4; i < 8 && i < static_cast<int>(r.args.size()); ++i) ctx.cpuRegs.GPR.r[4 + i].UD[0] = r.args[i];
        ctx.cpuRegs.GPR.r[29].UD[0] = r.sp;
        ctx.cpuRegs.GPR.r[31].UD[0] = r.ra;
        if (opt.seed_v1) ctx.cpuRegs.GPR.r[3].UD[0] = r.v1;
        if (opt.seed_v0) ctx.cpuRegs.GPR.r[2].UD[0] = r.v0;
        ctx.cpuRegs.pc = r.fn;
        // 3. run (the EE thread's rounding mode; recomp_call = the engine's dispatch, probe armed on every id)
        Ps2Float::SetGuestThreadRounding();
        g_ee_backedge_budget = 1024;
        try {
            recomp_call(target_id, r.fn, ctx);
        } catch (const Abort& a) {
            res.pass = false;
            res.reason = a.reason;
        } catch (const std::exception& e) {
            res.pass = false;
            res.reason = std::string("exception: ") + e.what();
        }
        res.callees_seen = s.callee_seq;
        res.syscalls = s.syscalls;
        res.callees_unmatched = s.callees_unmatched;
        res.vu0 = s.vu0_calls;
        res.own_read_bytes = s.own_read_bytes;
        res.own_read_seeded = s.own_read_seeded;
        res.uncovered = s.uncovered_reads;
        if (!s.uncovered_first.empty()) { res.has_uncovered_first = true; res.uncovered_first = s.uncovered_first[0]; }
        if (!res.pass) return res;
        // 4. compare: v0 / v1
        const uint64_t v0 = ctx.cpuRegs.GPR.r[2].UD[0], v1 = ctx.cpuRegs.GPR.r[3].UD[0];
        if (v0 != r.v0) { res.pass = false; res.reason = "v0 " + Hex(v0) + " != recorded " + Hex(r.v0); return res; }
        if (!opt.ignore_v1 && v1 != r.v1) { res.pass = false; res.reason = "v1 " + Hex(v1) + " != recorded " + Hex(r.v1); return res; }
        // 5. compare: the recorded write set, outside the own frame
        const uint32_t frame_lo = r.sp >= opt.frame ? r.sp - opt.frame : 0, frame_hi = r.sp;
        auto in_frame = [&](uint32_t a) { return IsRam(a) && a >= frame_lo && a < frame_hi; };
        std::vector<uint8_t> got;
        for (const auto& w : r.writes) {
            got.resize(w.bytes.size());
            s.Peek(w.addr, got.data(), static_cast<uint32_t>(w.bytes.size()));
            for (size_t i = 0; i < w.bytes.size(); ++i) {
                const uint32_t a = w.addr + static_cast<uint32_t>(i);
                if (in_frame(a)) { ++res.write_bytes_frame; continue; }
                ++res.write_bytes_compared;
                if (got[i] != w.bytes[i]) {
                    res.pass = false;
                    res.has_addr = true;
                    res.addr = a;
                    res.expected = w.bytes[i];
                    res.got = got[i];
                    char b[96];
                    std::snprintf(b, sizeof b, "write mismatch at %s: expected 0x%02x, got 0x%02x", Hex8(a).c_str(),
                                  w.bytes[i], got[i]);
                    res.reason = b;
                    return res;
                }
            }
        }
        // 6. extra writes: bytes the candidate wrote that the recording does not have (outside the own frame)
        {
            // the recorded write set as a sorted list of ranges
            std::vector<std::pair<uint32_t, uint32_t>> rec;
            for (const auto& w : r.writes) rec.emplace_back(w.addr, static_cast<uint32_t>(w.bytes.size()));
            std::sort(rec.begin(), rec.end());
            auto recorded = [&](uint32_t a) {
                auto it = std::upper_bound(rec.begin(), rec.end(), std::make_pair(a, 0xFFFFFFFFu));
                if (it == rec.begin()) return false;
                --it;
                return a >= it->first && a < it->first + it->second;
            };
            for (const auto& [a0, n] : s.own_writes) {
                for (uint32_t i = 0; i < n; ++i) {
                    const uint32_t a = a0 + i;
                    if (in_frame(a) || recorded(a)) continue;
                    res.pass = false;
                    res.has_addr = true;
                    res.addr = a;
                    uint8_t g = 0;
                    s.Peek(a, &g, 1);
                    res.got = g;
                    res.expected = 0;
                    res.reason = "extra write at " + Hex8(a) + " (the recording has no write there)";
                    return res;
                }
            }
        }
        // 7. the callee sequence
        if (s.callee_seq != r.callees) {
            std::string why = "callee sequence " + HexQ(s.callee_seq) + " != recorded " + HexQ(r.callees);
            if (!opt.lenient_callees) { res.pass = false; res.reason = why; return res; }
            res.reason = "(lenient) " + why;
        }
        return res;
    }
};

// ---------------------------------------------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------------------------------------------
int Usage(const char* why) {
    if (why) std::fprintf(stderr, "ps2r_replay_host: %s\n", why);
    std::fprintf(stderr,
                 "usage: ps2r_replay_host --corpus FILE --fn ADDR [--candidate LIB [--symbol NAME]]\n"
                 "                        [--callees auto|recorded|original] [--max-records N] [--frame BYTES]\n"
                 "                        [--lenient-callees] [--ignore-v1] [--v1-entry seed|zero] [--v0-entry zero|seed]\n"
                 "                        [--verbose N] [--report FILE] [--max-trips N]\n");
    return 2;
}

bool ParseU32(const char* s, uint32_t* out) {
    char* end = nullptr;
    unsigned long long v = std::strtoull(s, &end, 0);
    if (!end || *end || v > 0xFFFFFFFFull) return false;
    *out = static_cast<uint32_t>(v);
    return true;
}

}  // namespace replay

int main(int argc, char** argv) {
    using namespace replay;
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { Usage((std::string(what) + " needs a value").c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--corpus") opt.corpus = next("--corpus");
        else if (a == "--fn") { if (!ParseU32(next("--fn"), &opt.fn)) return Usage("--fn: bad address"); }
        else if (a == "--candidate") opt.candidate = next("--candidate");
        else if (a == "--symbol") opt.symbol = next("--symbol");
        else if (a == "--callees") {
            std::string m = next("--callees");
            if (m == "auto") opt.callees = CalleeMode::Auto;
            else if (m == "recorded") opt.callees = CalleeMode::Recorded;
            else if (m == "original") opt.callees = CalleeMode::Original;
            else return Usage("--callees: auto | recorded | original");
        } else if (a == "--max-records") opt.max_records = std::strtoull(next("--max-records"), nullptr, 0);
        else if (a == "--frame") { if (!ParseU32(next("--frame"), &opt.frame)) return Usage("--frame: bad size"); }
        else if (a == "--lenient-callees") opt.lenient_callees = true;
        else if (a == "--ignore-v1") opt.ignore_v1 = true;
        else if (a == "--v1-entry") { std::string m = next("--v1-entry"); if (m == "seed") opt.seed_v1 = true; else if (m == "zero") opt.seed_v1 = false; else return Usage("--v1-entry: seed | zero"); }
        else if (a == "--v0-entry") { std::string m = next("--v0-entry"); if (m == "seed") opt.seed_v0 = true; else if (m == "zero") opt.seed_v0 = false; else return Usage("--v0-entry: seed | zero"); }
        else if (a == "--verbose") opt.verbose = std::strtoull(next("--verbose"), nullptr, 0);
        else if (a == "--report") opt.report = next("--report");
        else if (a == "--max-trips") opt.max_trips = std::strtoull(next("--max-trips"), nullptr, 0);
        else if (a == "--help" || a == "-h") return Usage(nullptr);
        else return Usage(("unknown argument " + a).c_str());
    }
    if (opt.corpus.empty() || !opt.fn) return Usage("--corpus and --fn are required");

    const auto t0 = std::chrono::steady_clock::now();
    Driver d;
    d.opt = opt;
    try {
        d.corpus = racorp::Read(opt.corpus);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "ps2r_replay_host: %s\n", e.what());
        return 2;
    }
    for (const auto& r : d.corpus.records)
        if (r.fn == opt.fn) d.targets.push_back(&r);
    if (opt.max_records && d.targets.size() > opt.max_records) d.targets.resize(opt.max_records);
    d.IndexCallees();

    // The engine surface: bind the scratch image, build the function map, arm the probe on every id.
    State& s = S();
    RecompAbi::BindMemory(&s.mem);
    initialize_recompiled_functions();
    for (uint32_t i = 0; i < g_recomp_fn_count; ++i) g_recomp_fn_probe[i] = 1;
    d.target_id = recomp_fn_id_for(opt.fn);
    if (d.target_id == 0xFFFFFFFFu) {
        std::fprintf(stderr, "ps2r_replay_host: %s is not a generated function start\n", Hex8(opt.fn).c_str());
        return 2;
    }
    std::string err;
    if (!d.LoadCandidate(&err)) {
        std::fprintf(stderr, "ps2r_replay_host: %s\n", err.c_str());
        return 2;
    }
    d.InstallCalleeRouting();
    const auto t1 = std::chrono::steady_clock::now();

    // Replay
    std::vector<RecordResult> results;
    results.reserve(d.targets.size());
    uint64_t pass_records = 0, pass_calls = 0, fail_records = 0, fail_calls = 0, uncovered_records = 0;
    uint64_t read_bytes_recorded = 0, read_bytes_seeded_hit = 0, write_bytes_compared = 0, write_bytes_frame = 0;
    std::map<std::vector<uint32_t>, std::pair<uint64_t, uint64_t>> shapes;   // callee sequence -> (records, passed)
    const RecordResult* first_failure = nullptr;
    for (const auto* r : d.targets) {
        results.push_back(d.Replay(*r));
        const RecordResult& res = results.back();
        read_bytes_recorded += r->read_bytes();
        read_bytes_seeded_hit += res.own_read_seeded;
        write_bytes_compared += res.write_bytes_compared;
        write_bytes_frame += res.write_bytes_frame;
        auto& sh = shapes[r->callees];
        ++sh.first;
        if (res.pass) { ++pass_records; pass_calls += r->count; ++sh.second; }
        else { ++fail_records; fail_calls += r->count; if (!first_failure) first_failure = &results.back(); }
        if (res.uncovered) ++uncovered_records;
    }
    const auto t2 = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };

    // Report
    std::string o;
    o += "{\n";
    o += "  \"fn\": " + Json(Hex8(opt.fn)) + ",\n";
    o += "  \"corpus\": " + Json(opt.corpus) + ",\n";
    o += "  \"candidate\": " + (opt.candidate.empty() ? std::string("null") : Json(opt.candidate)) + ",\n";
    o += "  \"mode\": " + Json(opt.candidate.empty() ? "identity" : "candidate") + ",\n";
    o += "  \"callees\": " + Json(opt.callees == CalleeMode::Auto ? "auto" : opt.callees == CalleeMode::Recorded ? "recorded" : "original") + ",\n";
    o += "  \"ignore_v1\": " + std::string(opt.ignore_v1 ? "true" : "false") + ",\n";
    o += "  \"v1_entry\": " + Json(opt.seed_v1 ? "seed" : "zero") + ", \"v0_entry\": " + Json(opt.seed_v0 ? "seed" : "zero") + ",\n";
    o += "  \"lenient_callees\": " + std::string(opt.lenient_callees ? "true" : "false") + ",\n";
    o += "  \"records\": " + std::to_string(d.targets.size()) + ",\n";
    o += "  \"calls\": " + std::to_string(pass_calls + fail_calls) + ",\n";
    o += "  \"pass\": " + std::to_string(pass_records) + ",\n";
    o += "  \"fail\": " + std::to_string(fail_records) + ",\n";
    o += "  \"pass_calls\": " + std::to_string(pass_calls) + ",\n";
    o += "  \"fail_calls\": " + std::to_string(fail_calls) + ",\n";
    o += "  \"uncovered_records\": " + std::to_string(uncovered_records) + ",\n";
    o += "  \"verdict\": " + Json(d.targets.empty() ? "no records" : fail_records ? "fail" : "pass") + ",\n";
    auto rec_json = [&](const RecordResult& r) {
        std::string j = "{\"index\": " + std::to_string(r.index) + ", \"frame\": " + std::to_string(r.frame) +
                        ", \"count\": " + std::to_string(r.count) + ", \"pass\": " + (r.pass ? "true" : "false");
        j += ", \"args\": [";
        for (size_t i = 0; i < r.args.size() && i < 4; ++i) { if (i) j += ", "; j += Json(Hex(r.args[i])); }
        j += "]";
        if (!r.reason.empty()) j += ", \"reason\": " + Json(r.reason);
        if (r.has_addr) {
            j += ", \"addr\": " + Json(Hex8(r.addr));
            char b[64];
            std::snprintf(b, sizeof b, ", \"expected\": \"0x%02x\", \"got\": \"0x%02x\"", r.expected, r.got);
            j += b;
        }
        j += ", \"uncovered_reads\": " + std::to_string(r.uncovered);
        if (r.has_uncovered_first) j += ", \"uncovered_first\": " + Json(Hex8(r.uncovered_first));
        j += ", \"callees_seen\": " + HexQ(r.callees_seen);
        if (!r.syscalls.empty()) j += ", \"syscalls\": " + HexQ(r.syscalls);
        if (!r.callees_unmatched.empty()) j += ", \"callees_unmatched\": " + HexQ(r.callees_unmatched);
        if (r.vu0) j += ", \"vu0_calls\": " + std::to_string(r.vu0);
        j += ", \"own_read_bytes\": " + std::to_string(r.own_read_bytes) +
             ", \"write_bytes_compared\": " + std::to_string(r.write_bytes_compared) + "}";
        return j;
    };
    if (first_failure) o += "  \"first_failure\": " + rec_json(*first_failure) + ",\n";
    else o += "  \"first_failure\": null,\n";
    o += "  \"coverage\": {\n";
    o += "    \"read_bytes_recorded\": " + std::to_string(read_bytes_recorded) + ",\n";
    o += "    \"read_bytes_hit\": " + std::to_string(read_bytes_seeded_hit) + ",\n";
    o += "    \"write_bytes_compared\": " + std::to_string(write_bytes_compared) + ",\n";
    o += "    \"write_bytes_in_own_frame\": " + std::to_string(write_bytes_frame) + ",\n";
    o += "    \"callee_shapes\": [";
    bool first = true;
    for (const auto& [seq, c] : shapes) {
        if (!first) o += ", ";
        first = false;
        o += "{\"callees\": " + HexQ(seq) + ", \"records\": " + std::to_string(c.first) + ", \"pass\": " + std::to_string(c.second) + "}";
    }
    o += "],\n";
    o += "    \"callee_calls\": {\"recorded\": " + std::to_string(d.callee_recorded) + ", \"original\": " + std::to_string(d.callee_original) +
         ", \"ambiguous\": " + std::to_string(d.callee_ambiguous) + ", \"unmatched\": " + std::to_string(d.callee_unmatched) + "}\n";
    o += "  },\n";
    if (opt.verbose) {
        o += "  \"per_record\": [\n";
        size_t n = 0;
        for (int want_fail = 1; want_fail >= 0 && n < opt.verbose; --want_fail)
            for (const auto& r : results) {
                if (n >= opt.verbose) break;
                if ((r.pass ? 0 : 1) != want_fail) continue;
                o += (n ? ",\n    " : "    ") + rec_json(r);
                ++n;
            }
        o += "\n  ],\n";
    }
    o += "  \"timing_ms\": {\"load\": " + std::to_string(ms(t0, t1)) + ", \"replay\": " + std::to_string(ms(t1, t2)) + "}\n";
    o += "}\n";
    if (!opt.report.empty()) {
        FILE* f = std::fopen(opt.report.c_str(), "w");
        if (!f) { std::fprintf(stderr, "ps2r_replay_host: cannot write %s\n", opt.report.c_str()); return 2; }
        std::fwrite(o.data(), 1, o.size(), f);
        std::fclose(f);
    } else {
        std::fwrite(o.data(), 1, o.size(), stdout);
    }
    return fail_records ? 1 : 0;
}
