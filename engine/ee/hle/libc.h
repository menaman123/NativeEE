#pragma once
// Libc: native cores for the SDK 3.0.1 static-libc memory/string family
// (memcpy / memmove / memset / strlen), the SdkWrap rows of the binding
// manifest (HOOK_RECLASSIFICATION 3.4). Each core returns true when it fully
// handled the call -- memory effect AND the exact register effects of the
// recompiled MIPS body, including 128-bit residues -- and false when a guard
// rejects the arguments, in which case the wrapper runs the original body.
//
// Overlap rule (the MIPS SDK memmove FUN_002c6d8c): a backward byte copy is
// selected exactly when src < dest < src+n; every forward path reads before
// any write that could touch the read bytes, so the memory effect equals
// std::memmove in ALL cases (OLD libc_hle.cpp header proof, byte-verified by
// OLD tests/libc_hle_diff; ledger: docs/audit/A4_CPU_HLE.md, HOOK 3.4). The
// memcpy row has no MIPS reference (never emitted): its shipping contract is
// memory = memmove, v0 = dest, nothing else written.
//
// Guards: full 64-bit a0/a1/a2 must resolve inside plain main RAM (KSEG
// mirrors, scratchpad, MMIO, garbage upper bits all fall back); strlen needs a
// NUL plus a 0x20-byte overread margin. A title may register one protected
// byte band whose overlap also falls back (OLD's VTABLE-PROTECT path).
#include <cstdint>

#include "engine/ee/cpu/cpu_state.h"

class Libc {
public:
    static bool Memcpy(CpuContext& ctx);
    static bool Memmove(CpuContext& ctx);
    static bool Memset(CpuContext& ctx);
    static bool Strlen(CpuContext& ctx);

    // Title-registered protected band [lo, hi): copies overlapping it fall back.
    static void SetProtectedBand(uint64_t lo, uint64_t hi);

private:
    static bool RangeInRam(uint64_t base, uint64_t len);
    static bool OverlapsProtected(uint64_t base, uint64_t len);
};
