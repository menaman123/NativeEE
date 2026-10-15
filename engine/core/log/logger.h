// core/log/logger.h -- the runtime text log: levels, three sinks (file,
// stderr, ring), and the runtime dev.log_level gate. The compile-time category
// gate lives in log.h / log_categories.h; this class only ever sees enabled
// categories.
#pragma once

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

#include "engine/core/log/log_categories.h"

namespace ps2r {
namespace log {

enum class Level : uint8_t {
    Quiet = 0,   // nothing
    Error,
    Warn,
    Info,
    Narrate,     // per-event hardware narration
};

const char* LevelName(Level l);
bool LevelFromName(const char* name, Level* out);

class Logger {
public:
    static Logger& Instance();

    // Opens the file sink (empty path = no file sink) and sets the runtime
    // level. Safe to call once at startup before threads exist.
    void Start(const std::string& file_path, Level level, bool to_stderr);
    void Stop();

    void SetLevel(Level level) { level_.store(static_cast<uint8_t>(level), std::memory_order_relaxed); }
    Level GetLevel() const { return static_cast<Level>(level_.load(std::memory_order_relaxed)); }
    // Runtime gate: true when a message of `level` would be written.
    bool Passes(Level level) const {
        return static_cast<uint8_t>(level) <= level_.load(std::memory_order_relaxed) &&
               level != Level::Quiet;
    }

    // Formats "[CAT] message\n" and writes it to every open sink.
    void Logf(Category cat, Level level, const char* fmt, ...)
        __attribute__((format(printf, 4, 5)));
    void Logv(Category cat, Level level, const char* fmt, va_list ap);
    // Pre-formatted line (no trailing newline required).
    void WriteLine(Category cat, Level level, const char* line);

    void Flush();
    bool FileOpen() const { return file_ != nullptr; }
    uint64_t LinesWritten() const { return lines_.load(std::memory_order_relaxed); }

private:
    Logger() = default;
    std::mutex            mu_;
    std::FILE*            file_ = nullptr;
    bool                  to_stderr_ = false;
    std::atomic<uint8_t>  level_{static_cast<uint8_t>(Level::Quiet)};
    std::atomic<uint64_t> lines_{0};
};

}  // namespace log
}  // namespace ps2r
