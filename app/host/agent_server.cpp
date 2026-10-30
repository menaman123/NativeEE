#include "app/host/agent_server.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "engine/core/image/png.h"
#include "engine/ee/cpu/access_tracer.h"
#include "engine/ee/cpu/cpu_state.h"

namespace ps2r::app {

namespace {

constexpr uint32_t kSpadBase = 0x70000000u;
constexpr uint32_t kMaxReadBytes = 16u << 20;
constexpr uint32_t kMaxSearchHits = 100000;

const char* const kFeatures[] = {
    "session.info", "run.step", "run.resume", "run.pause", "run.until", "pad.set", "pad.press", "state.save", "state.load",
    "state.probe", "screen.grab", "gs.frame", "vram.read", "mem.read", "mem.write", "mem.search", "mem.watch.add",
    "mem.watch.remove", "cpu.regs", "cpu.stack", "fn.break.add", "fn.break.remove", "fn.trace", "fn.census", "fn.calls.recent",
    "corpus.record", "knobs.set",
};
const char* const kUnavailableOps[] = {"alloc.log"};
constexpr uint64_t kSaveMaxFramesDefault = 300;

struct ButtonName { const char* name; uint16_t bit; };
const ButtonName kButtons[] = {
    {"select", 0x0001}, {"l3", 0x0002}, {"r3", 0x0004}, {"start", 0x0008}, {"up", 0x0010}, {"right", 0x0020},
    {"down", 0x0040}, {"left", 0x0080}, {"l2", 0x0100}, {"r2", 0x0200}, {"l1", 0x0400}, {"r1", 0x0800},
    {"triangle", 0x1000}, {"circle", 0x2000}, {"cross", 0x4000}, {"square", 0x8000},
};

const char* const kGprNames[32] = {"zero", "at", "v0", "v1", "a0", "a1", "a2", "a3", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
                                   "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"};

}  // namespace

const char* AgentServer::PrimName(uint8_t prim) {
    switch (prim) {
        case 0: return "point";
        case 1: return "line";
        case 2: return "line_strip";
        case 3: return "triangle";
        case 4: return "triangle_strip";
        case 5: return "triangle_fan";
        case 6: return "sprite";
        default: return "reserved";
    }
}

const char* const* AgentServer::Features(size_t* count) {
    *count = sizeof(kFeatures) / sizeof(kFeatures[0]);
    return kFeatures;
}

AgentServer::AgentServer(Bindings bindings) : b_(std::move(bindings)) {}

AgentServer::~AgentServer() { Close(); }

bool AgentServer::Open(const std::string& socket_path, int tcp_port, std::string* error) {
    if (!channel_.Open(socket_path, tcp_port, error)) return false;
    open_.store(true);
    return true;
}

void AgentServer::Close() {
    if (!open_.exchange(false)) return;
    if (observer_slot_ != UINT32_MAX) {
        FnHooks::RemoveObserver(observer_slot_);
        observer_slot_ = UINT32_MAX;
    }
    for (const auto& [id, b] : breaks_) FnHooks::Disarm(b.addr, FnHooks::kBreak);
    breaks_.clear();
    channel_.Close();
}

// --- helpers ------------------------------------------------------------------------------------------

std::string AgentServer::Hex(uint64_t v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(v));
    return buf;
}

