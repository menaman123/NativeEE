#include "app/host/agent_server.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include "engine/core/image/png.h"
#include "engine/ee/cpu/cpu_state.h"

namespace ps2r::app {

namespace {

constexpr uint32_t kSpadBase = 0x70000000u;
constexpr uint32_t kMaxReadBytes = 16u << 20;
constexpr uint32_t kMaxSearchHits = 100000;

const char* const kFeatures[] = {
    "session.info", "run.step", "run.resume", "run.pause", "run.until", "pad.set", "pad.press", "state.save", "state.load",
    "screen.grab", "gs.frame", "vram.read", "mem.read", "mem.write", "mem.search", "mem.watch.add", "mem.watch.remove",
    "cpu.regs", "knobs.set",
};
const char* const kTraceOps[] = {
    "cpu.stack", "fn.break.add", "fn.break.remove", "fn.trace", "fn.census", "fn.calls.recent", "corpus.record", "alloc.log",
};

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
    if (run.kind != RunKind::Resume) {
        Request r;
        r.connection = run.connection;
        r.id = run.id;
        r.has_id = true;
        AgentJson body = AgentJson::Object();
        body.Set("frame", AgentJson::UInt(frame_));
        body.Set("reason", AgentJson::String(reason));
        if (run.kind == RunKind::Until) body.Set("detail", detail ? *detail : AgentJson::Null());
        Reply(r, body);
    }
    AgentJson ev = AgentJson::Object();
    ev.Set("event", AgentJson::String("stopped"));
    ev.Set("frame", AgentJson::UInt(frame_));
    ev.Set("reason", AgentJson::String(reason));
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
    if (run_.kind != RunKind::None) {
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
        if (body.Has("fn_enter") || body.Has("fn_exit")) {
            Fail(r, "not available in this build: run.until fn_enter/fn_exit needs the trace hooks");
            return true;
        }
        bool has_target = false;
        uint64_t target = 0, watch = 0;
        const char* reason = "frame";
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
        if (!has_target && watch == 0) { Fail(r, "run.until needs frame, max_frames or watch"); return true; }
        if (has_target && target <= frame_) {
            // Already there: answer at once without advancing.
            AgentJson out = AgentJson::Object();
            out.Set("frame", AgentJson::UInt(frame_));
            out.Set("reason", AgentJson::String(reason));
            out.Set("detail", AgentJson::Null());
            Reply(r, out);
            return true;
        }
        StartRun(r, RunKind::Until, has_target, target, reason, watch);
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
    if (op == "state.load") return OpStateLoad(r);
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
    for (const char* t : kTraceOps)
        if (op == t) { Fail(r, "not available in this build: " + op + " needs the trace hooks"); return true; }
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
    std::string path, out_path, err;
    int slot = -1;
    if (const AgentJson* p = r.body.Get("path")) { if (p->IsString()) path = p->AsString(); }
    if (const AgentJson* s = r.body.Get("slot")) { int64_t v = 0; if (s->AsInt64(&v)) slot = static_cast<int>(v); }
    if (!b_.state_save(path, slot, &out_path, &err)) { Fail(r, "state.save: " + err); return; }
    ++saves_;
    AgentJson out = AgentJson::Object();
    out.Set("path", AgentJson::String(out_path));
    out.Set("frame", AgentJson::UInt(frame_));
    Reply(r, out);
}

bool AgentServer::OpStateLoad(const Request& r) {
    if (!b_.state_load_prepare) { Fail(r, "not available in this build: state.load has no restore owner"); return true; }
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

std::string AgentServer::Census() const {
    return "on:requests=" + std::to_string(requests_) + ":replies=" + std::to_string(replies_) + ":errors=" + std::to_string(errors_) +
           ":events=" + std::to_string(events_) + ":boundaries=" + std::to_string(boundaries_) + ":waits=" + std::to_string(waits_) +
           ":saves=" + std::to_string(saves_) + ":loads=" + std::to_string(loads_) + ":connections=" + std::to_string(channel_.Accepted()) +
           ":watches=" + std::to_string(watches_.size());
}

}  // namespace ps2r::app
