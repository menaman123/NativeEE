#pragma once
// Fixed-width integer aliases used by the EE register file and the generated
// recompiled code. Our own names (u8..u128, s8..s128); nothing vendored.
#include <cstdint>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

// 128-bit quantities as two 64-bit halves, little-endian (lo first) -- the
// R5900 register width. Plain aggregates: no constructors, so the register
// unions that contain them stay trivially copyable (the THRD savestate section
// serialises them raw).
struct u128 {
    u64 lo;
    u64 hi;
};

struct s128 {
    s64 lo;
    s64 hi;
};

static_assert(sizeof(u128) == 16 && sizeof(s128) == 16, "128-bit halves");
