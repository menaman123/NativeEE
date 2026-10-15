// core/savestate/quiescence.h -- the save-point refusal predicate.
//
// A savestate is taken only at a frame boundary where every registered
// subsystem reports it can be captured whole (the reference's quiesce check:
// an FMV actively decoding, a channel mid-transfer, ... refuse the save, and
// the request is dropped or deferred by its owner). Owners register a NAMED
// predicate; Check() asks them in registration order and reports the first
// refusal with its name and reason, counting refusals per predicate for the
// census. Pure plumbing: no I/O, no state of its own beyond the counters.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ps2r::savestate {

class Quiescence {
public:
    // Returns true when the save may proceed; false with `reason` otherwise.
    using Predicate = std::function<bool(std::string* reason)>;

    void Register(std::string name, Predicate pred);

    // True when no predicate refuses. On refusal `*refused_by` / `*reason`
    // (when non-null) name the first refusing predicate; its count rises.
    bool Check(std::string* refused_by = nullptr, std::string* reason = nullptr);

    size_t Count() const { return preds_.size(); }
    uint64_t Checks() const { return checks_; }
    uint64_t Refusals() const { return refusals_; }
    // Refusals attributed to a named predicate (0 when unknown).
    uint64_t RefusalsBy(const std::string& name) const;

private:
    struct Entry {
        std::string name;
        Predicate pred;
        uint64_t refusals = 0;
    };
    std::vector<Entry> preds_;
    uint64_t checks_ = 0;
    uint64_t refusals_ = 0;
};

}  // namespace ps2r::savestate
