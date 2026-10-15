#pragma once
// One 128-bit quadword as moved by LQ/SQ and the DMA paths, with every element
// view the generated code uses. Trivially copyable POD; the member names are
// part of the generated-code ABI.
#include <cstdint>

struct alignas(16) QuadWord {
    union {
        uint8_t u8[16];
        uint16_t u16[8];
        uint32_t u32[4];
        uint64_t u64[2];
        int8_t s8[16];
        int16_t s16[8];
        int32_t s32[4];
        int64_t s64[2];
        float f32[4];
        uint8_t m2[16];
    };
};

static_assert(sizeof(QuadWord) == 16, "QuadWord is 16 bytes");
