#include "engine/core/savestate/quiescence.h"

namespace ps2r::savestate {

void Quiescence::Register(std::string name, Predicate pred) {
    if (!pred) return;
    preds_.push_back(Entry{std::move(name), std::move(pred), 0});
}

bool Quiescence::Check(std::string* refused_by, std::string* reason) {
    ++checks_;
    for (Entry& e : preds_) {
        std::string why;
        if (e.pred(&why)) continue;
        ++e.refusals;
        ++refusals_;
        if (refused_by) *refused_by = e.name;
        if (reason) *reason = why;
        return false;
    }
    return true;
}

uint64_t Quiescence::RefusalsBy(const std::string& name) const {
    for (const Entry& e : preds_)
        if (e.name == name) return e.refusals;
    return 0;
}

}  // namespace ps2r::savestate
