# `engine/vu/savestate/`

**Phase:** integration I5.7-A (plan item 5.7)  
**Purpose:** the VU `.ps2st` section codecs. `VuMemorySection` -- VU0C / VU0D (4096 B raw) and VU1C / VU1D (16384 B raw) over the two `VuUnit` records, one `SectionSpec` per tag, all REQUIRED (the reference loader fails the load on a missing or mis-sized memory section); restoring a micro memory bumps the unit's micro epoch and the VU1 sections run the root-bound callback (`Vu1HleRuntime::BumpExternalGeneration`). `Vu1RegisterSection` -- VU1R (`u8 running, u32 1600, 1600-byte context image` = 1605 B; the 608-byte `VUregs` block at image offset 992 is `vu1.regs()`), OPTIONAL like the reference loader; serialize writes `running = 0` and a zeroed image around the register block. Registered in `app/host/ee_bootstrap.cpp`: the memories right after RAM0/SPAD/META and before THRD, VU1R after SIFS and before SDRX (the reference file order).

## Belongs here

- VuMemorySection
- Vu1RegisterSection

## Does not belong here

- the unit itself (`engine/vu/vu_unit`); the envelope / registry (`engine/core/savestate`)

## Transplanted from (OLD, read-only)

- `host_app/savestate.cpp:353-356` (section order), `:1116-1119` (memory capture), `:1582-1585` (exact-size admissibility), `:1723-1731` (memory restore, failure = load aborted)
- `host_app/savestate.cpp:1317-1323` (VU1R capture: running flag, `sizeof(CpuContext)`, the worker's context), `:1633-1643` (VU1R validate), `:2000-2019` (VU1R restore; absent = reset registers)
- `host_app/cpu_state.h` layout as pinned by `engine/ee/cpu/cpu_state.h` (`vuRegs` at 992, `sizeof == 1600`)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
