// core/log/log_categories.h -- compile-time log categories.
//
// Harvested from the frozen original's log-tag registry (the TagFilterBuf
// deny/allow families in its main.cpp: VIF, GIF, GS, VU, ATLAS, SCANOUT, DRAW,
// TEX, STREAM, DIAG..., plus the kept families KERNEL/LOADER/HEAP/HLE/IO/
// WARNING/CRASH/EXIT) and the runtime-log subsystems of ARCHITECTURE section 9.
// A category compiled out here costs nothing at its emit sites: PS2R_LOG()
// resolves `Enabled(cat)` at compile time (log.h).
//
// The build enables categories through PS2R_LOG_CATEGORY_MASK (a 64-bit mask of
// 1u << Category); the default enables every category, and Release builds may
// narrow it. Runtime narrowing below that is Logger's dev.log_level gate.
#pragma once

#include <cstdint>

namespace ps2r {
namespace log {

enum class Category : uint8_t {
    VIF = 0,
    GIF,
    GS,
    VU,
    DMAC,
    MEM,
    SIF,
    SND,
    PAD,
    MPEG,
    KERNEL,
    LOADER,
    HEAP,
    PERF,
    IOP,
    CDVD,
    TIME,
    SAVE,
    HARNESS,
    INTC,
    TIMER,
    IPU,
    MC,
    SCANOUT,
    TEX,
    DRAW,
    STREAM,
    HLE,
    BOOT,
    CONFIG,
    TRIAGE,
    HOST,
    COUNT
};

static_assert(static_cast<int>(Category::COUNT) <= 64, "category mask is 64 bits");

#ifndef PS2R_LOG_CATEGORY_MASK
#define PS2R_LOG_CATEGORY_MASK 0xFFFFFFFFFFFFFFFFull
#endif

constexpr uint64_t kCategoryMask = PS2R_LOG_CATEGORY_MASK;

// Compile-time: is this category compiled into the binary at all?
constexpr bool Enabled(Category c) {
    return (kCategoryMask >> static_cast<unsigned>(c)) & 1ull;
}

// Category name for the log line prefix and for --set dev.log_categories.
const char* CategoryName(Category c);

// Parses a name (case-insensitive); returns false for unknown names.
bool CategoryFromName(const char* name, Category* out);

}  // namespace log
}  // namespace ps2r
