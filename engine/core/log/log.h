// core/log/log.h -- the PS2R_LOG emit macro: a compile-time category gate in
// front of the runtime Logger. A category that is compiled out (log_categories.h)
// leaves NO code at the emit site: the argument expressions are never evaluated.
#pragma once

#include "engine/core/log/log_categories.h"
#include "engine/core/log/logger.h"

// PS2R_LOG(CAT, Level, "fmt", ...) -- CAT is a bare Category enumerator name.
#define PS2R_LOG(cat, level, ...)                                                          \
    do {                                                                                   \
        if constexpr (::ps2r::log::Enabled(::ps2r::log::Category::cat)) {                 \
            ::ps2r::log::Logger::Instance().Logf(::ps2r::log::Category::cat,              \
                                                 ::ps2r::log::Level::level, __VA_ARGS__); \
        }                                                                                  \
    } while (0)

// True when the category is compiled in AND the runtime level admits `level`
// (for callers that must skip expensive argument preparation).
#define PS2R_LOG_ON(cat, level)                                          \
    (::ps2r::log::Enabled(::ps2r::log::Category::cat) &&                 \
     ::ps2r::log::Logger::Instance().Passes(::ps2r::log::Level::level))
