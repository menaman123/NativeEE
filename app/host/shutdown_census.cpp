// app/host/shutdown_census.cpp -- the contract is in the header.
#include "app/host/shutdown_census.h"

namespace ps2r::app {

namespace {
uint64_t MsSince(ShutdownCensus::Clock::time_point since) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(ShutdownCensus::Clock::now() - since).count());
}
}  // namespace

void ShutdownCensus::Begin() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (begun_.load(std::memory_order_acquire)) return;
    began_ = Clock::now();
    begun_.store(true, std::memory_order_release);
}

void ShutdownCensus::Enter(const char* step) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_ = step ? step : "";
    current_started_ = Clock::now();
}

// A step entered before the close (the game's own run, "game_unwind") counts
// from the close request, never from its entry.
ShutdownCensus::Clock::time_point ShutdownCensus::StepStart() const {
    return (begun_.load(std::memory_order_acquire) && began_ > current_started_) ? began_ : current_started_;
}

void ShutdownCensus::Leave() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_.empty()) return;
    steps_.emplace_back(current_, MsSince(StepStart()));
    current_.clear();
}

void ShutdownCensus::End() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!begun_.load(std::memory_order_acquire)) return;
    total_ms_.store(MsSince(began_), std::memory_order_release);
}

void ShutdownCensus::Note(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    notes_.emplace_back(key, value);
}

bool ShutdownCensus::HasNote(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& note : notes_) if (note.first == key) return true;
    return false;
}

std::string ShutdownCensus::Current(uint64_t* elapsed_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (elapsed_ms) *elapsed_ms = current_.empty() ? 0 : MsSince(StepStart());
    return current_;
}

uint64_t ShutdownCensus::ElapsedMs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return begun_.load(std::memory_order_acquire) ? MsSince(began_) : 0;
}

std::string ShutdownCensus::Render() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t total = total_ms_.load(std::memory_order_acquire);
    std::string out = "total=" + std::to_string(total ? total : (begun_.load() ? MsSince(began_) : 0));
    for (const auto& step : steps_) out += "," + step.first + "=" + std::to_string(step.second);
    for (const auto& note : notes_) out += "," + note.first + "=" + note.second;
    if (!current_.empty()) out += ",stuck=" + current_ + ":" + std::to_string(MsSince(StepStart()));
    return out;
}

}  // namespace ps2r::app
