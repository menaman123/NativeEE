# `engine/ee/cpu/`

**Phase:** 3  
**Purpose:** CpuContext register PODs (own typedefs, layout-identical to OLD — static_asserted), dense-id dispatch table, the frozen A4 §1.3 seam surface, shadow call stack.

## Files (stage 3A)

| Pair | Class | Rule implemented |
|---|---|---|
| `cpu_state.h` (register PODs, lint-exempt) | `EmotionEngineState` = `CpuContext` | frozen layout: `CPURegs` 720 + `FPUregs` 264 + `VUregs` 608 at 992 = **1600** bytes == every anchor state's THRD `ctx_size`; member names are the generated-code ABI (`cpuRegs`, `fpuRegs`, `vuRegs`) |
| `ps2_float.{h,inl,cpp}` | `Ps2Float` + the ABI free functions | exponent 0xFF -> +-MAX_FLOAT (no NaN/Inf); div/sqrt/rsqrt in single precision under FE_TONEAREST with the caller's mode restored; guest threads run FE_TOWARDZERO |
| `cpu_context_init.{h,cpp}` | `CpuContextInit` | power-on reset: zero, `pc`, `$sp`, `VF0.w = 1`, `Q = 1` |
| `park_site_dump.{h,cpp}` | `ParkSiteDump` | the `fatal_no_jump` park record's text: `target=` + `src=` (every GPR equal to the target — the R5900 `jr`/`jalr` read their target from one GPR) + `chain=` (shadow frames, innermost first) + `gpr:` (the parking thread's 31 GPRs, low word), so the guest word a zero target was loaded from is readable from `park_detail` |

## Belongs here

- cpu_state.h (lint-exempt PODs), CpuContext, DispatchTable (update_function_impl / recomp_fn_id_for / find_containing_function), seams.cpp, ShadowCallStack

## Does not belong here

- manual bodies (never), vendored PCSX2 headers (rule 8)

## Transplanted from (OLD, read-only)

- `cpu_state.h, recomp_seams.cpp, manual_switch_dispatchers.cpp MECHANISM only, main.cpp init_cpu_context / install_dual_hook seam`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
