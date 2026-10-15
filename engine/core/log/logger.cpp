// core/log/logger.cpp -- sinks and the runtime level gate.
#include "engine/core/log/logger.h"

#include <cstring>

#include "engine/platform/host/platform.h"

namespace ps2r {
namespace log {

namespace {
const char* const kCategoryNames[static_cast<int>(Category::COUNT)] = {
    "VIF",    "GIF",  "GS",     "VU",   "DMAC",   "MEM",     "SIF",  "SND",
    "PAD",    "MPEG", "KERNEL", "LOADER", "HEAP", "PERF",    "IOP",  "CDVD",
    "TIME",   "SAVE", "HARNESS", "INTC", "TIMER", "IPU",     "MC",   "SCANOUT",
    "TEX",    "DRAW", "STREAM", "HLE",  "BOOT",   "CONFIG",  "TRIAGE", "HOST"};

const char* const kLevelNames[] = {"quiet", "error", "warn", "info", "narrate"};
}  // namespace

const char* CategoryName(Category c) {
    const int i = static_cast<int>(c);
    return (i >= 0 && i < static_cast<int>(Category::COUNT)) ? kCategoryNames[i] : "?";
}

bool CategoryFromName(const char* name, Category* out) {
    if (!name) return false;
    for (int i = 0; i < static_cast<int>(Category::COUNT); i++) {
        if (ps2r::host::StrCaseCmp(name, kCategoryNames[i]) == 0) {
            if (out) *out = static_cast<Category>(i);
            return true;
        }
    }
    return false;
}

const char* LevelName(Level l) {
    const int i = static_cast<int>(l);
    return (i >= 0 && i <= static_cast<int>(Level::Narrate)) ? kLevelNames[i] : "?";
}

bool LevelFromName(const char* name, Level* out) {
    if (!name) return false;
    for (int i = 0; i <= static_cast<int>(Level::Narrate); i++) {
        if (ps2r::host::StrCaseCmp(name, kLevelNames[i]) == 0) {
            if (out) *out = static_cast<Level>(i);
            return true;
        }
    }
    return false;
}

Logger& Logger::Instance() {
    static Logger l;
    return l;
}

void Logger::Start(const std::string& file_path, Level level, bool to_stderr) {
    std::lock_guard<std::mutex> lk(mu_);
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    if (!file_path.empty() && level != Level::Quiet) file_ = std::fopen(file_path.c_str(), "w");
    to_stderr_ = to_stderr;
    level_.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
}

void Logger::Stop() {
    std::lock_guard<std::mutex> lk(mu_);
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void Logger::Logf(Category cat, Level level, const char* fmt, ...) {
    if (!Passes(level)) return;
    va_list ap;
    va_start(ap, fmt);
    Logv(cat, level, fmt, ap);
    va_end(ap);
}

void Logger::Logv(Category cat, Level level, const char* fmt, va_list ap) {
    if (!Passes(level)) return;
    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    WriteLine(cat, level, buf);
}

void Logger::WriteLine(Category cat, Level level, const char* line) {
    if (!Passes(level)) return;
    std::lock_guard<std::mutex> lk(mu_);
    if (file_) std::fprintf(file_, "[%s] %s\n", CategoryName(cat), line);
    if (to_stderr_) std::fprintf(stderr, "[%s] %s\n", CategoryName(cat), line);
    lines_.fetch_add(1, std::memory_order_relaxed);
}

void Logger::Flush() {
    std::lock_guard<std::mutex> lk(mu_);
    if (file_) std::fflush(file_);
    std::fflush(stderr);
}

}  // namespace log
}  // namespace ps2r
