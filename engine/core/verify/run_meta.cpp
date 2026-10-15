// core/verify/run_meta.cpp
#include "engine/core/verify/run_meta.h"

#include <cstdio>

#include "engine/core/threads/park.h"

namespace ps2r::verify {

// wall_ms counts from the leg's arming: a leg that parks before its first
// present (the Phase-3 boot/anchor legs) still reports its true wall time.
RunMeta::RunMeta(std::string path, Identity id)
    : path_(std::move(path)), id_(std::move(id)), start_ns_(Park::NowNs()) {}

std::string RunMeta::Render() const {
    const uint64_t wall_ms = start_ns_ ? (Park::NowNs() - start_ns_) / 1000000ull : 0;
    char buf[64];
    std::string out;
    out += "build_id=" + id_.build_id + "\n";
    out += "gl_renderer=" + (gl_renderer_.empty() ? std::string("none") : gl_renderer_) + "\n";
    out += "tree=" + id_.tree + "\n";
    out += "profile=" + (id_.profile.empty() ? std::string("none") : id_.profile) + "\n";
    out += "env_ps2=" + id_.env_ps2 + "\n";
    std::snprintf(buf, sizeof(buf), "env_hash=0x%016llx\n", (unsigned long long)id_.env_hash);
    out += buf;
    std::snprintf(buf, sizeof(buf), "frames=%llu\n", (unsigned long long)frames_);
    out += buf;
    std::snprintf(buf, sizeof(buf), "wall_ms=%llu\n", (unsigned long long)wall_ms);
    out += buf;
    out += finalized_ ? "finalized=1\n" : "finalized=0\n";
    for (const auto& kv : extra_) out += kv.first + "=" + kv.second + "\n";
    return out;
}

bool RunMeta::WriteNow() const {
    if (path_.empty()) return true;
    std::FILE* f = std::fopen(path_.c_str(), "w");
    if (!f) return false;
    const std::string text = Render();
    const bool written = std::fwrite(text.data(), 1, text.size(), f) == text.size();
    const bool closed = std::fclose(f) == 0;
    return written && closed;
}

bool RunMeta::Flush() const { return WriteNow(); }

std::unique_ptr<RunMeta> RunMeta::CopyForSegment(std::string path) const {
    auto next = std::make_unique<RunMeta>(std::move(path), id_);
    next->gl_renderer_ = gl_renderer_;
    next->extra_ = extra_;
    return next;
}

void RunMeta::OnPresent(const char* gl_renderer) {
    if (frames_ == 0) {
        if (gl_renderer && *gl_renderer && gl_renderer_.empty()) gl_renderer_ = gl_renderer;
        WriteNow();   // early stamp: even a wedged leg carries meta
    }
    frames_++;
    if ((frames_ & 63) == 0) WriteNow();
}

void RunMeta::Finalize() {
    if (finalized_) return;
    finalized_ = true;
    WriteNow();
}

void RunMeta::Set(const std::string& key, const std::string& value) {
    for (auto& kv : extra_)
        if (kv.first == key) { kv.second = value; return; }
    extra_.emplace_back(key, value);
}

}  // namespace ps2r::verify
