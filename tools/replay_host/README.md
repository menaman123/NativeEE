# tools/replay_host -- the tier-1 replay harness

`ps2r_replay_host` replays the recorded calls of ONE recompiled function against a candidate body on a scratch memory
image and reports, per call, whether the candidate's effects equal the recording's. It is the engine half of
recomp-agent's tier-1 verification (the Python driver `tools/tier1.py` and the MCP tool `verify_candidate` live in the
recomp-agent repository; its `docs/VERIFY.md` describes the loop).

## What it links

The generated bodies (`libps2r_title_recomp.a`, whole archive: the common TU's function map, `update_function_impl`,
the probe table, the 6,575 bodies), the engine's memory objects (`engine/ee/mem/`: `FastMem`, `MainMemory`,
`AddressMap`, `MmioRouter`, `Scratchpad`), the PS2 float helpers (`engine/ee/cpu/ps2_float.cpp`), `recomp_abi.cpp`
(the memory binding, `g_logFile`, the census stubs) and `seams.cpp` here, which stands in for the engine behind the
generated code:

| seam | here |
|---|---|
| `recomp_probe_enter/exit/unwind` (fn_hooks.cpp) | the probe depth and the callee sequence |
| `AccessTracer::OnRead/OnWrite` (access_tracer.cpp) | the per-byte state: seeded / written / read-by-candidate / poisoned; uncovered reads |
| `ps2r_ee_yield_point`, `g_ee_backedge_budget` (seams.cpp, ee_yield_point.cpp) | a runaway-loop guard (`--max-trips`) |
| `runtime_syscall_dispatcher`, `handle_mtc0_write` | recorded per call, no effect |
| `ps2_threading::exitThread`, `lookupThreadInfo` | abort the record / null |
| `VuMacroCommitFlags` (vu_macro_bridge.cpp:122-135, copied) ; `Vu0Macro::Execute`, `Vu1Window::Execute` | the flag arithmetic ; counted as unsupported |
| `DispatchTable::GeneratedBody/TraceBody/ObserveInstall` | identity wrappers (no proof metadata) |
| `ShadowCallStack::RejectedPush/StackEmptied`, the crash-ring gates, `recomp_fcf_miss`, `g_vsync_frame` | inert |

It therefore needs the title's generated code and `PS2R_TRACE_ACCESS=ON` (the shards' accessors report every load
and store; without them neither coverage nor the cheap per-record reset exists). The executable exports its symbols
(`ENABLE_EXPORTS`), so a candidate shared object built with unresolved symbols (`-undefined dynamic_lookup` on macOS)
binds `memory::`, `recomp_call`, the float helpers to THE SAME objects the generated code uses. A candidate must not
link engine libraries of its own (it would carry a second, unbound `g_bound_memory`).

## The replay, per record

1. Reset the image (an undo log of every byte touched since the last record; the I/O side table is cleared).
2. Seed: the record's READ set (first-seen values) into RAM / scratchpad / the I/O table; the stack arguments at
   `sp+0x10..0x1c`; every recorded-WRITE byte not in the read set gets the complement of its expected value (poison).
3. Registers: a0..a3, t0..t3, sp, ra; v1 = the recorded exit value (`--v1-entry seed`, default: a function that does
   not write v1 leaves the caller's, which the record has no entry value for); v0 = 0 (`--v0-entry seed` to change);
   `fesetround(FE_TOWARDZERO)` as the engine's guest thread.
4. Run: `recomp_call(id, fn, ctx)`. The candidate is installed with `update_function_impl` (hooked -> the map);
   without `--candidate` the generated body runs (identity). Every id's probe byte is set, so each call is bracketed
   (depth 1 = the candidate, 2+ = callees); accesses at depth 1 are the candidate's.
5. Callees (`--callees`): `recorded` = the callee's own record from the same corpus (caller == fn, a0..a3 equal; sp
   then ra break ties; several records with different effects = ambiguous) applied without running it; `original` =
   the generated body on the image; `auto` = recorded when unambiguous, else original. Installed as stubs through
   `update_function_impl` on every callee address the records name.
6. Compare: v0, v1 (`--ignore-v1` skips it), the recorded write set byte for byte outside `[sp - frame, sp)`
   (`--frame`, default 0x4000), extra stores by the candidate outside that frame, the callee sequence
   (`--lenient-callees` makes a difference a note).
7. Report (JSON): counts, verdict, `first_failure` (reason, first differing address, expected/got), `coverage`
   (recorded read bytes touched, write bytes compared, callee shapes with pass counts, how callees were served),
   `per_record` (`--verbose N`, failures first), `timing_ms`. Exit 0 = all pass, 1 = a failure, 2 = an error.

Addresses in the corpus are the tracer's normalised form (scratchpad kept as 0x7000xxxx, everything else
`MainMemory::Phys`); the host normalises the same way (`replay::Normalise`).

## The candidate prologue

`replay_abi.h` is the candidate's precompiled prologue (`ps2r_replay_pch` -> `<build>/tools/replay_host/
replay_abi.pch`): the generated header, the eleven ABI forwarders, the standard headers, plus `ps2r_call(addr, ctx)`
and register helpers. `replay_toolchain.json` beside it is the recipe (compiler, flags, defines, includes, pch, host,
generated dir) a driver follows: clang refuses a PCH whose options differ from the TU's, so the flags are spelled out.
