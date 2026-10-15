# engine/ee/recomp_abi — the generated-code seam (PHASE3_PLAN 2.9 / 3.4)

The frozen recompiler's output includes `"../host_app/<name>.h"` (11 names) and calls free functions /
reads globals under OLD's names. This directory is the ONLY place those names exist in NEW:

| Path | Content |
|---|---|
| `anchor/` | include anchor: `-I engine/ee/recomp_abi/anchor` makes `../host_app/x.h` resolve here |
| `host_app/*.h` (11) | declaration-only forwarders: `cpu_state.h` (+ `ps2_float.h`, + the `VR_reg` component operators — P3-VROPS), `memory.h` (`memory::read/write/read_quad/write_quad` → `RecompAbi::Memory()` = the bound `FastMem`), `syscalls.h`, `sif_hle.h`, `gs_hle.h`, `hle_heap.h` (12 `HLE_002c*` + 5 `HLE_FUN_002a*` + `GetPtcRecord`), `ps2_threading.h`, `cdvd_stream.h`, `timer_hle.h`, `vu.h` (`Vu0Macro`/`Vu1Window`), `sif_rpc_handlers.h` |
| `recomp_abi.{h,cpp}` | `RecompAbi::BindMemory/Memory` + the inert globals (rows 9, 12, 24, 25, 27, 29): `g_logFile` unopened `std::ofstream`, census off, `StreamPollCompleteMarker` false, diag counters, `g_vu0`/`g_vu1` objects, `GetPtcRecord` nullptr |

Seam BODIES live in `engine/ee/cpu/seams.cpp` (parks / interim parks / inert) and in
`games/<t>/hle/glue/` (title pass-throughs). The dispatch surface the generated common TU DEFINES is wrapped,
never redefined, by `engine/ee/cpu/dispatch_table` and `shadow_call_stack`. Retired with the in-tree
recompiler (Phase 9.1). Lint: `host_app/` is the one "generated-code ABI" exemption in `lint_one_class.py`.
