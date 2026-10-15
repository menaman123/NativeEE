// core/diag/diag_registry.h -- named probes, censuses and counters (diagnostics
// builds only). Subsystems register a name + description + either a counter or
// a callback; the registry prints ONE startup configuration dump and one exit
// dump, replacing the per-site "announce" idiom. Mechanism only: no probe site
// keyed on a game address lives here.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace ps2r {
namespace diag {

class DiagRegistry {
public:
    struct Entry {
        std::string                   name;
        std::string                   description;
        bool                          enabled = false;
        std::atomic<uint64_t>*        counter = nullptr;          // optional
        std::function<std::string()>  report;                     // optional
    };

    static DiagRegistry& Instance();

    // Registers a probe/census. `enabled` records the run's decision for the
    // startup dump; returns the entry index.
    size_t Register(const std::string& name, const std::string& description, bool enabled,
                    std::atomic<uint64_t>* counter = nullptr,
                    std::function<std::string()> report = nullptr);
    bool IsEnabled(const std::string& name) const;
    void SetEnabled(const std::string& name, bool on);
    size_t Count() const;

    // "[DIAG] name=on|off  description" per entry (the startup config dump).
    void DumpConfig(std::FILE* out) const;
    // "[DIAG] name counter=N report" per entry (the exit dump).
    void DumpReport(std::FILE* out) const;
    void Clear();

private:
    DiagRegistry() = default;
    mutable std::mutex mu_;
    std::vector<Entry> entries_;
};

}  // namespace diag
}  // namespace ps2r
