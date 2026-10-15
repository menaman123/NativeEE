// core/diag/diag_registry.cpp -- see diag_registry.h.
#include "engine/core/diag/diag_registry.h"

namespace ps2r {
namespace diag {

DiagRegistry& DiagRegistry::Instance() {
    static DiagRegistry r;
    return r;
}

size_t DiagRegistry::Register(const std::string& name, const std::string& description,
                              bool enabled, std::atomic<uint64_t>* counter,
                              std::function<std::string()> report) {
    std::lock_guard<std::mutex> lk(mu_);
    Entry e;
    e.name = name;
    e.description = description;
    e.enabled = enabled;
    e.counter = counter;
    e.report = std::move(report);
    entries_.push_back(std::move(e));
    return entries_.size() - 1;
}

bool DiagRegistry::IsEnabled(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (const Entry& e : entries_)
        if (e.name == name) return e.enabled;
    return false;
}

void DiagRegistry::SetEnabled(const std::string& name, bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    for (Entry& e : entries_)
        if (e.name == name) e.enabled = on;
}

size_t DiagRegistry::Count() const {
    std::lock_guard<std::mutex> lk(mu_);
    return entries_.size();
}

void DiagRegistry::DumpConfig(std::FILE* out) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::fprintf(out, "[DIAG] %zu registered probes\n", entries_.size());
    for (const Entry& e : entries_)
        std::fprintf(out, "[DIAG] %s=%s  %s\n", e.name.c_str(), e.enabled ? "on" : "off",
                     e.description.c_str());
}

void DiagRegistry::DumpReport(std::FILE* out) const {
    std::lock_guard<std::mutex> lk(mu_);
    for (const Entry& e : entries_) {
        if (!e.enabled) continue;
        std::fprintf(out, "[DIAG] %s", e.name.c_str());
        if (e.counter) std::fprintf(out, " counter=%llu",
                                    (unsigned long long)e.counter->load(std::memory_order_relaxed));
        if (e.report) std::fprintf(out, " %s", e.report().c_str());
        std::fprintf(out, "\n");
    }
}

void DiagRegistry::Clear() {
    std::lock_guard<std::mutex> lk(mu_);
    entries_.clear();
}

}  // namespace diag
}  // namespace ps2r
