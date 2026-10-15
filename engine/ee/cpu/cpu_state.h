#pragma once
// Emotion Engine register file -- the state every recompiled function operates on.
//
// Layout contract: this struct is serialised RAW by the THRD savestate section
// (ctx_size guard == sizeof(CpuContext) == 1600, measured against the anchor
// states), and the generated recompiled code addresses its members by name
// (ctx.cpuRegs.GPR.r[n].UD[0], ctx.fpuRegs.fpr[n].f, ctx.vuRegs.VF[n] ...).
// Member NAMES and OFFSETS are therefore frozen; the static_asserts at the end
// pin them. Register PODs only (lint-exempt path: engine/ee/cpu/cpu_state.h).
#include "engine/core/types.h"

#include <cstddef>
#include <cstring>

// One 128-bit general-purpose register with every access width the R5900
// instruction set uses (UQ/SQ whole, UD/SD dword halves, UL/SL words, US/SS
// halfwords, UC/SC bytes). Little-endian element order: UD[0] is the low dword.
union alignas(16) GPR_reg {
    u128 UQ;
    s128 SQ;
    u64 UD[2];
    s64 SD[2];
    u32 UL[4];
    s32 SL[4];
    u16 US[8];
    s16 SS[8];
    u8 UC[16];
    s8 SC[16];
};

// One VU vector register: four floats (x,y,z,w) with raw-bit views.
union alignas(16) VR_reg {
    struct {
        float x, y, z, w;
    };
    float F[4];
    u32 I[4];
    s128 Q;
};

union GPRregs {
    GPR_reg r[32];
};

// COP0 system-control registers, by number (r[]) and by name (n.*).
union CP0regs {
    struct {
        u32 Index, Random, EntryLo0, EntryLo1, Context, PageMask, Wired, Reserved0, BadVAddr, Count, EntryHi,
            Compare, Status, Cause, EPC, PRid, Config, LLAddr, WatchLO, WatchHI, XContext, Reserved1, Reserved2,
            Debug, DEPC, PerfCnt, ErrCtl, CacheErr, TagLo, TagHi, ErrorEPC, DESAVE;
    } n;
    u32 r[32];
};

// One COP1 floating-point register: value / raw bits.
union FPRreg {
    float f;
    u32 UL;
    s32 SL;
};

// COP1 (FPU): 32 FPRs, 32 control registers (fprc[31] is FCR31), the
// accumulator used by MADD/MSUB and its overflow flag.
struct FPUregs {
    FPRreg fpr[32];
    u32 fprc[32];
    FPRreg ACC;
    u32 ACCflag;
};

// VU0 macro-mode (COP2) register block: VF0..VF31 (VF0 is hard-wired to
// {0,0,0,1}), VI0..VI15 (VI0 reads 0), ACC, Q, P, I, R, the three flag
// registers and the control registers reachable through CFC2/CTC2.
struct VUregs {
    VR_reg VF[32];
    u16 VI[16];
    VR_reg ACC;
    float Q;
    float P;
    u32 I;
    u32 R;
    u16 status_flag;  // bits 0-5 Z,S,U,O,I,D; bits 6-11 sticky ZS,SS,US,OS,IS,DS
    u16 mac_flag;     // 4 flags x 4 fields
    u32 clip_flag;    // 24 bits: 6 flags x 4 CLIP results
    u32 TPC;
    u32 CMSAR0;
    u32 FBRST;
    u32 VPU_STAT;
    u32 CMSAR1;

    // CFC2 view of control register `reg` (VU manual 5.1.2: VI registers are
    // 16-bit unsigned and zero-extend; VI0 and unmapped numbers read 0).
    s32 ReadControlReg(int reg) const {
        if (reg == 0) return 0;
        if (reg >= 1 && reg <= 15) return static_cast<s32>(static_cast<u32>(VI[reg]));
        switch (reg) {
            case 16: return static_cast<s32>(status_flag);
            case 17: return static_cast<s32>(mac_flag);
            case 18: return static_cast<s32>(clip_flag);
            case 20: return static_cast<s32>(R);
            case 21: return static_cast<s32>(I);
            case 22: {
                u32 bits;
                std::memcpy(&bits, &Q, sizeof(bits));
                return static_cast<s32>(bits);
            }
            case 26: return static_cast<s32>(TPC);
            case 27: return static_cast<s32>(CMSAR0);
            case 28: return static_cast<s32>(FBRST);
            case 29: return static_cast<s32>(VPU_STAT);
            case 31: return static_cast<s32>(CMSAR1);
            default: return 0;
        }
    }

    // CTC2: the status flag's lower 6 bits are read-only (only the sticky
    // bits 6-11 take a write); R is a 23-bit LFSR; TPC/CMSAR are 16-bit;
    // FBRST keeps only DE0/TE0/DE1/TE1 (FB/RS are write-triggers).
    void WriteControlReg(int reg, u32 value) {
        if (reg >= 1 && reg <= 15) {
            VI[reg] = static_cast<u16>(value);
            return;
        }
        switch (reg) {
            case 16: status_flag = static_cast<u16>((status_flag & 0x03Fu) | (value & 0xFC0u)); break;
            case 17: mac_flag = static_cast<u16>(value); break;
            case 18: clip_flag = value & 0xFFFFFFu; break;
            case 20: R = value & 0x7FFFFFu; break;
            case 21: I = value; break;
            case 22: std::memcpy(&Q, &value, sizeof(Q)); break;
            case 26: TPC = value & 0xFFFFu; break;
            case 27: CMSAR0 = value & 0xFFFFu; break;
            case 28: FBRST = value & 0x0C0Cu; break;
            case 31: CMSAR1 = value & 0xFFFFu; break;
            default: break;
        }
    }

    // Power-on state: everything zero except the hard-wired VF0.w = 1 and Q = 1.
    void Reset() {
        std::memset(this, 0, sizeof(VUregs));
        VF[0].w = 1.0f;
        Q = 1.0f;
    }
};

// Integer core: GPRs, the two multiply/divide pipelines' HI/LO, COP0, SA, PC.
struct CPURegs {
    GPRregs GPR;
    GPR_reg HI;
    GPR_reg LO;
    GPR_reg HI1;
    GPR_reg LO1;
    CP0regs CP0;
    u32 sa;
    u32 pc;
};

struct EmotionEngineState {
    CPURegs cpuRegs;
    FPUregs fpuRegs;
    VUregs vuRegs;
};

typedef EmotionEngineState CpuContext;

// Frozen layout (THRD ctx_size == 1600 in every anchor state).
static_assert(sizeof(GPR_reg) == 16, "GPR_reg is one quadword");
static_assert(sizeof(VR_reg) == 16, "VR_reg is one quadword");
static_assert(sizeof(CPURegs) == 720, "CPURegs: 32x16 GPR + 4x16 HI/LO + 128 CP0 + sa + pc, 16-aligned");
static_assert(sizeof(FPUregs) == 264, "FPUregs: 128 fpr + 128 fprc + ACC + ACCflag");
static_assert(sizeof(VUregs) == 608, "VUregs: 512 VF + 32 VI + 16 ACC + 44 scalars, 16-aligned");
static_assert(offsetof(EmotionEngineState, fpuRegs) == 720, "fpuRegs offset");
static_assert(offsetof(EmotionEngineState, vuRegs) == 992, "vuRegs offset");
static_assert(sizeof(EmotionEngineState) == 1600, "CpuContext size == anchor THRD ctx_size");
