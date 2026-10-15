// Alarms implementation (tick-domain one-shot alarms fired only at a boundary).
#include "engine/ee/hle/alarms.h"

#include <algorithm>

#include "engine/ee/hle/kernel_codes.h"

Alarms& Alarms::Instance() {
    static Alarms a;
    return a;
}

int Alarms::SetAlarm(uint64_t now_tick, uint32_t ticks, Handler handler, uint32_t arg) {
    std::lock_guard<std::mutex> lk(mu_);
    const int id = next_id_++;
    const uint32_t t = ticks ? ticks : 1u;
    armed_.push_back(Entry{id, now_tick + t, ticks, arg, std::move(handler)});
    return id;
}

int Alarms::ReleaseAlarm(int id) {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto it = armed_.begin(); it != armed_.end(); ++it)
        if (it->id == id) {
            armed_.erase(it);
            return KE_OK;
        }
    return KE_ERROR;
}

int Alarms::FireDue(uint64_t now_tick) {
    std::vector<Entry> due;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto it = armed_.begin(); it != armed_.end();) {
            if (it->due_tick <= now_tick) {
                due.push_back(std::move(*it));
                it = armed_.erase(it);
            } else {
                ++it;
            }
        }
    }
    std::sort(due.begin(), due.end(), [](const Entry& a, const Entry& b) { return a.id < b.id; });
    for (auto& e : due)
        if (e.handler) e.handler(e.id, e.time, e.arg);
    return static_cast<int>(due.size());
}

int Alarms::ArmedCount() {
    std::lock_guard<std::mutex> lk(mu_);
    return static_cast<int>(armed_.size());
}

void Alarms::ResetForTest() {
    std::lock_guard<std::mutex> lk(mu_);
    armed_.clear();
    next_id_ = 1;
}