std::string AgentServer::Base64Encode(const uint8_t* bytes, size_t n) {
    static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((n + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        const uint32_t v = (uint32_t(bytes[i]) << 16) | (uint32_t(bytes[i + 1]) << 8) | bytes[i + 2];
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(kAlphabet[(v >> 6) & 63]);
        out.push_back(kAlphabet[v & 63]);
    }
    if (i < n) {
        uint32_t v = uint32_t(bytes[i]) << 16;
        if (i + 1 < n) v |= uint32_t(bytes[i + 1]) << 8;
        out.push_back(kAlphabet[(v >> 18) & 63]);
        out.push_back(kAlphabet[(v >> 12) & 63]);
        out.push_back(i + 1 < n ? kAlphabet[(v >> 6) & 63] : '=');
        out.push_back('=');
    }
    return out;
}

bool AgentServer::Base64Decode(const std::string& text, std::vector<uint8_t>* out) {
    out->clear();
    uint32_t acc = 0;
    int bits = 0;
    for (const char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        else return false;
        acc = (acc << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out->push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
        }
    }
    return true;
}

bool AgentServer::ParseButtons(const AgentJson* arr, uint16_t* out, std::string* err) {
    *out = 0;
    if (!arr) return true;
    if (!arr->IsArray()) { *err = "buttons must be an array of names"; return false; }
    for (const AgentJson& item : arr->Items()) {
        if (!item.IsString()) { *err = "buttons must be an array of names"; return false; }
        bool found = false;
        for (const ButtonName& b : kButtons)
            if (item.AsString() == b.name) { *out |= b.bit; found = true; break; }
        if (!found) { *err = "unknown button '" + item.AsString() + "'"; return false; }
    }
    return true;
}

bool AgentServer::GetU32(const AgentJson& body, const char* key, uint32_t* out, bool required, std::string* err) {
    const AgentJson* v = body.Get(key);
    if (!v || v->IsNull()) {
        if (required) { *err = std::string("missing '") + key + "'"; return false; }
        return true;
    }
    uint64_t u = 0;
    if (!v->AsUInt64(&u) || u > 0xFFFFFFFFull) { *err = std::string("'") + key + "' must be a non-negative integer (or a 0x hex string)"; return false; }
    *out = static_cast<uint32_t>(u);
    return true;
}

uint8_t* AgentServer::Resolve(uint32_t addr, uint32_t len) const {
    const Memory& m = b_.memory;
    if (m.ram && addr < m.ram_size && len <= m.ram_size - addr) return m.ram + addr;
    if (m.spad && addr >= kSpadBase && addr - kSpadBase < m.spad_size && len <= m.spad_size - (addr - kSpadBase))
        return m.spad + (addr - kSpadBase);
    return nullptr;
}

void AgentServer::Reply(const Request& r, AgentJson body) {
    AgentJson msg = AgentJson::Object();
    if (r.has_id) msg.Set("id", AgentJson::Int(r.id));
    msg.Set("ok", AgentJson::Bool(true));
    for (size_t i = 0; i < body.Keys().size(); ++i) msg.Set(body.Keys()[i], body.Items()[i]);
    ++replies_;
    channel_.Send(r.connection, msg.Dump());
}

void AgentServer::Fail(const Request& r, const std::string& error) {
    AgentJson msg = AgentJson::Object();
    if (r.has_id) msg.Set("id", AgentJson::Int(r.id));
    msg.Set("ok", AgentJson::Bool(false));
    msg.Set("error", AgentJson::String(error));
    ++errors_;
    channel_.Send(r.connection, msg.Dump());
}

void AgentServer::Event(AgentJson body) {
    ++events_;
    channel_.Broadcast(body.Dump());
}

// --- the boundary -------------------------------------------------------------------------------------

void AgentServer::CheckWatches(uint64_t frame) {
    for (auto& [id, w] : watches_) {
        const uint8_t* p = Resolve(w.addr, w.len);
        if (!p) continue;
        if (w.last.size() == w.len && std::memcmp(w.last.data(), p, w.len) == 0) continue;
        const bool first = w.last.size() != w.len;
        w.last.assign(p, p + w.len);
        if (first) continue;   // the first boundary only takes the baseline
        AgentJson ev = AgentJson::Object();
        ev.Set("event", AgentJson::String("watch"));
        ev.Set("id", AgentJson::UInt(id));
        ev.Set("addr", AgentJson::UInt(w.addr));
        ev.Set("kind", AgentJson::String("w"));
        if (w.len <= 8) {
            uint64_t v = 0;
            for (uint32_t i = 0; i < w.len; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
            ev.Set("value", AgentJson::UInt(v));
        } else {
            ev.Set("value", AgentJson::String(Base64Encode(p, w.len)));
        }
        ev.Set("len", AgentJson::UInt(w.len));
        ev.Set("pc", AgentJson::Null());
        ev.Set("fn", AgentJson::Null());
        ev.Set("frame", AgentJson::UInt(frame));
        Event(ev);
        if (run_.kind == RunKind::Until && run_.watch == id && !stop_reason_) {
            stop_reason_ = "watch";
            stop_detail_ = AgentJson::Object();
            stop_detail_.Set("watch_id", AgentJson::UInt(id));
            stop_detail_.Set("addr", AgentJson::UInt(w.addr));
        }
    }
}

void AgentServer::FinishRun(const char* reason, const AgentJson* detail) {
    if (run_.kind == RunKind::None) return;
    const Run run = run_;
    run_ = Run{};
    press_.active = false;
    paused_ = true;
    if (run.fn_armed) DisarmBreakIfUnused(run.fn_target);
    Request r;
    r.connection = run.connection;
    r.id = run.id;
    r.has_id = true;
    if (run.kind == RunKind::Save) {
        // The deferred save did not reach an admissible boundary.
        Fail(r, std::string("state.save: ") + reason + " at frame " + std::to_string(frame_) + " before an admissible boundary (requested at frame " +
                    std::to_string(run.save_requested) + "; the refusal there: " + run.save_why + ")");
    } else if (run.kind != RunKind::Resume) {
        AgentJson body = AgentJson::Object();
        body.Set("frame", AgentJson::UInt(frame_));
        body.Set("reason", AgentJson::String(reason));
        if (run.kind == RunKind::Until) body.Set("detail", detail ? *detail : AgentJson::Null());
        if (mid_frame_) body.Set("mid_frame", AgentJson::Bool(true));
        Reply(r, body);
    }
    AgentJson ev = AgentJson::Object();
    ev.Set("event", AgentJson::String("stopped"));
    ev.Set("frame", AgentJson::UInt(frame_));
    ev.Set("reason", AgentJson::String(reason));
    if (mid_frame_) ev.Set("mid_frame", AgentJson::Bool(true));
    Event(ev);
    for (const Request& p : pause_requests_) {
        AgentJson body = AgentJson::Object();
        body.Set("frame", AgentJson::UInt(frame_));
        Reply(p, body);
    }
    pause_requests_.clear();
    pause_requested_ = false;
}

bool AgentServer::OnBoundary(uint64_t frame) {
    if (!open_.load()) return true;
    ++boundaries_;
    frame_ = frame;
    stop_reason_ = nullptr;
    CheckWatches(frame);
    if (!started_) {
        // The first boundary: paused here until the first run op.
        started_ = true;
        paused_ = true;
        AgentJson ev = AgentJson::Object();
        ev.Set("event", AgentJson::String("stopped"));
        ev.Set("frame", AgentJson::UInt(frame_));
        ev.Set("reason", AgentJson::String("start"));
        Event(ev);
    }
    // Requests that arrived while the frame ran (a run.pause among them stops the run here).
    if (!DrainQueue(false)) return false;
    if (run_.kind == RunKind::Save) {
        if (pause_requested_) FinishRun("paused", nullptr);
        else TrySave();
    } else if (run_.kind != RunKind::None) {
        if (stop_reason_) FinishRun(stop_reason_, &stop_detail_);
        else if (pause_requested_) FinishRun("paused", nullptr);
        else if (run_.has_target && frame_ >= run_.target) FinishRun(run_.target_reason, nullptr);
    }
    // Paused: wait here for the run op that advances the game.
    while (paused_ && open_.load()) {
        ++waits_;
        if (!DrainQueue(true)) return false;
    }
    return true;
}

void AgentServer::OnRestored(uint64_t frame, const EmotionEngineState* ctx) {
    if (!open_.load()) return;
    ctx_ = ctx;
    frame_ = frame;
    stop_reason_ = nullptr;
    run_ = Run{};
    press_.active = false;
    paused_ = true;
    for (auto& [id, w] : watches_) w.last.clear();   // the restored memory is the new baseline
    CheckWatches(frame);
    if (load_pending_) {
        ++loads_;
        load_pending_ = false;
        AgentJson body = AgentJson::Object();
        body.Set("frame", AgentJson::UInt(frame_));
        body.Set("path", AgentJson::String(load_path_));
        Reply(load_request_, body);
        AgentJson ev = AgentJson::Object();
        ev.Set("event", AgentJson::String("stopped"));
        ev.Set("frame", AgentJson::UInt(frame_));
        ev.Set("reason", AgentJson::String("load"));
        Event(ev);
    }
    while (paused_ && open_.load()) {
        ++waits_;
        // A second load admitted right here (before any instruction) would restart again; allowed.
        if (!DrainQueue(true)) return;
    }
}

void AgentServer::RestoreFailed(const std::string& error) {
    if (!load_pending_) return;
    load_pending_ = false;
    Fail(load_request_, "state.load: the live restore failed after unwind: " + error);
}

void AgentServer::ApplyPad(uint64_t stamp, Pad* io) {
    if (!open_.load()) return;
    io->buttons |= pad_.buttons;
    if (pad_.lx != 0x80 || pad_.ly != 0x80) { io->lx = pad_.lx; io->ly = pad_.ly; }
    if (pad_.rx != 0x80 || pad_.ry != 0x80) { io->rx = pad_.rx; io->ry = pad_.ry; }
    if (press_.active && stamp >= press_.first_stamp && stamp <= press_.last_stamp) io->buttons |= press_.buttons;
}

bool AgentServer::DrainQueue(bool block) {
    AgentChannel::Request raw;
    for (;;) {
        const bool got = block ? channel_.Wait(&raw, std::chrono::milliseconds(200)) : channel_.Poll(&raw);
        if (!got) {
            if (block && paused_ && open_.load()) continue;   // keep waiting (a client may connect later)
            return true;
        }
        ++requests_;
        Request r;
        r.connection = raw.connection;
        std::string err;
        if (!AgentJson::Parse(raw.line, &r.body, &err) || !r.body.IsObject()) {
            Fail(r, "request is not a JSON object: " + err);
            continue;
        }
        if (const AgentJson* id = r.body.Get("id")) { r.has_id = id->AsInt64(&r.id); }
        const AgentJson* op = r.body.Get("op");
        if (!op || !op->IsString()) { Fail(r, "request has no 'op' string"); continue; }
        r.op = op->AsString();
        if (!Handle(r)) return false;
        // A run op started: leave the wait so the game advances.
        if (block && !paused_) return true;
    }
}

bool AgentServer::StartRun(const Request& r, RunKind kind, bool has_target, uint64_t target, const char* target_reason, uint64_t watch) {
    if (run_.kind != RunKind::None) {
        Fail(r, "the game is already running (another run op or run.resume is in flight); send run.pause first");
        return false;
    }
    if (load_pending_) { Fail(r, "a state.load is in flight"); return false; }
    run_.kind = kind;
    run_.connection = r.connection;
    run_.id = r.id;
    run_.has_target = has_target;
    run_.target = target;
    run_.target_reason = target_reason;
    run_.watch = watch;
    pause_requested_ = false;
    paused_ = false;
    if (kind == RunKind::Resume) {
        AgentJson body = AgentJson::Object();
        body.Set("frame", AgentJson::UInt(frame_));
        Reply(r, body);
    }
    return true;
}

bool AgentServer::Handle(const Request& r) {
    const std::string& op = r.op;
    const AgentJson& body = r.body;
    std::string err;
    if (op == "session.info") { OpSessionInfo(r); return true; }
    if (op == "run.step") {
        uint64_t n = 1;
        if (const AgentJson* f = body.Get("frames")) {
            if (!f->AsUInt64(&n) || n < 1) { Fail(r, "frames must be an integer >= 1"); return true; }
        }
        StartRun(r, RunKind::Step, true, frame_ + n, "step", 0);
        return true;
    }
    if (op == "run.resume") { StartRun(r, RunKind::Resume, false, 0, "", 0); return true; }
    if (op == "run.pause") {
        if (run_.kind == RunKind::None) {
            AgentJson out = AgentJson::Object();
            out.Set("frame", AgentJson::UInt(frame_));
            Reply(r, out);
        } else {
            pause_requested_ = true;
            pause_requests_.push_back(r);
        }
        return true;
    }
    if (op == "run.until") {
        bool has_target = false;
        uint64_t target = 0, watch = 0;
        uint32_t fn_target = 0;
        bool fn_exit = false;
        const char* reason = "frame";
        if (body.Has("fn_enter") || body.Has("fn_exit")) {
            if (body.Has("fn_enter") && body.Has("fn_exit")) { Fail(r, "run.until takes fn_enter or fn_exit, not both"); return true; }
            fn_exit = body.Has("fn_exit");
            if (!GetU32(body, fn_exit ? "fn_exit" : "fn_enter", &fn_target, true, &err)) { Fail(r, err); return true; }
            if (FnHooks::IdOf(fn_target) == UINT32_MAX) { Fail(r, Hex(fn_target) + " is not a recompiled function's entry"); return true; }
        }
        if (const AgentJson* f = body.Get("frame")) {
            if (!f->AsUInt64(&target)) { Fail(r, "frame must be an integer"); return true; }
            has_target = true;
            reason = "frame";
        }
        if (const AgentJson* m = body.Get("max_frames")) {
            uint64_t n = 0;
            if (!m->AsUInt64(&n)) { Fail(r, "max_frames must be an integer"); return true; }
            if (!has_target || frame_ + n < target) { target = frame_ + n; reason = "max_frames"; }
            has_target = true;
        }
        if (const AgentJson* w = body.Get("watch")) {
            if (!w->AsUInt64(&watch) || watches_.find(watch) == watches_.end()) { Fail(r, "no such watch (mem.watch.add first)"); return true; }
        }
        if (!has_target && watch == 0 && fn_target == 0) { Fail(r, "run.until needs frame, max_frames, watch, fn_enter or fn_exit"); return true; }
        if (has_target && target <= frame_) {
            // Already there: answer at once without advancing.
            AgentJson out = AgentJson::Object();
            out.Set("frame", AgentJson::UInt(frame_));
            out.Set("reason", AgentJson::String(reason));
            out.Set("detail", AgentJson::Null());
            Reply(r, out);
            return true;
        }
        if (StartRun(r, RunKind::Until, has_target, target, reason, watch) && fn_target) {
            run_.fn_target = fn_target;
            run_.fn_exit = fn_exit;
            run_.fn_armed = true;
            ArmBreak(fn_target);
        }
        return true;
    }
    if (op == "pad.set") { OpPadSet(r); return true; }
    if (op == "pad.press") {
        uint16_t buttons = 0;
        if (!body.Has("buttons")) { Fail(r, "missing 'buttons'"); return true; }
        if (!ParseButtons(body.Get("buttons"), &buttons, &err)) { Fail(r, err); return true; }
        uint64_t n = 2;
        if (const AgentJson* f = body.Get("frames")) {
            if (!f->AsUInt64(&n) || n < 1) { Fail(r, "frames must be an integer >= 1"); return true; }
        }
        if (StartRun(r, RunKind::Press, true, frame_ + n, "press", 0)) {
            press_.active = true;
            press_.buttons = buttons;
            press_.first_stamp = frame_ + 1;
            press_.last_stamp = frame_ + n;
        }
        return true;
    }
    if (op == "state.save") { OpStateSave(r); return true; }
    if (op == "state.probe") { OpStateProbe(r); return true; }
    if (op == "state.load") return OpStateLoad(r);
    if (op == "cpu.stack") { OpCpuStack(r); return true; }
    if (op == "fn.break.add") { OpBreakAdd(r); return true; }
    if (op == "fn.break.remove") { OpBreakRemove(r); return true; }
    if (op == "fn.trace") { OpFnTrace(r); return true; }
    if (op == "fn.census") { OpFnCensus(r); return true; }
    if (op == "fn.calls.recent") { OpCallsRecent(r); return true; }
    if (op == "corpus.record") { OpCorpusRecord(r); return true; }
    if (op == "screen.grab") { OpScreenGrab(r); return true; }
    if (op == "gs.frame") { OpGsFrame(r); return true; }
    if (op == "vram.read") { OpVramRead(r); return true; }
    if (op == "mem.read") { OpMemRead(r); return true; }
    if (op == "mem.write") { OpMemWrite(r); return true; }
    if (op == "mem.search") { OpMemSearch(r); return true; }
    if (op == "mem.watch.add") { OpWatchAdd(r); return true; }
    if (op == "mem.watch.remove") { OpWatchRemove(r); return true; }
    if (op == "cpu.regs") { OpCpuRegs(r); return true; }
    if (op == "knobs.set") { OpKnobsSet(r); return true; }
    for (const char* t : kUnavailableOps)
        if (op == t) { Fail(r, "not available in this build: " + op + " needs a named allocator hook (none is)"); return true; }
    Fail(r, "unknown op '" + op + "'");
    return true;
}

// --- ops ----------------------------------------------------------------------------------------------

void AgentServer::OpSessionInfo(const Request& r) {
    AgentJson out = AgentJson::Object();
    out.Set("title", AgentJson::String(b_.title));
    out.Set("elf_sha256", AgentJson::String(b_.elf_sha256));
    out.Set("frame", AgentJson::UInt(frame_));
    out.Set("paused", AgentJson::Bool(paused_));
    out.Set("build", AgentJson::String(b_.build));
    AgentJson features = AgentJson::Array();
    for (const char* f : kFeatures) features.Push(AgentJson::String(f));
    out.Set("features", features);
    out.Set("protocol", AgentJson::Int(1));
    out.Set("frame_clock", AgentJson::String("boundary ordinal (0-based) = pad stamp = padlog clock"));
    if (b_.extra_info) b_.extra_info(&out);
    Reply(r, out);
}

void AgentServer::OpPadSet(const Request& r) {
    std::string err;
    Pad p;
    if (!ParseButtons(r.body.Get("buttons"), &p.buttons, &err)) { Fail(r, err); return; }
    uint8_t* axes[4] = {&p.lx, &p.ly, &p.rx, &p.ry};
    const char* names[4] = {"lx", "ly", "rx", "ry"};
    for (int i = 0; i < 4; ++i) {
        uint32_t v = 0x80;
        if (!GetU32(r.body, names[i], &v, false, &err)) { Fail(r, err); return; }
        if (v > 255) { Fail(r, std::string(names[i]) + " must be 0..255"); return; }
        *axes[i] = static_cast<uint8_t>(v);
    }
    pad_ = p;
    AgentJson out = AgentJson::Object();
    out.Set("frame", AgentJson::UInt(frame_));
    Reply(r, out);
}

void AgentServer::OpStateSave(const Request& r) {
    if (!b_.state_save) { Fail(r, "not available in this build: state.save has no savestate owner"); return; }
    if (mid_frame_) { Fail(r, "state.save needs a frame boundary: the game is stopped inside a function hook (run.step first)"); return; }
    if (run_.kind != RunKind::None) { Fail(r, "the game is running; run.pause first"); return; }
    if (load_pending_) { Fail(r, "a state.load is in flight"); return; }
    std::string path, out_path, err, why;
    int slot = -1;
    if (const AgentJson* p = r.body.Get("path")) { if (p->IsString()) path = p->AsString(); }
    if (const AgentJson* s = r.body.Get("slot")) { int64_t v = 0; if (s->AsInt64(&v)) slot = static_cast<int>(v); }
    uint64_t max_frames = kSaveMaxFramesDefault;
    if (const AgentJson* m = r.body.Get("max_frames")) { if (!m->AsUInt64(&max_frames)) { Fail(r, "max_frames must be an integer"); return; } }
    const bool admissible = !b_.state_save_admissible || b_.state_save_admissible(&why);
    if (admissible) {
        if (!b_.state_save(path, slot, &out_path, &err)) { Fail(r, "state.save: " + err); return; }
        ++saves_;
        AgentJson out = AgentJson::Object();
        out.Set("path", AgentJson::String(out_path));
        out.Set("frame", AgentJson::UInt(frame_));
        out.Set("requested_frame", AgentJson::UInt(frame_));
        Reply(r, out);
        return;
    }
    if (max_frames == 0) { Fail(r, "state.save: this boundary is not admissible (" + why + ") and max_frames is 0"); return; }
    // Drive the game to the next admissible boundary (TrySave at every OnBoundary).
    if (!StartRun(r, RunKind::Save, true, frame_ + max_frames, "max_frames", 0)) return;
    run_.save_path = path;
    run_.save_slot = slot;
    run_.save_requested = frame_;
    run_.save_why = why;
    ++save_deferrals_;
}

bool AgentServer::TrySave() {
    std::string why, out_path, err;
    const Run run = run_;
    if (b_.state_save_admissible && !b_.state_save_admissible(&why)) {
        if (frame_ >= run.target) { FinishRun("max_frames", nullptr); return true; }
        return false;   // keep stepping
    }
    run_ = Run{};
    paused_ = true;
    Request r;
    r.connection = run.connection;
    r.id = run.id;
    r.has_id = true;
    if (!b_.state_save(run.save_path, run.save_slot, &out_path, &err)) {
        Fail(r, "state.save: " + err + " (at frame " + std::to_string(frame_) + ", deferred from " + std::to_string(run.save_requested) + ")");
    } else {
        ++saves_;
        AgentJson out = AgentJson::Object();
        out.Set("path", AgentJson::String(out_path));
        out.Set("frame", AgentJson::UInt(frame_));
        out.Set("requested_frame", AgentJson::UInt(run.save_requested));
        out.Set("deferred_from", AgentJson::String(run.save_why));
        Reply(r, out);
    }
    AgentJson ev = AgentJson::Object();
    ev.Set("event", AgentJson::String("stopped"));
    ev.Set("frame", AgentJson::UInt(frame_));
    ev.Set("reason", AgentJson::String("save"));
    Event(ev);
    for (const Request& p : pause_requests_) {
        AgentJson body = AgentJson::Object();
        body.Set("frame", AgentJson::UInt(frame_));
        Reply(p, body);
    }
    pause_requests_.clear();
    pause_requested_ = false;
    return true;
}

void AgentServer::OpStateProbe(const Request& r) {
    AgentJson out = AgentJson::Object();
    out.Set("frame", AgentJson::UInt(frame_));
    if (mid_frame_) {
        out.Set("admissible", AgentJson::Bool(false));
        out.Set("reason", AgentJson::String("stopped inside a function hook, not at a boundary"));
    } else if (!b_.state_save_admissible) {
        out.Set("admissible", AgentJson::Bool(true));
        out.Set("reason", AgentJson::Null());
    } else {
        std::string why;
        const bool ok = b_.state_save_admissible(&why);
        out.Set("admissible", AgentJson::Bool(ok));
        out.Set("reason", ok ? AgentJson::Null() : AgentJson::String(why));
    }
    Reply(r, out);
}

bool AgentServer::OpStateLoad(const Request& r) {
    if (!b_.state_load_prepare) { Fail(r, "not available in this build: state.load has no restore owner"); return true; }
    if (mid_frame_) { Fail(r, "state.load needs a frame boundary: the game is stopped inside a function hook (run.step first)"); return true; }
    if (run_.kind != RunKind::None) { Fail(r, "the game is running; run.pause first"); return true; }
    std::string path, resolved, err;
    int slot = -1;
    if (const AgentJson* p = r.body.Get("path")) { if (p->IsString()) path = p->AsString(); }
    if (const AgentJson* s = r.body.Get("slot")) { int64_t v = 0; if (s->AsInt64(&v)) slot = static_cast<int>(v); }
    if (!b_.state_load_prepare(path, slot, &resolved, &err)) { Fail(r, "state.load: " + err); return true; }
    load_pending_ = true;
    load_request_ = r;
    load_path_ = resolved;
    return false;   // unwind the boundary owner; OnRestored answers
}

void AgentServer::OpScreenGrab(const Request& r) {
    if (!b_.grab) { Fail(r, "not available in this build: no display grab"); return; }
    Grab g;
    std::string err;
    if (!b_.grab(&g, &err)) { Fail(r, "screen.grab: " + err); return; }
    if (g.w <= 0 || g.h <= 0 || g.rgba.size() < static_cast<size_t>(g.w) * static_cast<size_t>(g.h) * 4) {
        Fail(r, "screen.grab: no presented frame yet");
        return;
    }
    std::string format = "png";
    if (const AgentJson* f = r.body.Get("format")) { if (f->IsString()) format = f->AsString(); }
    if (format != "png" && format != "ppm") { Fail(r, "format must be png or ppm"); return; }
    double scale = 1.0;
    if (const AgentJson* s = r.body.Get("scale")) { if (s->IsNumber()) scale = s->AsDouble(); }
    if (scale < 0.05 || scale > 1.0) { Fail(r, "scale must be in 0.05..1"); return; }
    int w = g.w, h = g.h;
    const uint8_t* src = g.rgba.data();
    std::vector<uint8_t> scaled;
    if (scale < 0.999) {
        w = std::max(1, static_cast<int>(g.w * scale + 0.5));
        h = std::max(1, static_cast<int>(g.h * scale + 0.5));
        scaled.resize(static_cast<size_t>(w) * h * 4);
        // Box filter over the source cells each destination pixel covers.
        for (int y = 0; y < h; ++y) {
            const int sy0 = y * g.h / h, sy1 = std::max(sy0 + 1, (y + 1) * g.h / h);
            for (int x = 0; x < w; ++x) {
                const int sx0 = x * g.w / w, sx1 = std::max(sx0 + 1, (x + 1) * g.w / w);
                uint32_t acc[4] = {0, 0, 0, 0}, n = 0;
                for (int sy = sy0; sy < sy1; ++sy)
                    for (int sx = sx0; sx < sx1; ++sx) {
                        const uint8_t* p = g.rgba.data() + (static_cast<size_t>(sy) * g.w + sx) * 4;
                        for (int c = 0; c < 4; ++c) acc[c] += p[c];
                        ++n;
                    }
                uint8_t* d = scaled.data() + (static_cast<size_t>(y) * w + x) * 4;
                for (int c = 0; c < 4; ++c) d[c] = static_cast<uint8_t>(acc[c] / n);
            }
        }
        src = scaled.data();
    }
    std::vector<uint8_t> bytes;
    if (format == "png") {
        bytes = ps2r::image::Png::Encode(src, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
        if (bytes.empty()) { Fail(r, "screen.grab: PNG encode failed"); return; }
    } else {
        char header[64];
        const int n = std::snprintf(header, sizeof(header), "P6\n%d %d\n255\n", w, h);
        bytes.assign(header, header + n);
        bytes.reserve(bytes.size() + static_cast<size_t>(w) * h * 3);
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
            bytes.push_back(src[i * 4]);
            bytes.push_back(src[i * 4 + 1]);
            bytes.push_back(src[i * 4 + 2]);
        }
    }
    AgentJson out = AgentJson::Object();
    out.Set("width", AgentJson::Int(w));
    out.Set("height", AgentJson::Int(h));
    out.Set("format", AgentJson::String(format));
    out.Set("data_b64", AgentJson::String(Base64Encode(bytes.data(), bytes.size())));
    out.Set("frame", AgentJson::UInt(frame_));
    Reply(r, out);
}

void AgentServer::OpGsFrame(const Request& r) {
    AgentJson out = AgentJson::Object();
    out.Set("frame", AgentJson::UInt(frame_));
    out.Set("draws", b_.draws ? b_.draws() : AgentJson::Array());
    Reply(r, out);
}

void AgentServer::OpVramRead(const Request& r) {
    std::string err;
    uint32_t addr = 0, len = 0;
    if (!GetU32(r.body, "addr", &addr, true, &err) || !GetU32(r.body, "len", &len, true, &err)) { Fail(r, err); return; }
    if (!b_.memory.vram) { Fail(r, "not available in this build: no GS local memory view"); return; }
    if (addr >= b_.memory.vram_size || len > b_.memory.vram_size - addr) { Fail(r, "vram range is past the 4 MB of GS local memory"); return; }
    if (len > kMaxReadBytes) { Fail(r, "len exceeds 16 MiB"); return; }
    AgentJson out = AgentJson::Object();
    out.Set("data_b64", AgentJson::String(Base64Encode(b_.memory.vram + addr, len)));
    Reply(r, out);
}

void AgentServer::OpMemRead(const Request& r) {
    std::string err;
    uint32_t addr = 0, len = 0;
    if (!GetU32(r.body, "addr", &addr, true, &err) || !GetU32(r.body, "len", &len, true, &err)) { Fail(r, err); return; }
    if (len > kMaxReadBytes) { Fail(r, "len exceeds 16 MiB"); return; }
    const uint8_t* p = Resolve(addr, len);
    if (!p) { Fail(r, "address range " + Hex(addr) + "+" + std::to_string(len) + " is outside main RAM / scratchpad"); return; }
    AgentJson out = AgentJson::Object();
    out.Set("data_b64", AgentJson::String(Base64Encode(p, len)));
    Reply(r, out);
}

void AgentServer::OpMemWrite(const Request& r) {
    std::string err;
    uint32_t addr = 0;
    if (!GetU32(r.body, "addr", &addr, true, &err)) { Fail(r, err); return; }
    const AgentJson* d = r.body.Get("data_b64");
    std::vector<uint8_t> bytes;
    if (!d || !d->IsString() || !Base64Decode(d->AsString(), &bytes)) { Fail(r, "data_b64 must be a base64 string"); return; }
    uint8_t* p = Resolve(addr, static_cast<uint32_t>(bytes.size()));
    if (!p) { Fail(r, "address range " + Hex(addr) + "+" + std::to_string(bytes.size()) + " is outside main RAM / scratchpad"); return; }
    if (!bytes.empty()) std::memcpy(p, bytes.data(), bytes.size());
    AgentJson out = AgentJson::Object();
    out.Set("len", AgentJson::UInt(bytes.size()));
    Reply(r, out);
}

void AgentServer::OpMemSearch(const Request& r) {
    std::string err;
    std::vector<uint8_t> needle;
    uint32_t align = 1;
    if (const AgentJson* bh = r.body.Get("bytes_hex")) {
        if (!bh->IsString() || bh->AsString().empty() || bh->AsString().size() % 2) { Fail(r, "bytes_hex must be an even-length hex string"); return; }
        const std::string& s = bh->AsString();
        for (size_t i = 0; i < s.size(); i += 2) {
            char* end = nullptr;
            const std::string pair = s.substr(i, 2);
            const long v = std::strtol(pair.c_str(), &end, 16);
            if (!end || *end) { Fail(r, "bytes_hex must be hex"); return; }
            needle.push_back(static_cast<uint8_t>(v));
        }
    } else if (r.body.Has("u32")) {
        uint32_t v = 0;
        if (!GetU32(r.body, "u32", &v, true, &err)) { Fail(r, err); return; }
        for (int i = 0; i < 4; ++i) needle.push_back(static_cast<uint8_t>(v >> (8 * i)));
        align = 4;
    } else if (const AgentJson* f = r.body.Get("f32")) {
        if (!f->IsNumber()) { Fail(r, "f32 must be a number"); return; }
        const float fv = static_cast<float>(f->AsDouble());
        uint32_t v = 0;
        std::memcpy(&v, &fv, 4);
        for (int i = 0; i < 4; ++i) needle.push_back(static_cast<uint8_t>(v >> (8 * i)));
        align = 4;
    } else {
        Fail(r, "mem.search needs bytes_hex, u32 or f32");
        return;
    }
    uint32_t lo = 0, hi = b_.memory.ram_size;
    if (const AgentJson* range = r.body.Get("range")) {
        if (!range->IsArray() || range->Size() != 2) { Fail(r, "range must be [lo, hi)"); return; }
        uint64_t a = 0, b = 0;
        if (!range->Items()[0].AsUInt64(&a) || !range->Items()[1].AsUInt64(&b) || a > b || b > 0xFFFFFFFFull) { Fail(r, "range must be [lo, hi) addresses"); return; }
        lo = static_cast<uint32_t>(a);
        hi = static_cast<uint32_t>(b);
    }
    uint32_t max_hits = 64;
    if (!GetU32(r.body, "max", &max_hits, false, &err)) { Fail(r, err); return; }
    if (max_hits == 0 || max_hits > kMaxSearchHits) max_hits = kMaxSearchHits;
    const uint8_t* base = Resolve(lo, hi - lo);
    if (!base) { Fail(r, "range is outside one memory (main RAM or scratchpad)"); return; }
    AgentJson hits = AgentJson::Array();
    bool truncated = false;
    const size_t n = hi - lo;
    if (needle.size() <= n) {
        for (size_t i = 0; i + needle.size() <= n; i += align) {
            if ((lo + i) % align) continue;
            if (base[i] != needle[0] || std::memcmp(base + i, needle.data(), needle.size()) != 0) continue;
            if (hits.Size() >= max_hits) { truncated = true; break; }
            hits.Push(AgentJson::UInt(lo + i));
        }
    }
    AgentJson out = AgentJson::Object();
    out.Set("hits", hits);
    out.Set("truncated", AgentJson::Bool(truncated));
    Reply(r, out);
}

void AgentServer::OpWatchAdd(const Request& r) {
    std::string err;
    uint32_t addr = 0, len = 0;
    if (!GetU32(r.body, "addr", &addr, true, &err) || !GetU32(r.body, "len", &len, true, &err)) { Fail(r, err); return; }
    if (len == 0 || len > 4096) { Fail(r, "len must be 1..4096"); return; }
    std::string kind = "w";
    if (const AgentJson* k = r.body.Get("kind")) { if (k->IsString()) kind = k->AsString(); }
    if (kind == "r") { Fail(r, "not available in this build: read watches need the trace build's accessors"); return; }
    const uint8_t* p = Resolve(addr, len);
    if (!p) { Fail(r, "address range is outside main RAM / scratchpad"); return; }
    Watch w;
    w.addr = addr;
    w.len = len;
    w.kind = kind;
    w.last.assign(p, p + len);
    const uint64_t id = next_watch_++;
    watches_[id] = std::move(w);
    AgentJson out = AgentJson::Object();
    out.Set("watch_id", AgentJson::UInt(id));
    out.Set("granularity", AgentJson::String("boundary"));
    Reply(r, out);
}

void AgentServer::OpWatchRemove(const Request& r) {
    uint64_t id = 0;
    const AgentJson* w = r.body.Get("watch_id");
    if (!w || !w->AsUInt64(&id) || watches_.erase(id) == 0) { Fail(r, "no such watch"); return; }
    Reply(r, AgentJson::Object());
}

void AgentServer::OpCpuRegs(const Request& r) {
    if (mid_frame_) {
        Fail(r, "cpu.regs: stopped inside a function hook; the hook carries a0..a3/t0..t3/sp/ra only (see the break event); "
                "the boundary owner's register file is available at the next boundary (run.step)");
        return;
    }
    if (!ctx_) { Fail(r, "cpu.regs: no boundary register file yet (step one frame first)"); return; }
    const EmotionEngineState& c = *ctx_;
    AgentJson out = AgentJson::Object();
    out.Set("pc", AgentJson::UInt(c.cpuRegs.pc));
    AgentJson gpr = AgentJson::Object();
    for (int i = 0; i < 32; ++i) {
        AgentJson pair = AgentJson::Array();
        pair.Push(AgentJson::String(Hex(c.cpuRegs.GPR.r[i].UD[0])));
        pair.Push(AgentJson::String(Hex(c.cpuRegs.GPR.r[i].UD[1])));
        gpr.Set(kGprNames[i], pair);
    }
    out.Set("gpr", gpr);
    AgentJson fpr = AgentJson::Array();
    for (int i = 0; i < 32; ++i) fpr.Push(AgentJson::Number(c.fpuRegs.fpr[i].f));
    out.Set("fpr", fpr);
    out.Set("hi", AgentJson::String(Hex(c.cpuRegs.HI.UD[0])));
    out.Set("lo", AgentJson::String(Hex(c.cpuRegs.LO.UD[0])));
    out.Set("thread", AgentJson::String("the boundary owner (the thread that called sceGsSyncV) at the completed boundary"));
    Reply(r, out);
}

void AgentServer::OpKnobsSet(const Request& r) {
    const AgentJson* name = r.body.Get("name");
    const AgentJson* value = r.body.Get("value");
    if (!name || !name->IsString() || !value) { Fail(r, "knobs.set needs name and value"); return; }
    if (!b_.knob_set) { Fail(r, "not available in this build: no run-time knobs"); return; }
    std::string err;
    if (!b_.knob_set(name->AsString(), *value, &err)) { Fail(r, "knobs.set " + name->AsString() + ": " + err); return; }
    AgentJson out = AgentJson::Object();
    out.Set("name", *name);
    out.Set("value", *value);
    Reply(r, out);
}

// --- function hooks (FnHooks, the trace branch) --------------------------------------------------------

AgentJson AgentServer::CallJson(const FnHooks::Call& call, bool with_ret) {
    AgentJson o = AgentJson::Object();
    // The control plane's frame = the last completed boundary before the call (g_vsync_frame counts
    // completed boundaries: the call ran during frame+1's work).
    o.Set("frame", AgentJson::UInt(call.frame ? call.frame - 1 : 0));
    AgentJson args = AgentJson::Array();
    for (uint32_t i = 0; i < FnHooks::kArgs; ++i) args.Push(AgentJson::String(Hex(call.args[i])));
    o.Set("args", args);
    AgentJson ret = AgentJson::Array();
    if (with_ret && !call.unwound) {
        ret.Push(AgentJson::String(Hex(call.ret[0])));
        ret.Push(AgentJson::String(Hex(call.ret[1])));
    }
    o.Set("ret", ret);
    o.Set("sp", AgentJson::UInt(call.sp));
    o.Set("ra", AgentJson::UInt(call.ra));
    o.Set("caller", call.caller ? AgentJson::UInt(call.caller) : AgentJson::Null());
    o.Set("fn", AgentJson::UInt(call.fn));
    o.Set("id", AgentJson::UInt(call.id));
    o.Set("seq", AgentJson::UInt(call.seq));
    o.Set("depth", AgentJson::UInt(call.depth));
    if (call.unwound) o.Set("unwound", AgentJson::Bool(true));
    return o;
}

AgentJson AgentServer::StackJson(const std::vector<FnHooks::Frame>& frames) {
    AgentJson arr = AgentJson::Array();
    for (const FnHooks::Frame& f : frames) {
        AgentJson o = AgentJson::Object();
        o.Set("fn", AgentJson::UInt(f.fn));
        o.Set("id", AgentJson::UInt(f.id));
        o.Set("ra", AgentJson::UInt(f.ra));
        arr.Push(o);
    }
    return arr;
}

void AgentServer::ArmBreak(uint32_t fn) {
    if (observer_slot_ == UINT32_MAX) observer_slot_ = FnHooks::AddObserver(FnHooks::kBreak, &AgentServer::ProbeObserver, this);
    FnHooks::Arm(fn, FnHooks::kBreak);
}

void AgentServer::DisarmBreakIfUnused(uint32_t fn) {
    for (const auto& [id, b] : breaks_)
        if (b.addr == fn) return;
    if (run_.fn_armed && run_.fn_target == fn) return;
    FnHooks::Disarm(fn, FnHooks::kBreak);
}

void AgentServer::ProbeObserver(void* user, const FnHooks::Call& call, bool exit) {
    static_cast<AgentServer*>(user)->ProbeHit(call, exit);
}

void AgentServer::ProbeHit(const FnHooks::Call& call, bool exit) {
    if (!open_.load() || mid_frame_) return;
    // The breakpoints on this function and edge.
    std::vector<std::pair<uint64_t, Break>> hits;
    for (const auto& [id, b] : breaks_)
        if (b.addr == call.fn && b.on_exit == exit) hits.emplace_back(id, b);
    const bool run_target = run_.kind != RunKind::None && run_.fn_target == call.fn && run_.fn_exit == exit;
    if (hits.empty() && !run_target) return;
    ++breaks_hit_;
    mid_frame_ = true;
    mid_call_ = call;
    mid_exit_ = exit;
    std::vector<FnHooks::Frame> stack;
    bool have_stack = false;
    AgentJson detail = AgentJson::Object();
    detail.Set("addr", AgentJson::UInt(call.fn));
    detail.Set("on", AgentJson::String(exit ? "exit" : "enter"));
    detail.Set("call", CallJson(call, exit));
    for (const auto& [id, b] : hits) {
        AgentJson ev = AgentJson::Object();
        ev.Set("event", AgentJson::String("break"));
        ev.Set("id", AgentJson::UInt(id));
        ev.Set("addr", AgentJson::UInt(call.fn));
        ev.Set("on", AgentJson::String(exit ? "exit" : "enter"));
        ev.Set("frame", AgentJson::UInt(frame_));
        if (b.cap_args) {
            AgentJson args = AgentJson::Array();
            for (uint32_t i = 0; i < FnHooks::kArgs; ++i) args.Push(AgentJson::String(Hex(call.args[i])));
            ev.Set("args", args);
        }
        if (b.cap_ret && exit && !call.unwound) {
            AgentJson ret = AgentJson::Array();
            ret.Push(AgentJson::String(Hex(call.ret[0])));
            ret.Push(AgentJson::String(Hex(call.ret[1])));
            ev.Set("ret", ret);
        }
        if (b.cap_stack) {
            if (!have_stack) { stack = FnHooks::Stack(); have_stack = true; }
            ev.Set("stack", StackJson(stack));
        }
        ev.Set("sp", AgentJson::UInt(call.sp));
        ev.Set("ra", AgentJson::UInt(call.ra));
        ev.Set("caller", call.caller ? AgentJson::UInt(call.caller) : AgentJson::Null());
        Event(ev);
        if (b.once) {
            breaks_.erase(id);
            DisarmBreakIfUnused(call.fn);
        }
    }
    AgentJson ids = AgentJson::Array();
    for (const auto& [id, b] : hits) ids.Push(AgentJson::UInt(id));
    detail.Set("break_ids", ids);
    if (run_.kind != RunKind::None) {
        if (run_target) FinishRun(exit ? "fn_exit" : "fn_enter", &detail);
        else FinishRun("break", &detail);
    }
    // Stopped mid-frame: serve requests here until a run op continues the game.
    while (paused_ && open_.load()) {
        ++mid_frame_waits_;
        DrainQueue(true);   // state.load is refused mid-frame, so no unwind can be admitted here
    }
    mid_frame_ = false;
}

void AgentServer::OpCpuStack(const Request& r) {
    AgentJson out = AgentJson::Object();
    out.Set("frames", StackJson(FnHooks::Stack()));
    out.Set("frame", AgentJson::UInt(frame_));
    out.Set("thread", AgentJson::String(mid_frame_ ? "the guest thread stopped in the hook (mid-frame)" : "the boundary owner at the completed boundary"));
    if (mid_frame_) out.Set("mid_frame", AgentJson::Bool(true));
    Reply(r, out);
}

void AgentServer::OpBreakAdd(const Request& r) {
    std::string err;
    uint32_t addr = 0;
    if (!GetU32(r.body, "addr", &addr, true, &err)) { Fail(r, err); return; }
    if (FnHooks::IdOf(addr) == UINT32_MAX) { Fail(r, Hex(addr) + " is not a recompiled function's entry"); return; }
    Break b;
    if (const AgentJson* on = r.body.Get("on")) {
        if (!on->IsString() || (on->AsString() != "enter" && on->AsString() != "exit")) { Fail(r, "on must be \"enter\" or \"exit\""); return; }
        b.on_exit = on->AsString() == "exit";
    }
    if (const AgentJson* cap = r.body.Get("capture")) {
        if (!cap->IsArray()) { Fail(r, "capture must be an array of args|ret|stack"); return; }
        b.cap_args = b.cap_ret = b.cap_stack = false;
        for (const AgentJson& c : cap->Items()) {
            if (!c.IsString()) { Fail(r, "capture must be an array of args|ret|stack"); return; }
            if (c.AsString() == "args") b.cap_args = true;
            else if (c.AsString() == "ret") b.cap_ret = true;
            else if (c.AsString() == "stack") b.cap_stack = true;
            else { Fail(r, "unknown capture '" + c.AsString() + "'"); return; }
        }
    }
    if (const AgentJson* once = r.body.Get("once")) { if (once->IsBool()) b.once = once->AsBool(); }
    b.addr = addr;
    const uint64_t id = next_break_++;
    breaks_[id] = b;
    ArmBreak(addr);
    AgentJson out = AgentJson::Object();
    out.Set("break_id", AgentJson::UInt(id));
    out.Set("addr", AgentJson::UInt(addr));
    out.Set("fn_id", AgentJson::UInt(FnHooks::IdOf(addr)));
    Reply(r, out);
}

void AgentServer::OpBreakRemove(const Request& r) {
    uint64_t id = 0;
    const AgentJson* b = r.body.Get("break_id");
    if (!b || !b->AsUInt64(&id)) { Fail(r, "break_id must be an integer"); return; }
    auto it = breaks_.find(id);
    if (it == breaks_.end()) { Fail(r, "no such breakpoint"); return; }
    const uint32_t fn = it->second.addr;
    breaks_.erase(it);
    DisarmBreakIfUnused(fn);
    Reply(r, AgentJson::Object());
}

void AgentServer::OpFnTrace(const Request& r) {
    const AgentJson* addrs = r.body.Get("addrs");
    if (!addrs || !addrs->IsArray()) { Fail(r, "addrs must be an array of function addresses"); return; }
    bool on = true;
    if (const AgentJson* o = r.body.Get("on")) { if (o->IsBool()) on = o->AsBool(); }
    std::vector<uint32_t> fns;
    for (const AgentJson& a : addrs->Items()) {
        uint64_t v = 0;
        if (!a.AsUInt64(&v) || v > 0xFFFFFFFFull) { Fail(r, "addrs must be integers (or 0x hex strings)"); return; }
        if (FnHooks::IdOf(static_cast<uint32_t>(v)) == UINT32_MAX) { Fail(r, Hex(v) + " is not a recompiled function's entry"); return; }
        fns.push_back(static_cast<uint32_t>(v));
    }
    for (const uint32_t fn : fns) {
        if (on) {
            FnHooks::Arm(fn, FnHooks::kRing);
            if (std::find(traced_.begin(), traced_.end(), fn) == traced_.end()) traced_.push_back(fn);
        } else {
            FnHooks::Disarm(fn, FnHooks::kRing);
            traced_.erase(std::remove(traced_.begin(), traced_.end(), fn), traced_.end());
        }
    }
    AgentJson out = AgentJson::Object();
    AgentJson tracing = AgentJson::Array();
    for (const uint32_t fn : traced_) tracing.Push(AgentJson::UInt(fn));
    out.Set("tracing", tracing);
    out.Set("ring", AgentJson::UInt(FnHooks::kRingDefault));
    Reply(r, out);
}

void AgentServer::OpFnCensus(const Request& r) {
    const AgentJson* action = r.body.Get("action");
    const std::string a = action && action->IsString() ? action->AsString() : "";
    AgentJson out = AgentJson::Object();
    out.Set("frame", AgentJson::UInt(frame_));
    if (a == "reset") {
        FnHooks::ArmAll(FnHooks::kCount);
        FnHooks::ResetCounts();
        census_armed_ = true;
        out.Set("calls", AgentJson::Object());
        out.Set("armed", AgentJson::Bool(true));
        Reply(r, out);
        return;
    }
    if (a == "off") {
        FnHooks::DisarmAll(FnHooks::kCount);
        census_armed_ = false;
        out.Set("calls", AgentJson::Object());
        out.Set("armed", AgentJson::Bool(false));
        Reply(r, out);
        return;
    }
    if (a != "get") { Fail(r, "action must be \"reset\", \"get\" or \"off\""); return; }
    AgentJson calls = AgentJson::Object();
    for (const auto& [id, n] : FnHooks::Counts()) calls.Set(Hex(FnHooks::AddressOfId(id)), AgentJson::UInt(n));
    out.Set("calls", calls);
    out.Set("armed", AgentJson::Bool(census_armed_));
    if (!census_armed_) out.Set("note", AgentJson::String("the census counts only after fn.census {action:\"reset\"} armed it"));
    Reply(r, out);
}

void AgentServer::OpCallsRecent(const Request& r) {
    std::string err;
    uint32_t addr = 0, n = 16;
    if (!GetU32(r.body, "addr", &addr, true, &err) || !GetU32(r.body, "n", &n, false, &err)) { Fail(r, err); return; }
    if (FnHooks::IdOf(addr) == UINT32_MAX) { Fail(r, Hex(addr) + " is not a recompiled function's entry"); return; }
    if (n == 0 || n > 4096) n = 4096;
    std::vector<FnHooks::Call> recent = FnHooks::Recent(addr, n);   // newest first
    AgentJson calls = AgentJson::Array();
    for (auto it = recent.rbegin(); it != recent.rend(); ++it) calls.Push(CallJson(*it, true));   // newest last
    AgentJson out = AgentJson::Object();
    out.Set("calls", calls);
    out.Set("traced", AgentJson::Bool((FnHooks::Flags(FnHooks::IdOf(addr)) & FnHooks::kRing) != 0));
    Reply(r, out);
}

void AgentServer::OpCorpusRecord(const Request& r) {
    const AgentJson* on = r.body.Get("on");
    if (!on || !on->IsBool()) { Fail(r, "corpus.record needs on:true|false"); return; }
    if (!AccessTracer::Available()) {
        Fail(r, "not available in this build: corpus.record needs the tracing build (CMake PS2R_TRACE_ACCESS=ON)");
        return;
    }
    std::string err;
    if (on->AsBool()) {
        if (AccessTracer::Active()) { Fail(r, "a recording is already running (" + corpus_path_ + "); stop it first"); return; }
        const AgentJson* p = r.body.Get("path");
        if (!p || !p->IsString() || p->AsString().empty()) { Fail(r, "corpus.record on needs a path"); return; }
        std::vector<uint32_t> fns;
        if (const AgentJson* f = r.body.Get("functions")) {
            if (f->IsArray()) {
                for (const AgentJson& a : f->Items()) {
                    uint64_t v = 0;
                    if (!a.AsUInt64(&v) || v > 0xFFFFFFFFull) { Fail(r, "functions must be \"all\" or an array of addresses"); return; }
                    fns.push_back(static_cast<uint32_t>(v));
                }
            } else if (!f->IsString() || f->AsString() != "all") { Fail(r, "functions must be \"all\" or an array of addresses"); return; }
        }
        uint64_t max_distinct = 200000;
        if (const AgentJson* m = r.body.Get("max_distinct")) m->AsUInt64(&max_distinct);
        std::vector<std::pair<std::string, std::string>> meta;
        meta.emplace_back("control_plane_frame", std::to_string(frame_));
        if (!AccessTracer::Start(p->AsString(), fns, meta, max_distinct, &err)) { Fail(r, "corpus.record: " + err); return; }
        corpus_path_ = p->AsString();
        corpus_on_ = true;
        AgentJson out = AgentJson::Object();
        out.Set("on", AgentJson::Bool(true));
        out.Set("path", AgentJson::String(corpus_path_));
        out.Set("records", AgentJson::UInt(0));
        out.Set("frame", AgentJson::UInt(frame_));
        Reply(r, out);
        return;
    }
    if (!AccessTracer::Active()) { Fail(r, "no recording is running"); return; }
    const AccessTracer::Stats st = AccessTracer::CurrentStats();
    if (!AccessTracer::Stop(&err)) { Fail(r, "corpus.record stop: " + err); return; }
    corpus_on_ = false;
    AgentJson out = AgentJson::Object();
    out.Set("on", AgentJson::Bool(false));
    out.Set("path", AgentJson::String(corpus_path_));
    out.Set("records", AgentJson::UInt(st.calls));
    out.Set("distinct", AgentJson::UInt(st.distinct));
    out.Set("dropped", AgentJson::UInt(st.dropped));
    out.Set("truncated", AgentJson::Bool(st.truncated));
    out.Set("frame", AgentJson::UInt(frame_));
    Reply(r, out);
}

std::string AgentServer::Census() const {
    return "on:requests=" + std::to_string(requests_) + ":replies=" + std::to_string(replies_) + ":errors=" + std::to_string(errors_) +
           ":events=" + std::to_string(events_) + ":boundaries=" + std::to_string(boundaries_) + ":waits=" + std::to_string(waits_) +
           ":saves=" + std::to_string(saves_) + ":save_deferrals=" + std::to_string(save_deferrals_) + ":loads=" + std::to_string(loads_) +
           ":connections=" + std::to_string(channel_.Accepted()) + ":watches=" + std::to_string(watches_.size()) +
           ":breaks=" + std::to_string(breaks_.size()) + ":breaks_hit=" + std::to_string(breaks_hit_) +
           ":mid_frame_waits=" + std::to_string(mid_frame_waits_) + ":traced=" + std::to_string(traced_.size()) +
           ":census_armed=" + (census_armed_ ? "1" : "0") + ":probed_calls=" + std::to_string(FnHooks::ProbedCalls());
}

}  // namespace ps2r::app
