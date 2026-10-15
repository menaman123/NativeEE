// NativeEE -- NEW-authored fixed-width type aliases for the transplanted
// recompiler tool (stage P8.8-T, finding INT-P88T-PCSX2-GPL-HEADER).
//
// This file replaces the copy of OLD's host_app/common/Pcsx2Types.h, which carried
// third-party copyleft text; STAGE_RULES rule 5 forbids copied licence text in NEW.
// It keeps the file name and every type name that tools/recompiler_v2/host_app/cpu_state.h
// consumes (s8..u64, uptr/sptr/uint, u128/s128 with lo/hi and the array views), so the
// tool sources stay byte-identical to OLD. Nothing here affects the generated text: the
// tool only needs these declarations to compile cpu_state.h / memory.h, which it never
// executes. The nativeee runtime does not include this header.
#pragma once

#include <cstdint>

using s8  = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using uptr = std::uintptr_t;
using sptr = std::intptr_t;
using uint = unsigned int;

// 128-bit unsigned quadword viewed as two 64-bit halves or as arrays of narrower lanes.
// Plain aggregate (no constructors) so it can sit inside the register unions.
union u128 {
    struct {
        u64 lo;
        u64 hi;
    };
    u64 _u64[2];
    u32 _u32[4];
    u16 _u16[8];
    u8  _u8[16];

    bool operator==(const u128& o) const { return lo == o.lo && hi == o.hi; }
    bool operator!=(const u128& o) const { return !(*this == o); }
};

// 128-bit signed quadword as two signed 64-bit halves.
struct s128 {
    s64 lo;
    s64 hi;

    bool operator==(const s128& o) const { return lo == o.lo && hi == o.hi; }
    bool operator!=(const s128& o) const { return !(*this == o); }
};

static_assert(sizeof(u128) == 16, "u128 must be 16 bytes");
static_assert(sizeof(s128) == 16, "s128 must be 16 bytes");
