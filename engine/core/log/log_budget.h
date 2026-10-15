// core/log/log_budget.h -- per-key log budgeting primitives replacing the
// hand-rolled `static int s_lines` idioms: first_n, every_nth, frame_window
// (per-frame cap that resets each presented frame and reports what it clipped).
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace ps2r {
namespace log {

class LogBudget {
public:
    static LogBudget& Instance();

    // True for the first `n` calls with this key, false afterwards.
    bool FirstN(const char* key, uint32_t n);
    // True on every n-th call with this key (1st, n+1th, ...).
    bool EveryNth(const char* key, uint32_t n);
    // True while fewer than `per_frame` lines were allowed for this key during
    // `frame`; clipped lines are counted and reported through Suppressed().
    bool FrameWindow(const char* key, uint32_t per_frame, uint32_t frame);
    // Lines clipped by FrameWindow for `key` in the most recently closed frame.
    uint32_t Suppressed(const char* key);

    void Reset();

private:
    struct Entry {
        uint64_t calls = 0;
        uint32_t frame = 0xffffffffu;
        uint32_t frame_count = 0;
        uint32_t suppressed = 0;
        uint32_t last_suppressed = 0;
    };
    std::mutex                   mu_;
    std::map<std::string, Entry> entries_;
};

}  // namespace log
}  // namespace ps2r
