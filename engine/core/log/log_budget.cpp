// core/log/log_budget.cpp -- see log_budget.h.
#include "engine/core/log/log_budget.h"

namespace ps2r {
namespace log {

LogBudget& LogBudget::Instance() {
    static LogBudget b;
    return b;
}

bool LogBudget::FirstN(const char* key, uint32_t n) {
    std::lock_guard<std::mutex> lk(mu_);
    Entry& e = entries_[key];
    return e.calls++ < n;
}

bool LogBudget::EveryNth(const char* key, uint32_t n) {
    std::lock_guard<std::mutex> lk(mu_);
    Entry& e = entries_[key];
    const uint64_t c = e.calls++;
    return n == 0 ? false : (c % n) == 0;
}

bool LogBudget::FrameWindow(const char* key, uint32_t per_frame, uint32_t frame) {
    std::lock_guard<std::mutex> lk(mu_);
    Entry& e = entries_[key];
    if (e.frame != frame) {
        e.last_suppressed = e.suppressed;
        e.frame = frame;
        e.frame_count = 0;
        e.suppressed = 0;
    }
    if (e.frame_count < per_frame) {
        e.frame_count++;
        return true;
    }
    e.suppressed++;
    return false;
}

uint32_t LogBudget::Suppressed(const char* key) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = entries_.find(key);
    return it == entries_.end() ? 0u : it->second.last_suppressed;
}

void LogBudget::Reset() {
    std::lock_guard<std::mutex> lk(mu_);
    entries_.clear();
}

}  // namespace log
}  // namespace ps2r
