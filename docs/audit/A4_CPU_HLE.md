# A4 — CPU / VU / HLE / IOP / TIMING / OS Audit

Stage: A4 of the NativeEE audit (the refactor of the original Ps2Recomp tree) (session s113).
Date: 2026-09-22.
Subject: `$PS2R_OLD_ROOT` (read-only; no builds, no runs, no git writes).
Method: file inventory, targeted greps and header/source reads via scripts under `/tmp/s113/a4/` (01_inventory.sh … 09_hooktable.py). Every claim below carries a file (and where useful a line) reference verified in this session.

---

## 0. Top-10 findings

1. **Three overlapping hook layers install behavior at EE addresses with no single registry.** (a) 191 `install_dual_hook` sites in `host_app/main.cpp` (156 distinct addresses; definition at main.cpp:464); (b) 235 `install(0x…)` calls in `host_app/manual_switch_dispatchers.cpp` wiring 222 machine-restitched `fun_*_body.inc` bodies + raw-string bodies (host_app/fun_*.inc, 222 files); (c) `sce_hle::register_sce_hle_overrides()` (host_app/sce_hle/sce_hle_registry.cpp) binding SDK-library handlers to addresses matched from `crash_matches_full.json`. All go through the generated `update_function_impl` seam, but the total override surface (~540+ address bindings) is discoverable only by grep. The refactor needs one declarative hook manifest (address → kind → provenance → record).

2. **`main.cpp` is 845 KB and is mostly diagnostic probe archaeology.** Of the 191 dual-hook sites, only ~5 are behavioral fixes (classification in §4.3); ~130 are read-only investigator probes that the in-source comments themselves mark "safe to remove". The gate-purge stage can delete the bulk of main.cpp once the behavioral 5 (plus the S8-ARGV boot argv setup, main.cpp:~530, and the S96-BUDGETEMU budget hook, main.cpp:3254-3611) are promoted to named modules.

3. **The timing system is one good design (time_domain.h) buried under 40+ interacting env gates.** `timedom::g_emu_count` ([S94-TIMEDOM], host_app/time_domain.h) is already the single emulated tick domain with a regime detector and lead governor — but T0 subtick (PS2_T0_SUBTICK), budget emulation (PS2_BUDGET_EMU + PhaseClassifier), Mode Q (PS2_QUANTUM), turbo (PS2_TURBO/PS2_LOAD_TURBO*), heartbeat re-clock (PS2_HB_TICKCLOCK), EE pace (PS2_EE_PACE/PS2_EE_RUNAHEAD), freerun (PS2_VBLANK_FREERUN), and CDVD instant/budget (PS2_CDVD_INSTANT/PS2_CDVD_BUDGET_KB) each carry their own default and legacy fallback. 40 of the 556 distinct `PS2_*` env vars are timing-related. §5.4 proposes the consolidation.

4. **556 distinct `PS2_*` env vars gate the runtime** (grep census, /tmp/s113/a4/env_vars_list.txt). Most are latch-once diagnostic gates defaulting to the shipped behavior, but defaults are scattered (some default ON: PS2_TIME_EMU, PS2_QUANTUM, PS2_FASTMEM, PS2_VU1_HLE, PS2_VU1_HLE_DIRECT, PS2_GS_DRAWIR, PS2_T0_SUBTICK, PS2_VU_CLAMPELIDE; some OFF: PS2_EE_PACE, PS2_LOAD_TURBO, PS2_MEMWATCH). The refactored program needs a single typed config table with an explicit shipped-default column; the gate-purge procedure must delete the =0 legacy arms that exist only as A/B evidence.

5. **Generated-code volume is the dominant build cost and is 3-tier:** EE recomp = 91 MB C++ across 32 shards (recompiled_shard_000..031.cpp, 2–4.3 MB each) + a 2.6 MB common TU + 521 KB header declaring 9,327 `FUN_XXXXXXXX(CpuContext&, uint32_t addr=0)` functions; VU AOT = 233 MB across 29 `vu_aot_generated_*.inc` + 351 MB static lib (build-v2/libvu_aot_lib.a) — currently **disabled** (vu_aot.ini `corpus_compile = none`, runtime rides the cached-interpreter tier); IOP recomp (STREAM.IRX) = 2.5 MB. CMakeLists.txt records the pre-shard pain: one 53 MB VU AOT TU took 2068 s user / 2.6 GB RSS to compile; sharding (K=32) exists purely to make compilation feasible.

6. **The guest-thread model is std::thread-per-PS2-thread with 1 GB stacks.** `ps2_threading.{h,cpp}` (ran-j/PS2Recomp State.h pattern) replaced the old fiber scheduler; `pthread_attr_setstacksize(…, 1 GB)` at ps2_threading.cpp:526 and :676 (comment history: 32 MB → 256 MB → 1 GB after an island-resume overflow at ~35k nested recompiled dispatch frames). Recompiled code recursion depth is unbounded because guest calls map to host C++ calls. This is the single hardest Vita-class blocker (§7-B1): a 512 MB device cannot give even one thread a 1 GB stack; the IR design must bound host stack depth (tail-dispatch loop or stackless continuations at park sites).

7. **The VU1 HLE program library is the project's crown jewel and is already modular in spirit:** 333 KB `host_app/hle/vu1_hle_programs.cpp` with namespaces ttx, pcs, lit, crys, skn, idx, zk, spr (+ family clip gate shared helpers); registry keyed by (image_hash, variant_hash, start_pc) (hle/vu1_hle.h); dispatch default-ON (PS2_VU1_HLE), typed-op DIRECT and DrawIR tiers default-ON; proven by an offline oracle (tests/vu1_replay --hle over vutrace corpora: vutrace_cases/ 62, vutrace_cases2/ 126, vutrace_collect/ 3,293 entries) with per-register tolerance diffing and a --catalog "which program next" workflow (docs/hle_harness.md). §3.5 specifies the module split.

8. **Memory-system correctness patches hide inside the diagnostic gauntlet.** memory.h implements a guarded inline fast path over a `std::vector<uint8_t>` 32 MB main RAM (PS2_FASTMEM default ON); the slow path is a ~3,300-line probe gauntlet in memory.cpp — inside which lives [KEY-REDIRECT], a *game-progression-critical pointer repair* that mutates state on an every-256-writes cadence and is deliberately NOT behind PS2_MEMWATCH (memory.h:150-160 comments). A behavioral fix living inside a diagnostic path is the highest-risk migration item in this subsystem. There is also a non-hardware `meta_arena_memory` (2 MB at EE phys 0x02000000, memory.h:15) that the allocator identity system depends on and the savestate serializes (META section).

9. **Savestate is a versioned, sectioned, CRC-checked format with an explicit cross-engine contract** (docs/SAVESTATE_SCHEMA.md: magic "PS2SAVE1", 28 fourcc sections, per-section CRC + trailer CRC, REQUIRED-flag forward-compat). The portability rule that makes it work is CONVENTIONS.md R5: *the host C++ call stack carries no PS2 state; state = CpuContext + RAM*, plus the generated shadow call stack (ee_call_stack_push/pop) and `recomp_fn_id_for` containment checks for resume addresses. Any IR/codegen change must preserve exactly this contract or savestates break silently — it belongs in the EQUIVALENCE plan as a first-class gate.

10. **The recompiler has a written, enforcer-backed semantics contract — the IR design's requirements doc already exists.** recompiler_tool/CONVENTIONS.md pins delay slots (DS1-DS7 incl. branch-likely nullification and 64-bit branch compares), register semantics (R1-R6: 128-bit GPRs with sign-correct 32-bit writes, $zero, HI1/LO1 pipes, no cross-site caching, function-ABI state contract), dispatcher/discovery rules (D1-D5), and emission hygiene (P1 probes env-gated default-silent, P2 byte-determinism, P3 license attribution) — each rule names its enforcer test. §1.3 maps these onto the IR design.

---

## 1. Recompiler pipeline

### 1.1 As-is: two engines, one translation core

| Piece | Location | Role |
|---|---|---|
| Legacy CLI | recompiler_tool/main.cpp (11.7 KB) | original entry: ELF + Ghidra JSON → recompiled.h/.cpp |
| v2 CLI | recompiler_tool/main_v2.cpp (27.8 KB) | [RECOMP-P2/P3a] own discovery pipeline (ELF → leaders → two-engine jump tables → multi-source functions); Ghidra JSON demoted to emission-compat input (aliases, true-gap fns, pin bodies), "NEVER a discovery input"; P4 drops it |
| Shared translation core | recompiler_tool/recompiler.cpp (**1.59 MB**, single file) | instruction → C++ emission for BOTH engines ("the REUSED legacy Recompiler (no new translation semantics)", CMakeLists.txt:73-75) |
| Discovery | recompiler_tool/discovery/ (leaders, jumptables, functions, report, rv2_decode) | v2-only function discovery |
| Emission driver | recompiler_tool/emit/driver_v2.cpp | builds v2 function set, drives sharded emission |
| Control flow | recompiler_tool/translate/control_flow_v2.cpp | v2 control-flow translation |
| Overrides | recompiler_tool/legacy/recomp_overrides_config.h + recomp_overrides.ini | generation-time pin channel (below) |
| Corpus audit | recompiler_tool/corpus/ (audit.cpp, recorder_gen.cpp) | [RECOMP-P4] corpus-as-audit: runtime records taken jr/jalr (site→target) pairs (PS2_RECOMP_CORPUS env; recorder emitted into recompiled.cpp), tool audits them against static tables |
| VU AOT generator | recompiler_tool/vu_aot_gen.cpp (71 KB) | separate `vu_aot_gen` executable; corpus cases → vu_aot_generated_*.inc |
| IOP recompiler | iop_recompiler/iop_recompiler.cpp (47 KB) | STREAM.IRX (R3000A) → iop_recompiled.cpp |
| Disassembler | rabbitizer (static lib) + EEAnalyze | MIPS R5900/R3000 decode |

Inputs: `game_files/example_title.rep/SLUS_123.45` (ELF), `game_files/ghidra_functions/extracted_functions.json`, `recomp_overrides.ini`, `switch_manual_enumeration.json` (ALL_MANUAL expansion), corpus pair files. CMake configure fails if the ELF or JSON is missing (BUILDING skill).

Outputs and shapes (measured in build-v2/):
- `recompiled.h` 521 KB — 9,327 `void FUN_XXXXXXXX(CpuContext&, uint32_t addr = 0)` declarations + entry.
- `recompiled.cpp` 2.6 MB common TU — no bodies under sharding; owns the runtime tables (`recompiled_functions` std::map at recompiled.cpp:148, `function_ranges`, sorted snapshot tables + `initialize_recompiled_functions()`), `find_containing_function` fallback dispatch, the corpus recorder, FTRACE, the thread-local shadow EE call stack (EE_CALL_STACK_MAX=128) with `ee_call_stack_push/pop`, `ee_get_call_stack_*`, `recomp_mark_hooked`, `update_function_impl`, `recomp_fn_id_for(_containing)`.
- `recompiled_shard_000..031.cpp` — 32 self-contained shards, 2.07–4.30 MB each, ~91 MB total (PS2_SHARDED_RECOMP=ON default; OFF = "exact pre-shard behaviour -- one 91 MB / 1.63 M-line recompiled.cpp", CMakeLists.txt:316).
- `iop_recompiled.{h,cpp}` — 11.5 KB header + 2.49 MB body operating on a 32-bit `IopContext` (gpr[32]/hi/lo/pc, single global instance; host_app/iop_context.h).
- VU AOT: 29 × `vu_aot_generated_*.inc` (3.4–9.3 MB, 233 MB total) aggregated into 32 `host_app/vu_aot_programs_N.cpp` TUs → libvu_aot_lib.a 351 MB. Currently generated-but-empty-registry: vu_aot.ini `corpus_compile = none`.

Compile cost (evidence in CMakeLists.txt comments): shard split exists because the single-TU forms were unbuildable in practice (53 MB VU AOT TU: 2068 s user, 2.6 GB maxrss, line ~207; K raised 8→24→32 in lockstep with vu_aot_gen kChunks); shards get optimization while the common TU stays -O0; vu.cpp optionally -O3 (PS2_VU_O3 default ON, "measured -0.6..-0.9 ms/f at hub1"). The mac build also keeps a parallel `build-v2-nog/` tree (file-list identical to build-v2; a no-graphics variant).

Function-body shape (per CONVENTIONS.md + recompiled.cpp reads): every function is a `switch (addr)`-headed dispatch over its block leaders (D1 — mid-entry support), straight-line C++ per MIPS instruction reading/writing `ctx`, `Label_*` gotos for internal edges, jump tables emitted from ELF-recovered target sets (D3), and every indirect site retaining the `find_containing_function` runtime-dispatcher fallback (D4). Wrappers push/pop the shadow call stack.

### 1.2 Override / pin channel

- `recomp_overrides.ini` — generation-time, v2-engine-only. Repeatable `pin = 0x…` (or `ALL_MANUAL`) makes an address emit/install its LEGACY manual body through the existing install seam. Pin closure: pinning a v2 container that absorbed other legacy entries auto-demotes the absorbed aliases to their own legacy bodies (e.g. `pin = 0x001017c8` reports "338 pinned"); pinning an absorbed member directly is diagnosed and ignored. Schema pinned by tests/test_recomp_overrides_config.cpp.
- Manual bodies today: `host_app/manual_switch_dispatchers.cpp` (115 KB) `#include`s 222 `host_app/fun_*_body.inc` machine-restitched bodies plus hand-written raw-string bodies, installing 235 addresses via its `install(0x…, impl, name)` lambda (public installer `install_manual_switch_dispatchers()`, line 1988; registers each address in a sorted manual-addr registry for savestate chain classification — manual bodies have **no mid-entry**: `(void)addr`). Three fully hand-written dispatchers are documented in the header comment: FUN_00203358 (particle SM), FUN_002a13e0 (bullseye SM vtable dispatcher), FUN_00255f30 (text/decal render+tick with VU0-macro sin/cos folded to std::sin/cos). recompiler_tool/legacy/README.md records the P3 plan to relocate all of this under recompiler_tool/legacy/manual_bodies/ (not yet done — the directory does not exist).
- Registry/corpus option: vu_aot.ini (`corpus_compile = auto|all|none`, `collection_enabled`, `collection_per_image`) governs the VU AOT registry contents at build time; PS2_RECOMP_CORPUS arms the EE jr/jalr corpus recorder at runtime.

### 1.3 What an IR-based design (MIPS → IR → host C++/other target) must preserve

The emission semantics are already specified as testable rules (recompiler_tool/CONVENTIONS.md); the IR contract is their union:

- **Delay slots** DS1-DS7: slot-before-transfer ordering incl. call-argument materialization (DS2); branch condition latched before the slot (DS3); branch-likely slot only on the taken path (DS4); branch compares on full 64-bit values (DS5); slot words fetched from the .text image when block metadata splits them (DS6); jr-$ra returns after slot effects (DS7). In an IR these become: explicit slot node sequenced before the terminator, and likely-branches lowering to guarded slot execution.
- **Register model** R1-R6: 128-bit GPRs where 32-bit results sign-extend into SD[0]; $zero never written; sign-typed immediates; **no cross-site register caching** — every statement reads ctx afresh across call/dispatch/label boundaries (an IR may only cache within a block if spills at every boundary are proven); HI/LO vs HI1/LO1 pipe separation; the R5 function-ABI/savestate contract (host stack carries no PS2 state).
- **FPU semantics** (host_app/cpu_state.h): non-IEEE "PS2 float" — `ps2_float_clamp` (exp==0xFF → ±MAX_FLOAT, no NaN/Inf propagation); EE thread runs FE_TOWARDZERO globally (set at main.cpp:605 and per guest thread at ps2_threading.cpp:366); div.s/sqrt.s/rsqrt.s swap to FE_TONEAREST for the operation and must be computed in single precision (double-then-truncate is documented as wrong: 1 ULP divides flipped VU1 CLIP verdicts — cpu_state.h:26-38). VU side additionally truncates (chop) everywhere: vu.cpp's VuChopRoundGuard RAII sets FE_TOWARDZERO around Execute, HLE programs use the same ChopGuard.
- **Syscall boundary**: `syscall` lowers to `runtime_syscall_dispatcher(GPR r3 = $v1, ctx)` (emitted form verified at build-v2/recompiled_shard_000.cpp:111; same call in the dynamic-interpreter path at syscalls.cpp:1456); ei/di/sync/eret/mtc0/mfc0 all have defined handlers (handle_mtc0_write semantics at syscalls.cpp:95+).
- **Hook points (frozen surfaces)**: address-keyed value replacement through `update_function_impl` preserving pointer/reference stability of per-site caches (D5, "the R137 contract"); the shadow call stack push/pop wrappers; `recomp_fn_id_for` dense ids; default-argument mid-entry `addr` dispatch; `find_containing_function` universal fallback (D4). The savestate system, the sce_hle registry, manual dispatchers and all main.cpp hooks consume these five surfaces.
- **Determinism** P2: identical inputs → byte-identical output (no timestamps/paths/pointer ordering).
- **VU0 macro-mode COP2**: emitted inline in EE function bodies (recompiler.cpp ~:23098-24600: broadcast/accumulator arithmetic with 4-bit xyzw dest-mask helpers, vmulq/Q ops, lqc2/sqc2, cfc2/ctc2 incl. interlocked variants, VCALLMS running the VU0 micro synchronously). IR needs COP2 ops as first-class typed nodes sharing the clamped-float semantics above.

### 1.4 Essential vs accidental

**Essential:** the CONVENTIONS semantic rules; the two-input discovery (ELF + optional match JSON); the pin/override channel concept with pin-closure; the corpus-as-audit loop; sharded emission (any full-game recompile output must be shardable for compile cost); the frozen hook surfaces; deterministic emission.

**Accidental:** the 1.59 MB single-file recompiler.cpp (translation, per-address special cases, embedded manual-body strings and diagnostics probes all in one TU — the "probes go in recompiler.cpp" rule concentrated diagnostics here); two CLIs sharing one translation core (v2 was meant to replace legacy — finish it); Ghidra JSON as emission-compat crutch (P4 plan already drops it); manual bodies living in host_app/ as 222 loose .inc files instead of the planned legacy/manual_bodies/; the inject_fun_1017c8_helpers.{h,cpp} special-case; generated code #including host_app headers by relative path (`../host_app/...`), inverting the layer direction; default-argument `uint32_t addr = 0` on 9,327 functions (an artifact of mid-entry dispatch — an IR can emit explicit entry-point tables instead).

---

## 2. Runtime glue

### 2.1 CpuContext

`host_app/cpu_state.h`: `EmotionEngineState` (typedef `CpuContext`) = CPURegs (32× 128-bit `GPR_reg` unions UQ/UD/UL/US/UC + signed views, HI/LO incl. pipe-1, CP0 32× u32, sa, pc), FPUregs (32× FPRreg f/UL, ACC, control), VUregs (VU0 macro state: VF[32] `VR_reg`, VI[16], ACC/Q/P/I/R, MAC/status/clip flags). Types come from `common/Pcsx2Types.h` (u128 etc.). The PS2-float helpers (`ps2_float_clamp`, `ps2_div_s`, `ps2_sqrt_s`, `ps2_rsqrt_s`) live here and are the single source of FPU clamp/round semantics (§1.3). `VR_reg` carries operator overloads that clamp on every element op — convenient but it hides clamping cost in innocuous-looking `a + b` expressions (accidental; an IR should make clamps explicit ops).

### 2.2 Memory

- Backing stores are all plain `std::vector<uint8_t>` (memory.h:13-32): `main_memory` 32 MB, `scratchpad_memory` 16 KB, `meta_arena_memory` 2 MB (non-hardware allocator arena at EE phys 0x02000000 — host invention, savestate-serialized), vu0/vu1 code+data memories, `gs_vram` 4 MB, IOP RAM 2 MB (iop side). **No mmap/VirtualAlloc anywhere** (grep: zero hits outside imgui) — "fastmem" here is not page-table fastmem, it is an inline bounds-checked fast path.
- Fast path ([R130-FASTMEM], memory.h:55-260): `read<T>/write<T>/read_quad/write_quad` short-circuit `(addr & 0x1FFFFFFF) + size <= main_memory.size()` to memcpy; everything else (MMIO 0x10000000-0x1000FFFF and GS privileged 0x12000000-0x12001FFF via `is_io_register`, scratchpad `(addr & 0x70000000)==0x70000000`, HW window incl. [R121-MTVU] VU1-mem sync hooks, meta-arena, BIOS mirrors, OOB) falls to `*_slow` in memory.cpp. Quad ops apply the R5900 `addr &= ~0xF` mask before the check. Gates: PS2_FASTMEM (default ON), `g_fastmem_force_slow` counter (watchpoints), PS2_MEMWATCH (default OFF) re-arms the verbatim probe gauntlet.
- The slow path is a ~3,300-line diagnostic gauntlet **containing one live mutator**: [KEY-REDIRECT] pointer repair, every-256-writes cadence, deliberately outside PS2_MEMWATCH ("repairs a game-progression-critical pointer and stays unconditionally live", memory.h:150-160). Migration must lift this into a named game-fix module.
- `translate_address(addr, size)` orders: scratchpad → HW regs (with MTVU sync hooks on VU1 windows) → main RAM+mirrors (KUSEG/KSEG0/KSEG1 via `& 0x1FFFFFFF`) → meta-arena → BIOS. VU1 micro writes through the EE window bump `g_vu1_micro_extgen` so image-hash memos never go stale (memory.h:21-30).

### 2.3 Threading

Actual host-thread census (grep of std::thread/pthread creations):

| Thread | Created at | Role |
|---|---|---|
| main | process | SDL window + event loop, GUI |
| cpu_thread | main.cpp:14180 | boot orchestration, hook installs, then PS2 tid=1 |
| gfx_thread | main.cpp:14181 | GraphicsThreadFunc: RenderQueue consumer, GL backend, presents |
| vblank_thread | main.cpp:4679 | VBlankWorkerFunc ~60 Hz ticker (§5) |
| per-PS2-thread | ps2_threading.cpp:502-526/660-676 | one pthread per guest thread, **1 GB stack each**, FE_TOWARDZERO set per thread (:366); park/wake via per-thread mutex+cv |
| MTVU worker | vu1_async.cpp (pthread, 32 MB stack) | [R121-MTVU] VIF1/VU1 execution off the EE thread; PS2_MTVU gate; segment queue + GifMtvuMarker arbiter ordering; batch staging 32 KB/32 records |
| GIF arbiter drain | gif_arbiter.h:530 | orders PATH1/2/3 packet streams to the GS |
| IOP worker threads | iop_stubs.cpp:750 | one std::thread per IOP guest thread (STREAM.IRX recomp) |
| IOP hard timer | iop_stubs.cpp:1186 | StartHardTimer ticker = STREAM.IRX 60 Hz heartbeat (re-clockable onto the tick domain, PS2_HB_TICKCLOCK) |
| IOP deferred-event | iop_stubs.cpp:1855 | deferred event dispatch |
| savestate writer | savestate_writer.cpp:127 | async slowspot capture writes |
| bug-report workers | bug_report.cpp:445,543 | detached snapshot bundlers |
| GUI scanners | gui/savestate_browser.cpp:123, gui/vu_microcode_section.cpp:267 | detached |
| SDL audio | sound/mixer.cpp | device callback (audio is time master) |

The **fiber layer is legacy**: platform/macos/fiber_macos.cpp implements CreateFiber/SwitchToFiber over ucontext (deprecated on macOS) and platform/win32 the Windows equivalents, but they serve `ps2_scheduler_old_fiber.cpp` (87 KB), which the std::thread model replaced ("Replaces the fiber-based PS2Scheduler…", ps2_threading.h:3-5). main_old.cpp likewise. The steering doc's fiber description is out of date. Dead-code candidates for the purge list: ps2_scheduler_old_fiber.cpp, platform/*/fiber_*, main_old.cpp, the 20 test_* preservation/bug-condition pairs at host_app root that pin the OLD architecture, test_rendering_architecture.cpp (65 KB).

### 2.4 Dispatch tiers and hook install

Call dispatch is three-tier, all owned by the generated common TU: (1) direct emitted calls FUN_x(ctx) for static jal targets; (2) `recompiled_functions` std::map for indirect/jalr with static table cases first (D3); (3) `find_containing_function` over `function_ranges` + sorted snapshot arrays as universal fallback (D4), entering mid-function via the `switch(addr)` header. A fourth safety net exists: `dynamic_decode_and_execute` (syscalls.cpp:2177) — a rabbitizer-driven EE interpreter (100k instruction cap) used as last-resort execution of non-recompiled code. Hook installation flows through `update_function_impl(addr, fn)` which keeps map + ranges + snapshots coherent; `install_dual_hook` (main.cpp:464) additionally wraps rather than replaces (wrapper receives the original lambda). `recomp_mark_hooked(addr)` tags hooked entries for the save system.

### 2.5 Syscalls and kernel HLE

`runtime_syscall_dispatcher` (syscalls.cpp; 271 case labels incl. negated/i-variants) implements the SDK-3.0.1 kernel surface: thread ops (Create/Start/Exit/Terminate/Sleep/Wakeup/Suspend/ChangePriority/Rotate), semaphores, event flags, InitMainThread/InitHeap, SIF (SetDma/DmaStat/BindRpc/CallRpc/SetReg/GetReg/SetDChain/StopDma), GS (PutIMR/GetIMR/SetGsCrt), INTC/DMAC handler add/remove + enable/disable, FlushCache, GetMemorySize, Deci2, RFU stubs. Threads/semas/evflags live in `ps2_threading.cpp` tables (ThreadInfo/SemaInfo/EventFlagInfo with per-object mutex+cv, kernel error codes KE_*); INTC (intc.h: STAT/MASK, handler registry, DispatchInterrupt/DispatchCause running guest handlers via recompiled dispatch) and DMAC (dmac.cpp: 10 channels, chain modes, handler queues) are savestate-serialized including handler registrations (INTC/DMAC sections). VBlank semaphores 1 and 3 + INTC VBON/TIM2/VBOF are raised by the vblank worker (§5).

### 2.6 Essential vs accidental (runtime glue)

**Essential:** CpuContext layout + PS2-float helpers; the 4-tier dispatch with mid-entry; update_function_impl seam; kernel HLE tables + park/wake semantics with savestate park records (NotePark/RegisterThreadCtx at the park sites); memory region map incl. VU-window generation bumps; MTVU ordering contract (markers) as an optional tier.

**Accidental:** 1 GB stacks (symptom of unbounded host-stack recursion — must become a bounded dispatch discipline); the probe gauntlet in the memory slow path (all PS2_MEMWATCH content) and its 130+ embedded watch tags; [KEY-REDIRECT] living in memory.cpp; the dynamic interpreter living in syscalls.cpp; ~20 root-level test_*.cpp pairs pinning the pre-refactor architecture; fiber platform layer + old scheduler; `recompiled_functions` being a std::map (ordered map lookup on the hot indirect path — the snapshot arrays exist to compensate); meta-arena as a raw vector rather than a named subsystem.

---

## 3. VU

### 3.1 VU0 (macro + micro)

VU0 macro mode = COP2 instructions emitted inline into EE function bodies by recompiler.cpp (§1.3), operating on `ctx.vuRegs`; dest-mask helpers implement the 4-bit xyzw field semantics; Q/I/ACC and flag updates mirrored; VCALLMS runs the VU0 microprogram synchronously (vu.h:594 comment: Execute used to spin the self-branch); vu0_code/data memories exist (memory.h:17-18) and are savestate sections (VU0C/VU0D). tests/test_vu0_shadow_replay.cpp pins VU0 behavior. the title's VU0 micros and the full VU1 micro contain zero EFU/MFP/WAITP ops (vu.h:843) — the EFU pipeline can stay unimplemented.

### 3.2 VU1 execution tiers

vu.h/vu.cpp (64 KB + 658 KB) implement a four-tier ladder, selected by PS2_VU_MODE + registries:

1. **interp** — instrumented reference interpreter (default mode enum; the oracle's ground truth; carries VUTRACE capture hooks, [VU1-LOCKSTEP] dumps, flag-read traces).
2. **cached** — per-image predecoded block cache (`VuCachedProgram`, keyed by whole-image content hash `VuHashImageFast`), probe-free dispatch (`ExecuteCached`), bit-exact-with-interp **by contract** (pinned by vu1_replay --compare); hot-op fast kinds (VUK_U_*/VUK_L_* classifications: NOP 52.8% census, MADD/MULA families, MULq, CLIP, FTOI0/4, ITOF0, LQ/SQ/ILW/IADDIU/IBEQ/IBNE/MOVE/MTIR…) executed by always-inline bodies in vu_fast_exec.h (extracted verbatim from vu.cpp for ODR-safe sharing with AOT TUs); auto-fallback to interp when trace envs are armed. PS2_VU_FASTPACK=0 restores pre-classification dispatch.
3. **AOT** — build-time whole-program translation of corpus cases (vu_aot_gen.cpp → 233 MB .inc → vu_aot_programs_N.cpp), registry keyed by the same image hash (`VuAotProgramDesc` slices on VuCachedProgram); JR/JALR misses delegate to ExecuteCachedResume; per-image census counters (run_bundles, delegate_entries). **Currently disabled** (vu_aot.ini corpus_compile=none) — the AOT specialized bodies also carry multi-MB host stack frames (~2.4 MB each measured, vu1_async.cpp:80-90 [W12-AOTSTACK]), which forced the 32 MB MTVU worker stack. Clamp-elision (PS2_VU_CLAMPELIDE default ON) skips generator-proven-idempotent re-clamps; PS2_VU_NOCLAMP is a measurement-only semantics-changing gate.
4. **HLE** — native C++ programs replacing whole microprogram runs (below), default ON (PS2_VU1_HLE unset→ON since [D5]); misses fall to cached tier.

`vu_set_mode`-style invalidation: mode-independent signal on micro-mem writes (vu.cpp:1797); MTVU worker owns execution under PS2_MTVU with `Vu1Context` private CpuContext (savestate VU1R section).

### 3.3 VU1 HLE program library

Contract (hle/vu1_hle.h): an HLE program replaces one MSCAL/MSCNT-to-E-bit run. Input = {micro 16K, data 16K, start_pc, TOP/TOPS/ITOP/ITOPS, entry VUregs, image_hash}; Output = {ordered raw GIF kicks, data_writes (byte-exact post-run data-memory mutations), optional full post-run register file (has_regs, [VU1-HLE-R2] raw-bit oracle-proven)}; a program may decline (→FALLBACK). Identity model: the title ships ONE base program 0x0000-0x171F + MPG-patched per-material subroutines ≥0x1720 (kVariantBase); registry `Entry{name, image_hash, variant_hash, start_pc, fn}` with lookup order exact-image → variant hash, start_pc pinning (0 = dispatcher, 0x1748 = sprite/wave direct entry). `Analyze()` extracts prefix/variant hashes + the QW3 dispatch chain (JALR walk at 0x0028).

Implementation (hle/vu1_hle_programs.cpp, 333 KB): namespaces and blocks at lines — ttx (297 & 679: family tristrip emitters + shared ClipGateB0 clip gate), pcs (2146), lit (2478), crys (3011 & 5386), skn (3650 end), idx (3788), zk (4055/4310/4703 — three blocks), spr (5110); plus a "fam" clip target handled inside ttx ([CV3C] image 0x713e44016d5f4840). Shared helpers reimplement VU semantics bit-exactly on the host: clamped mul/div (VuDiv sign handling), saturating FTOI, raw-bit MOVE/LQ/SQ/MAX, near-clip fan emitters with MINI/MAX lane helpers, ChopGuard RAII for FE_TOWARDZERO — duplicated per namespace block in places (two ChopGuard definitions at :554 and :1974; float-bit helpers repeated). Registration is static-initializer based (`s_reg_* = (vu1hle::Register({...}), true)`), e.g. nine vs_family_tristrip registrations for nine image hashes at :610-635.

Runtime (hle/vu1_hle_runtime.cpp): on registered image, skip micro execution; hand raw kicks to the same GifArbiter path Op_XGKICK uses. Escalation tiers, each default ON with byte-identical fallback: raw packets → [VU1-HLE-DIRECT] typed gsdirect::Op streams (submitHleOps → ApplyHleDirectOps, skips drain-thread GIFtag parse) → [GS-DRAWIR] draw-level IR (gs_draw_ir.h DrawRec runs, submitHleIR → ApplyHleDrawIR; Phase D input). Per-kick fallback keeps arbiter slot order. Image-hash memos keyed on (micro_generation, instance) via g_vu1_micro_extgen.

### 3.4 Corpus + oracle

- Corpora: vutrace_cases/ (62), vutrace_cases2/ (126), vutrace_collect/ (3,293) binary Vu1Case snapshots (captured by the [VUTRACE] hook at VU1::Execute entry: micro+data memories, VIF1 regs, VUregs, start pc). Collection knobs: PS2_VUTRACE*, vu_aot.ini collection_enabled/per_image (≥30-frame spacing).
- Oracle: tests/vu1_replay.cpp (56 KB) — modes: legacy trace; --mode interp|cached; --compare (bit-exactness gate: fresh VU1 per run, byte-diff post data memory + final VUregs incl. TPC, exit 0 iff zero differing bytes); --bench; --dump-kicks (decode GIF packets a program XGKICKs); --hle (interp AND HLE, lower both to canonical GS register writes, diff with per-register tolerances: xy 1/16 px, z, rgba, st 1e-5, --exact, --strict-packets; identical state-rewrite collapsing except PRIM); --catalog (per image: hashes, dispatch chain, extent, cases, kicks, vertices, cached-tier bundle cost, PRIM types, coverage — "the which-program-next list"); --hle-ref proves the loop with the interpreter registered as HLE. Full corpus sweep FAIL=0 recorded as the [D5] default-ON evidence (vu1_hle_runtime.cpp:63-66). Companion oracles: vif_replay.cpp (94 KB), gs_replay.cpp, test_vu1_transform_gt.cpp.

### 3.5 What the HLE "program library" should look like as a module

- `vu/hle/contract.h` — Input/Output/Entry/registry (already exactly hle/vu1_hle.h; keep verbatim).
- `vu/hle/vu_bitmath.h` — ONE copy of the bit-exact helper set (clamp, VuDiv, FTOI saturate, lane masks, ChopGuard); today duplicated across namespace blocks and partially re-duplicated from vu_fast_exec.h.
- `vu/hle/programs/<family>.cpp` — one TU per namespace (ttx, pcs, lit, crys, skn, idx, zk, spr): splits the 333 KB TU, kills the three zk blocks / two crys blocks fragmentation, and makes per-family review possible.
- `vu/hle/registry.cpp` — data-driven registration table (name, hashes, entry, fn) replacing scattered static-init `s_reg_*` booleans; the table is also what --catalog compares coverage against.
- `vu/tiers/` — interp (reference, instrumented), cached (+fast_exec), aot (optional, behind the same registry key), with the mode ladder and invalidation in one place; the corpus/oracle tooling stays as the equivalence harness for all tiers.

### 3.6 Essential vs accidental (VU)

**Essential:** interp as bit-exact reference; whole-image content-hash identity + variant-hash family matching; the Input/Output HLE contract incl. data_writes and has_regs; oracle-first workflow (--compare/--hle gates before enabling); XGKICK→arbiter ordering; chop-rounding + clamp semantics; micro-write invalidation generations.

**Accidental:** 658 KB vu.cpp mixing reference semantics with dozens of embedded probes ([VUFLAG], LOCKSTEP dumps, ocean probes); duplicated bit-math helpers; three-block zk / two-block crys namespace fragmentation; static-init registration booleans; the currently-dead 233 MB AOT artifact tree + 351 MB lib rebuilt while corpus_compile=none; AOT multi-MB stack frames (a codegen style choice, not a semantic need); PS2_VU_NOCLAMP measurement arm.

---

## 4. HLE-sdk vs HLE-game

### 4.1 HLE-sdk (reusable, game-agnostic-by-API)

| Module | Files | Scope |
|---|---|---|
| libc HLE | host_app/libc_hle.cpp (22 KB) | [R138-LIBCHLE] guarded native memcpy/memmove/memset/strlen installed OVER dispatch entries via update_function_impl (gate PS2_LIBC_HLE default ON); any argument set the guard cannot prove safe falls back to the CAPTURED ORIGINAL; register semantics preserved because recompiled callers may read ANY register after a JAL (libc_hle.cpp:19-30) |
| sce SDK registry | host_app/sce_hle/ — sce_hle_registry.cpp + libkernl, libpad (45 KB), libmc (41 KB), libmpeg (40 KB), libsdr (21 KB), libcdvd (18 KB), libipu (13 KB), libgraph, libdma, libscf, libdev, netcnfif; bindings generated by generate_bindings.py → sce_hle_bindings.inc | handler signature void(CpuContext&) with a0-a3 + v0 sign-extended; bound at boot to every matched address from crash_matches_full.json (sce_lib_finder output); first call logs a "FAKED" banner |
| Kernel/syscalls | host_app/syscalls.cpp + ps2_threading.* + intc/dmac | §2.5 |
| Loader | host_app/iop_loader.cpp (20 KB) | IRX load orchestration; sceSifLoadModule via libkernl |
| IOP runtime | host_app/iop_stubs.{h,cpp} (109 KB) + iop_context.h + build/iop_recompiled.cpp (2.5 MB) | STREAM.IRX runs RECOMPILED (R3000A→C++, IopContext global) against HLE stubs of its 12 imported IOP libraries (sysmem/intrman/ioman/thbase/thevent/thsemap/vblank/sifcmd/sifman/stdio/sysclib/cdvdman subsets); host threads per IOP thread; StartHardTimer 60 Hz heartbeat; IOP RAM 2 MB is savestate section IOPM |
| SIF RPC | host_app/sif_hle.cpp + sif_rpc_handlers.cpp (296 KB) | mailbox regs, RPC bind/call routing to module handlers (CDVD incl. sceCdReadClock fixed 2005-06-15, MC, pad, sound/SDR, STREAM.IRX slots) |
| CDVD/ISO | host_app/hle_heap.cpp (198 KB) + cdvd_stream.cpp | ISO9660 walker, deferred async read engine (g_deferred_iso_reads), DVD seek/transfer latency model, pread-based race-free IO ([U1-WIN] ps2_pread portability shim); FMV sector streaming |
| Pad | host_app/pad_input.cpp (50 KB) + input/ | scePad surface over SDL gamepad/kbm backends |
| Memory card | sce_hle/libmc.cpp + mc0/ | libmc HLE over host directory; MCHL savestate section |
| Sound | sce_hle/libsdr.cpp + sound/ (sound_core 51 KB, voice, vag_decoder, mixer) | SDR/libsd command stream → host mixer; SDL audio device is rate master |
| MPEG/IPU | host_app/mpeg_hle.cpp (122 KB) + ipu.cpp | sceMpeg + IPU decode HLE for FMV |
| GS/DMA-side sce libs | gs_hle.cpp (hle_sceGsSyncV etc.), sce_hle/libgraph/libdma | frame-boundary + init surfaces |

### 4.2 HLE-game (the title-specific)

Non-hook game-specific pieces: manual_switch_dispatchers.cpp (235 installs; 222 fun_*_body.inc restitched bodies; three fully hand-written SMs — §1.2); S8-ARGV retail argv ("cdrom0:\SLUS_123.45;1", argc=1) in init_cpu_context (main.cpp:~530, ps2_argv.h); [S96-BUDGETEMU] budget-predicate emulation for FUN_00191bd0 (budget_math.h + hook at main.cpp:3254-3611, gate PS2_BUDGET_EMU, synthetic base PS2_BUDGET_BASE=8800 + [S112-BOOTPHASE] PhaseClassifier two-consecutive-low-draw-frames rule); [KEY-REDIRECT] allocator pointer repair (memory.cpp write gauntlet); XGKick-timing + roundSprite GameDB analogues live on the GS side (out of A4 scope, see A3).

### 4.3 Complete install_dual_hook table (main.cpp)

191 call sites, 156 distinct EE addresses (some addresses hooked/chained more than once across sessions). Classification: FIX = behavioral change; DIAG = read-only probe (comment-confirmed); DIAG? = probe by construction, wording not explicit; DISABLED = present but not installed ("false &&" or relocated to recompiler.cpp emission). Heuristic caveat: row 83 (`0x1a98b8`) is labeled FIX only because its comment says "FIX upstream" — it is a capped probe; the confirmed behavioral set is `0x181560` (free-list corruption elimination), `0x1b36b8` (RENDINIT double-alloc free), `0x268440` (boot type-registry populate append), `0x2ad110` (MC loc-buffer overflow removal), plus the PS2_BUDGET_EMU hook at `0x191bd0` (rows 49/121, gated). Everything DIAG/DIAG? is purge-eligible (each says so or chains through the original untouched). Source of truth for each row: the in-source comment block immediately above the call site.

| # | main.cpp line | EE addr | tag/label | classification | purpose (from in-source comment) |
|---|---|---|---|---|---|
| 1 | 796 | `0x177168` |  | DIAG | selected channel word, flag 0x8e8, EE stream-object trio state). / Investigator probe for the s48 island-music loop; safe to remove. / |
| 2 | 845 | `0x2a92c8` |  | DIAG? | FUN_002a92c8 — generic job "attach fs_obj + maybe-issue read". |
| 3 | 870 | `0x2ac168` |  | DIAG | FUN_002ac168 — bank-job poll (done/busy predicate). Logs poll / PASSES and state transitions only (busy-repeat spam suppressed). |
| 4 | 900 | `0x1e1500` |  | DIAG? | FUN_001e1500 — the cmd-1 (slot open) enqueuer: args + mirror post-state. |
| 5 | 923 | `0x2ac1d0` |  | DIAG? | FUN_002ac1d0 — bank-job completion (ack 0x103 + resident bit). |
| 6 | 970 | `0x186900` |  | DIAG? | floats/hex//q/. Prefix "[M2Q-OUT]" is NOT filtered by the "[VU" / denylist. / |
| 7 | 1032 | `0x186900` | [M2Q-OUT] install_dual_hook(0x186900) | DIAG? | [M2Q-OUT] install_dual_hook(0x186900) |
| 8 | 1051 | `0x185180` |  | DIAG | Y-rotation quat to a0..a0+0xF from the euler cache). Logs f12 step, / a0, pre/post quat. / |
| 9 | 1082 | `0x1874b8` |  | DIAG? |  |
| 10 | 1140 | `0x191b10` |  | DIAG | visible in the game's own data. Investigator probe for the s40 / slide-jump timing loop; safe to remove. / |
| 11 | 1205 | `0x2b3548` |  | DIAG? | result the recompiled body computes, so the exact tick-vs-mask / interleave is measurable. Capped at 400 lines. / |
| 12 | 1252 | `0x22c330` |  | DIAG | Logs first 60 calls + first 60 calls with ra in the 18-batch / caller range (sp scratchpad callers). Tag passes TagFilterBuf. / |
| 13 | 1301 | `0x22c330` | [LOOKAT-IN] install_dual_hook(0x22c330) | DIAG? | [LOOKAT-IN] install_dual_hook(0x22c330) |
| 14 | 1318 | `0x2aa020` |  | DIAG? | force flag, raw entry words (state word, clock/deadline pairs, / obj+vtable ptr) before the call and the state word + v0 after. / |
| 15 | 1378 | `0x2aa020` | [LDSM] install_dual_hook(0x2aa020) | DIAG? | [LDSM] install_dual_hook(0x2aa020) |
| 16 | 1393 | `0x2a0778` |  | DIAG? | load-screen holds and what events/phases it processes, naming the / missing emitter upstream. / |
| 17 | 1425 | `0x2a0778` | [NODEPUMP] install_dual_hook(0x2a0778) | DIAG? | [NODEPUMP] install_dual_hook(0x2a0778) |
| 18 | 1445 | `0x1ed448` |  | DIAG? | CHANGE of (state, gate fields) to name the exact field holding the / host world out of the ticking state. / |
| 19 | 1525 | `0x1ed448` | [WORLDSM] install_dual_hook(0x1ed448) | DIAG? | [WORLDSM] install_dual_hook(0x1ed448) |
| 20 | 1557 | `0x294b18` |  | DIAG? |  |
| 21 | 1578 | `0x194040` |  | DIAG? |  |
| 22 | 1619 | `0x230df0` |  | DIAG | or bails at the arrive-check. Log every mover entry in the / window: ctl, obj = *(*(ctl)+8), obj pos(+0x30)/localpos(+0x40), / and f12 = dt. One run decides. |
| 23 | 1697 | `0x22f758` |  | DIAG? |  |
| 24 | 1770 | `0x22f758` |  | DIAG? |  |
| 25 | 1863 | `0x22f758` |  | DIAG? |  |
| 26 | 1946 | `0x22f758` |  | DIAG? |  |
| 27 | 2031 | `0x22f758` |  | DIAG? |  |
| 28 | 2115 | `0x22f758` |  | DIAG? |  |
| 29 | 2213 | `0x294b18` |  | DIAG | WB counter: a0 = node base at 0x294b18. |
| 30 | 2227 | `0x230df0` |  | DIAG | Mover counter: obj = *(*(ctl)+8) at FUN_00230DF0 entry. |
| 31 | 2243 | `0x22f758` |  | DIAG | Sampler: once per 10 frames, window F1220..1560. |
| 32 | 2345 | `0x18afa8` |  | DIAG? | Dirty-commit: a0 = node base. |
| 33 | 2392 | `0x186900` |  | DIAG? | M2Q converter 0x186900: a0 = dst quat, a1 = src matrix. The / bit3 path at 0x1943c8 passes a1 = node base (matrix at +0). |
| 34 | 2474 | `0x22f758` |  | DIAG? |  |
| 35 | 2757 | `0x1ce778` |  | DIAG? | more bits differ -> bitstream misalignment / wrong object. / Vtable-dispatched, so it MUST be hooked via install_dual_hook. / |
| 36 | 2787 | `0x1ce778` | [MAT-DESC] install_dual_hook(0x1ce778) | DIAG? | [MAT-DESC] install_dual_hook(0x1ce778) |
| 37 | 2805 | `0x202458` |  | DIAG? | (No emulated-stack walk: this recompiler uses C++ call/return, so the / MIPS stack holds no scannable return-address chain — freeRa is the lead.) / |
| 38 | 2826 | `0x2045a8` |  | DIAG? |  |
| 39 | 2880 | `0x1815f0` | [STRFREE-CALLER] hooks 0x1815f0= | DIAG? | [STRFREE-CALLER] hooks 0x1815f0= |
| 40 | 2881 | `0x181590` | [STRFREE-CALLER] hooks 0x1815f0= | DIAG? | [STRFREE-CALLER] hooks 0x1815f0= |
| 41 | 2896 | `0x203f88` |  | DIAG? | FUN_00203f88), the other is set by a shallow struct copy — that's the / aliasing bug. The caller ra names the function to fix. / |
| 42 | 2954 | `0x2b5dd8` |  | DIAG | key later reused) vs (B) ring-base aliasing (the stream ring base itself / collides with the key slot). Read-only. / |
| 43 | 3000 | `0x202bd8` |  | DIAG | Read-only. (install_dual_hook works for direct-jal-called functions like / these; a one-time sentinel confirms the hook fired.) / |
| 44 | 3037 | `0x203230` |  | DIAG? |  |
| 45 | 3064 | `0x204ef8` |  | DIAG? |  |
| 46 | 3108 | `0x181560` |  | FIX | no OOM regression. This eliminates the free-list corruption that put / region-1 (key-buffer) pointers into pool 0x31b138 and overwrote the key. / |
| 47 | 3130 | `0x181590` |  | DIAG? |  |
| 48 | 3219 | `0x2029d8` | [R144-LISTLEAK] pump watch pump(0x2029d8 | DIAG | [R144-LISTLEAK] pump watch pump(0x2029d8)= |
| 49 | 3220 | `0x203358` | [R144-LISTLEAK] pump watch pump(0x2029d8 | DIAG | [R144-LISTLEAK] pump watch pump(0x2029d8)= |
| 50 | 3282 | `0x191bd0` |  | DIAG? | regardless of any clock, and S57 drain-first ordering depends on / the spin exiting promptly -- correct in both time domains. / |
| 51 | 3628 | `0x1df7a0` |  | DIAG | / CPU-thread-only state (all recompiled code runs on the CPU / thread): per-call sub-results from the nested hooks. |
| 52 | 3635 | `0x1df430` |  | DIAG? |  |
| 53 | 3642 | `0x1dec38` |  | DIAG? |  |
| 54 | 3718 | `0x1dec38` |  | DIAG? | Guest pointers here are typically the game-thread STACK, which / may live in scratchpad (0x70000000..0x70003FFF) — accept both. |
| 55 | 3738 | `0x1e0290` |  | DIAG? |  |
| 56 | 3848 | `0x1df7a0` |  | DIAG? | FUN_001DF7A0(key, flag) -> slot ptr (0 = fail) |
| 57 | 3860 | `0x1df430` |  | DIAG? | FUN_001DF430(slot, hdr, key, campos, id5) -> id (-1 reject) |
| 58 | 3873 | `0x1e18e8` |  | DIAG? | FUN_001E18E8: the bake GIF-packet emitter (offscreen pair draws). |
| 59 | 3898 | `0x1e5e90` |  | DIAG? | FUN_001E5E90(slot, key) commit / FUN_001E6130(slot) release. |
| 60 | 3908 | `0x1e6130` | FREE | DIAG? | FREE |
| 61 | 3984 | `0x2049f8` |  | DIAG? |  |
| 62 | 4034 | `0x204a48` |  | DIAG? |  |
| 63 | 4035 | `0x204c38` |  | DIAG? |  |
| 64 | 4036 | `0x2045e8` |  | DIAG? |  |
| 65 | 4043 | `0x204640` |  | DIAG? |  |
| 66 | 4067 | `0x1f2c48` | [BIGBAL-CTOR] ctor# | DIAG | them). PCSX2 constructs these during .rm2 parse too but ends with / only 0x2e live blocks — its dtor runs. Count both on host; if dtor / count is 0, the parent teardown path never  |
| 67 | 4077 | `0x1f2c78` | [BIGBAL-DTOR] dtor# | DIAG? | [BIGBAL-DTOR] dtor# |
| 68 | 4121 | `0x2b4738` |  | DIAG | so chunk objects leak every tick. Log the slot object, its vtable, / the isDone fn target, and the object's first words (the fields / isDone likely checks) so the failing condition |
| 69 | 4162 | `0x1e94e8` |  | DIAG | and +0x2c is non-null on entry, every overwrite leaks one object — / that's the 745-live-block pileup. Log wrapper, chunk type, and / prior +0x2c. |
| 70 | 4200 | `0x1b36b8` | [RENDINIT] # | FIX | RENDINIT re-allocates, free the first call's big blocks via the / game's own free (FUN_00181590) — nothing has consumed them yet / (both calls are inside the same boot sequence). |
| 71 | 4324 | `0x2049f8` |  | DIAG | 0x2049f8 via recompiled_functions[] (verified), so the runtime dual-hook / intercepts them. Read-only: calls orig first, then inspects v0. / |
| 72 | 4391 | `0x202bd8` |  | DIAG | (and names the corruptor target for a follow-up write-watch). / Read-only; calls orig first, then inspects v0 (the returned node). / |
| 73 | 4464 | `0x2050b0` |  | DIAG | (successor) is a non-null small value (the poison) names the exact call. / Read-only; inspects regs before orig. / |
| 74 | 4554 | `0x1a6068` |  | DIAG | whether the textured branch would run. Compare against PS2 (text / entry should have bit 27 SET). Read-only. / |
| 75 | 4584 | `0x1a6068` | [CHAIN-TEX-GATE] install_dual_hook(0x1a6 | DIAG? | [CHAIN-TEX-GATE] install_dual_hook(0x1a6068) |
| 76 | 5874 | `0x255f30` |  | DIAG | The hook only runs the diagnostic; it always calls through to / the original function so behavior is unchanged. / |
| 77 | 5885 | `0x255f30` |  | DIAG | to route dispatch straight to the generated FUN_00255f30. Equivalent / logging is already provided by the [LOGO-EXT] override in / recompiler.cpp (per the "probes go in recompiler. |
| 78 | 5990 | `0x255b20` |  | DIAG? | our host's ch5 is never reaching the mesh loop (mesh count=0 / or the gate check fails), no text will render. / |
| 79 | 6398 | `0x2a9040` |  | DIAG? | times but the flags never have bit 0 set, we need to find what / should be setting bit 0. / |
| 80 | 6459 | `0x2aab28` |  | DIAG | to decide if the next read should proceed. Returns 1 to allow. / Log its args and return value. / |
| 81 | 6508 | `0x1aa900` |  | DIAG? | -> packets ARE built and the loss is downstream (DMA/VIF/GIF). / Gated scene_state(0x309718)==3, cap 60. / |
| 82 | 6580 | `0x1aa638` |  | DIAG? | elsewhere in FUN_001a9c70. / Gated scene_state(0x309718)==3, cap 40. / |
| 83 | 6648 | `0x1a98b8` |  | FIX | scale (text_desc+0x08) is 0 (FIX upstream). / Gated scene_state(0x309718)==3, cap 30. / |
| 84 | 6715 | `0x1a9c70` |  | DIAG? | nonzero scale, the glyph render pipeline itself is broken. / Gated scene_state(0x309718)==3. / |
| 85 | 6769 | `0x1f3998` |  | DIAG | Hook FUN_001f3998 (SkipReadPredicate) — "return 1 to skip read". / Called from 2a9040. Log args and rv. / |
| 86 | 6808 | `0x2abde0` |  | DIAG? | and the state byte it's applying, so we can spot the premature / completion that triggers CloseHandle before the payload read. / |
| 87 | 6846 | `0x2abe60` |  | DIAG? | `(flags & 3) == 1` (bit0 set, bit1 clear) → poll via FUN_002ad110. / Any other pattern → return 1 (done). / |
| 88 | 6892 | `0x2ad110` |  | DIAG? | / Hook each step to see exactly where the chain breaks in our build. / |
| 89 | 7158 | `0x264948` |  | DIAG | fewer releases, never unsafe). EE thread; pure counter bump / after the original body returns. / |
| 90 | 7172 | `0x1a1bb8` |  | DIAG? |  |
| 91 | 7189 | `0x2adaf0` |  | DISABLED | [POLL-COMPLETE]/[ADAF0-DIAG] FUN_002adaf0 interception RELOCATED to / recompiler.cpp (emitted into recompiled.cpp) so the hook lives at the / recompiler.cpp level. This host-side d |
| 92 | 7260 | `0x2b6b28` |  | DIAG? |  |
| 93 | 7294 | `0x2b40f0` |  | DIAG? |  |
| 94 | 7371 | `0x19d248` |  | DIAG | We gate on scratchpad context 0x70003d40 (the one feeding the 4 / failing lookups) to keep log volume manageable. / |
| 95 | 7467 | `0x2b57b8` |  | DIAG? |  |
| 96 | 7511 | `0x1c4b28` |  | DIAG? |  |
| 97 | 7563 | `0x1a0e58` |  | DIAG? | FUN_0019d248(param_3=0) runs, we know the right populator exists / — just not on the path that makes it run in time. / |
| 98 | 7615 | `0x1c0670` |  | DIAG? | population code path doesn't run on our host at all — and we'll / know which entries ARE populated for comparison. / |
| 99 | 7664 | `0x1c6360` |  | DIAG? | pass a pre-populated entry. If this fires with a0=0x3b0208 at / all on PCSX2 but not on our host, that's the missing path. / |
| 100 | 7704 | `0x1a5b10` |  | DIAG? | hashmap on our host, we'd see populate calls; if it's never / called at all, that's another gap to investigate upstream. / |
| 101 | 7744 | `0x2abec8` |  | DIAG? | If it IS set sometimes but the vtable isn't 0x2F6618/0x2F9C78, / the call goes to a different method. / |
| 102 | 7806 | `0x1aa2e0` |  | DIAG | We log param_1, param_2 (asset path), and post-call piVar7[6] / so we can see the loop bound directly. / |
| 103 | 7866 | `0x1b95c8` |  | DIAG? | param_1 should be a prefix string (e.g., "Crash" or similar); / param_2 is the resolver_group for the 4th target. / |
| 104 | 7911 | `0x1a0fd8` |  | DIAG | with param_3=1 (populate). Logging shows which assets get opened / and whether the call completes. / |
| 105 | 7962 | `0x1007c0` |  | DIAG | flag. Log the full +0x1c word and all extracted bits so we can / see what flags were active at boot. / |
| 106 | 8019 | `0x268440` |  | FIX | BOTH: the Default.rm2 load (ELSE) AND the type-registry / populate (FUN_0025c6f8). So we append the populate call. / |
| 107 | 8096 | `0x2682f8` |  | DIAG | [2682F8-DIAG] FUN_002682f8 sets bits 16,17 of *param_1 based on / param_3/param_4. Capture every call and log what bits get set. / |
| 108 | 8136 | `0x1015d0` |  | DIAG? | "command line" the game sees. Sets bit 6 of *(game_state+0x1c) / when argv matches "RB". / |
| 109 | 8183 | `0x17d130` |  | DIAG | argc<2 → fake-argv parsing path. Log argc/argv and the parsed / result before calling FUN_001015d0. / |
| 110 | 8213 | `0x255188` |  | DIAG? | (param_1 == 0x1a8ccf0). This tells us whether the tick runs at / all on the scene object, and whether the timer is elapsing. / |
| 111 | 8285 | `0x259af0` |  | DIAG? | result (highest bit index), and caller's `ra` so we can see / which callers get what masks. / |
| 112 | 8412 | `0x1a2de0` |  | DIAG? | populator-bearing objects. The caller's `ra` reveals the / upstream caller that SHOULD be running. / |
| 113 | 8447 | `0x1a2f10` |  | DIAG? | the "delete" method — tracking it shows lifecycle of those / handler objects. / |
| 114 | 8479 | `0x2b6138` |  | DIAG | Logging lets us correlate constructor invocations with later / ABEC8-DIAG dispatches that target vtable 0x306f60. / |
| 115 | 8506 | `0x2b6050` |  | DIAG | enqueues a FUN_002AB630 stub afterwards. Logging lets us see / which variant runs on our host. / |
| 116 | 8540 | `0x2b68f0` |  | DIAG? | the work-item vtables we care about (0x2F6618 / 0x2F9C78), and / lets us identify call sites by `ra`. / |
| 117 | 8604 | `0x2b4a78` |  | DIAG? | - How many "iterations" the inner loop performs / - Exit reason (queue drained vs early-exit) / |
| 118 | 8687 | `0x2b68f0` |  | DIAG | closure[1]=category, closure[2]=mgr. So we can label the cat0 logo / enqueue precisely and see the exact queue + counters it lands in. / |
| 119 | 8773 | `0x2b4c10` |  | DIAG | read-only probe confirms whether it fires during the loading / window and whether the q1 ring count actually decrements (pop). / |
| 120 | 8832 | `0x191bd0` |  | DIAG | layer and never calls orig. The [BH4-PROBE] anchor-identity dump / therefore lives in the R145 hook. Left in place (chained via / orig) so re-ordering installs stays a one-line cha |
| 121 | 8867 | `0x2ad1a0` |  | DIAG | Logging each iteration of FUN_002ad1a0's do-while loop would be / too noisy; instead, we log entry/exit with queue state diff. / |
| 122 | 8940 | `0x170ed0` |  | DIAG? | param_2 distribution and the return-address so we can link / each kick to its caller. / |
| 123 | 9012 | `0x170d78` |  | DIAG | Logging it tells us whether the state machine takes the / "register-channel" path or the "fire-and-forget" path. / |
| 124 | 9070 | `0x269d28` |  | DIAG? | param_1 is the archive (int*); param_2 is the type; param_3 is / the stream offset word; param_4 is the stream wrapper. / |
| 125 | 9152 | `0x269b10` |  | DIAG | holders? This hook logs param_3[0..2] and *param_4 at entry, so / we can compare record contents. / |
| 126 | 9213 | `0x264090` |  | DIAG | doesn't dispatch). Log the branch and the first 24 bytes of / stream data so we can see what's read. / |
| 127 | 9297 | `0x1a6ac0` |  | DIAG? | BEFORE the insert+populate decision, so we know whether the / hashmap lookup would have found an existing entry. / |
| 128 | 9372 | `0x1bb710` |  | DIAG? | final *(param_1+0x6c) = sub_obj vtable pointer, which tells / us the type. / |
| 129 | 9452 | `0x2b3be8` |  | DIAG | Also dumps the first 16 bytes of the list at list_head so we can / see the chain state on entry. / |
| 130 | 9553 | `0x205df0` |  | DISABLED | [DESER] deserialization probe RELOCATED to recompiler.cpp (emitted into / recompiled.cpp for FUN_00205df0) so ALL diagnostic probes live at the / recompiler.cpp level. This host-si |
| 131 | 9677 | `0x2b35a0` |  | DIAG? |  |
| 132 | 9920 | `0x2554b0` |  | DIAG? | Use install_dual_hook to handle both direct JAL and indirect / JALR dispatch paths (FUN_002554b0 is called via vtable). / |
| 133 | 9968 | `0x256830` |  | DIAG? | orange); chase the font/style obj param_1[0x22] + glyph build. / gate fails (low4<2) while screen shown -> timing (ch33 late). / |
| 134 | 10029 | `0x204530` |  | DIAG? |  |
| 135 | 10045 | `0x2045a8` |  | DIAG? |  |
| 136 | 10078 | `0x2ad110` |  | FIX | This removes the need for the buffer overflow entirely (loc buffer stays / intact -> the "checking memory card" string survives to the lookup). / |
| 137 | 10180 | `0x170ed0` |  | DIAG | at 0x1182ec0. These probes tell us whether the apply runs and resolve / the (heap) apply-vtable address. Pure logging; dual-installed. / |
| 138 | 10200 | `0x179d80` |  | DIAG? |  |
| 139 | 10239 | `0x2b2558` |  | DIAG? | Bit 34 is the "scene ready" predicate that FUN_002b35a0 checks / in case 2 of the boot state machine. / |
| 140 | 10599 | `0x173238` | 173238 | DIAG? | 173238 |
| 141 | 10600 | `0x175e20` | 175e20 | DIAG? | 175e20 |
| 142 | 10978 | `0x1a9120` |  | DIAG? | install_dual_hook so it fires on both direct-map and JALR / dispatch paths. / |
| 143 | 11071 | `0x2a2548` |  | DIAG? | paths too (the plain `recompiled_functions[]=...` pattern only / works for direct JAL call sites). / |
| 144 | 11096 | `0x2a2430` |  | DIAG? | / [MC-CARD-QUERY] Hook FUN_002a2430 — MC query method. / |
| 145 | 11122 | `0x2a13e0` |  | DIAG? | [MC-SM13E0] Hook FUN_002a13e0 — the OUTER state machine that / decides which MC operation to issue. / |
| 146 | 11219 | `0x2a0250` |  | DIAG? | before=state0, an EXTERNAL writer collapsed it between ticks; if / before=3 & after=0, FUN_002a0250 itself did it. a0 = mc_ptr. / |
| 147 | 11301 | `0x2a6b68` |  | DIAG? | whether it runs at the bullseye + the manager phase/gate before and / after, to confirm host is stuck "done" instead of re-kicking. / |
| 148 | 11335 | `0x2a11d0` |  | DIAG? | we can confirm the duration value on host (PS2: DAT_003097dc*2 = / 18000) and how many times case 3 (re)activates it. / |
| 149 | 11389 | `0x17d478` |  | DIAG? | and its vtable dispatch. This is created by FUN_00169348 and / called every frame by FUN_0017D478. / |
| 150 | 11443 | `0x177168` |  | DIAG | so we can tune pad readiness precisely (the [CASE2-DISPATCH] / diagnostic the pad_input.cpp comment refers to was never added). / |
| 151 | 11497 | `0x169348` |  | DIAG? | [MC-OUTER-CTOR] Hook FUN_00169348 — constructor for the outer / MC container object. / |
| 152 | 11527 | `0x2a8668` |  | DIAG? | [MC-KICK-OFF] Hook FUN_002a8668 — MC manager init that calls / sceMcInit + first sceMcSync. / |
| 153 | 11563 | `0x177168` |  | DIAG? | FUN_002a11d0(DAT_00309724, 2, 0) → MC CHECK CARD command. / This is the only path that kicks off the MC UI. / |
| 154 | 11631 | `0x173238` |  | DIAG | Case 1 (uVar7=1) issues the MC check. Log retval and whether / FUN_002a11d0 was reached. / |
| 155 | 11660 | `0x2a11d0` |  | DIAG? | [MC-CHECK-CMD] Hook FUN_002a11d0 — the ACTUAL function that / issues "check MC card" to the outer object (param_2=2). / |
| 156 | 11694 | `0x254ee0` |  | DIAG? | Hook FUN_00254ee0 (channel state transition — called from FUN_00259b20) |
| 157 | 11728 | `0x255040` |  | DIAG? | Hook FUN_00255040 (channel start — called from FUN_00259bc0) |
| 158 | 11761 | `0x259a90` |  | DIAG? | Hook FUN_00259a90 (another channel update) |
| 159 | 11788 | `0x172b60` |  | DIAG? | case 5 → 6. On host it apparently returns 0 repeatedly, causing / the cycle back to case 0. / |
| 160 | 11834 | `0x2a0250` |  | DIAG? | FUN_002a13e0. If this returns non-zero, FUN_002a13e0 exits / BEFORE doing any MC dispatch. This is likely where we stall. / |
| 161 | 11876 | `0x2a69f0` |  | DIAG? | / [FUN_002A69F0] Hook MC state 0→1 transition function. / |
| 162 | 12079 | `0x13bea0` |  | DISABLED | FUN_0013bea0 is a vtable method — called via JALR. Use dual hook. / [R188-HYGIENE] NOT installed (false &&): 6 reads/call on a / per-frame scene vtable render method. Investigation |
| 163 | 12285 | `0x167a40` |  | DISABLED | / [R188-HYGIENE] NOT installed (false &&): slot-6 boot ctor / question answered (stores verified rounds ago). |
| 164 | 12358 | `0x100c48` |  | DIAG | why [FMV2-SEQ] previously logged 0 hits). install_dual_hook keeps / recompiled_functions[], function_ranges AND the sorted snapshot / tables in sync so the wrapper fires on the ind |
| 165 | 12455 | `0x1ece48` |  | DIAG | fires, after!=0 -> child WAS built (look elsewhere) / Dispatched indirectly -> install_dual_hook. Pure logging. / |
| 166 | 12490 | `0x1f55c0` |  | DIAG? | confirm +0x144==0 here AND can correlate ordering with [ECE48] (did the / builder run before this teardown?). node_vtbl identifies the class. / |
| 167 | 12532 | `0x2afb18` |  | DIAG | chain reaches the TTIDENT open and whether the player was idle so it / could succeed. Dual-installed because FUN_002afb18 may be reached on / the indirect tier as well. Pure loggin |
| 168 | 12597 | `0x17b980` |  | DIAG | and calls FUN_00176260 on the prev==15 arm. Logging the word before/ / after + the flag byte pins the end-of-playlist transition. Pure / logging, capped, stderr (PERF runs have PS2 |
| 169 | 12809 | `0x17d298` |  | DIAG | [R196-FMV3] engine frame-pacer probe: FUN_0017d298 computes the / per-frame step count / dt for the scene walk. Log its mode / globals and return value around the movie window. |
| 170 | 13282 | `0x117070` |  | DIAG | Log state-byte and ctx-pointer transitions to reconstruct the / command walk and see where it diverges from ground truth. / |
| 171 | 13315 | `0x116e70` |  | DIAG? |  |
| 172 | 13497 | `0x1b8e28` | reg1b8e28 | DIAG? | reg1b8e28 |
| 173 | 13498 | `0x1ad968` | add1ad968 | DIAG? | add1ad968 |
| 174 | 13499 | `0x19fe38` | root19fe38 | DIAG? | root19fe38 |
| 175 | 13500 | `0x1b92a0` | tick1b92a0 | DIAG? | tick1b92a0 |
| 176 | 13501 | `0x1ae9c0` | upd1ae9c0 | DIAG? | upd1ae9c0 |
| 177 | 13502 | `0x1aef48` | wlk1aef48 | DIAG? | wlk1aef48 |
| 178 | 13503 | `0x1ad158` | pak1ad158 | DIAG? | pak1ad158 |
| 179 | 13504 | `0x19fea8` | gat19fea8 | DIAG? | gat19fea8 |
| 180 | 13505 | `0x1b92d8` | pip1b92d8 | DIAG? | pip1b92d8 |
| 181 | 13506 | `0x1c0778` | st1c0778 | DIAG? | st1c0778 |
| 182 | 13507 | `0x1af788` | st1af788 | DIAG? | st1af788 |
| 183 | 13508 | `0x1afca8` | st1afca8 | DIAG? | st1afca8 |
| 184 | 13509 | `0x1ae698` | st1ae698 | DIAG? | st1ae698 |
| 185 | 13512 | `0x1adb38` | alc1adb38 | DIAG | [S8-REG] allocator: log args + RETURN (v0) + the two boot / packets' {count,key} headers to expose the failing lookup. |
| 186 | 13551 | `0x1adc18` | clm1adc18 | DIAG? | g1=[0x309840] (view mgr), v160=[g1+0x160] (active view), / vid=[v160+0xa0] (current view id), pid=[a0+0xe314] (packet / system's bound view id). Claim requires pid==0 // pid==vid. |
| 187 | 13592 | `0x2231b0` |  | DIAG? | Proves the claim->center->W->draw chain end-to-end. |
| 188 | 13940 | `0x1ad718` | [S26-DECAL] MISSING fn 0x1ad718\n | DIAG? | [S26-DECAL] MISSING fn 0x1ad718\n |
| 189 | 13953 | `0x1ad158` | [S26-DECAL] MISSING fn 0x1ad158\n | DIAG? | [S26-DECAL] MISSING fn 0x1ad158\n |
| 190 | 14008 | `0x2b18f8` | [S26-DECAL] MISSING fn 0x2b18f8\n | DIAG? | [S26-DECAL] MISSING fn 0x2b18f8\n |
| 191 | 14053 | `0x18e4b8` | [S26-DECAL] MISSING fn 0x18e4b8\n | DIAG? | [S26-DECAL] MISSING fn 0x18e4b8\n |

Sites: 191; distinct EE addresses: 156; classification counts: {'DIAG': 52, 'DIAG?': 130, 'FIX': 5, 'DISABLED': 4}
---

## 5. TIMING

### 5.1 Every clock the game observes

| # | Clock | Implementation | Domain today |
|---|---|---|---|
| 1 | EE timers T0-T3 (COUNT/MODE/COMP/HOLD) | host_app/ee_timers.cpp [R195-TIM0]: tick-domain timers; T0 = 9600 counts/tick; ISR dispatch at PumpAtBoundary (frame boundary) and synchronously at PumpAtCountRead (first post-wrap COUNT read, measured 25-90 µs); [S40-SUBTICK] intra-tick COUNT interpolation (PS2_T0_SUBTICK default ON) clamped to never cross the tick quantum or wrap ahead of settle; game master-clock accumulator lives in guest RAM 0x3097D0 (rides RAM0 in saves — TIMR section deliberately absent by default) | timedom::DomainTick(g_vblank_count) |
| 2 | VBlank tick | ps2_vblank.cpp VBlankWorkerFunc: dedicated thread, ~16.67 ms; each tick increments g_vblank_count, signals semas 1+3, raises INTC VBON/TIM2/VBOF, notifies waiters. Freerun mode ([R132/R133], PS2_VBLANK_FREERUN, auto-ON under PS2_NOVSYNC): tick fires when previous consumed or gfx retired a VSync, 16.67 ms timeout floor; tick-source classification (consumed/vsync/timeout/cdvd) | raw wall (the generator) |
| 3 | Frame boundary / sceGsSyncV | gs_hle.cpp hle_sceGsSyncV + sif_hle hle_WaitForVblank: bumps g_sceGsSyncV_count and g_ee_vsync_pushes, timedom::NoteArrival + OnBoundary, eetimers::PumpAtBoundary, ee_pace boundary park, then waitForNextVBlankTick | the emu-domain settle point |
| 4 | Frame-budget predicate | guest FUN_00191bd0 "BudgetTimeLeft" re-implemented in budget_math.h (byte-for-byte from shard_009 emission: rate 576000=BUSCLK/256, fps 60, period 9600, 78-count phase guard, closed-form fold); hook at main.cpp:3254+; PS2_BUDGET_EMU hatch: mode-1 synthetic base 8800 counts charged per frame, [S112-BOOTPHASE] PhaseClassifier drops to base_low on ≥2 consecutive low-draw frames (loads/FMV) so the CD pump is granted volume; PS2_BUDGET_EMU=0 = R145 host sawtooth | emu counts |
| 5 | CPU COUNT (COP0 r9) | syscalls.cpp mtc0/mfc0 cases | stored value only (no free-run model) — known gap |
| 6 | RTC | sif_rpc_handlers.cpp:1128 sceCdReadClock → constant 2005-06-15 12:00:00 | constant |
| 7 | IOP heartbeat | iop_stubs StartHardTimer host ticker drives STREAM.IRX 60 Hz duties; PS2_HB_TICKCLOCK=1 re-clocks one beat per round(usec/16667) ticks onto the tick generator (turbo.h) | wall thread (legacy) or tick domain |
| 8 | CDVD latency | hle_heap DVD seek/transfer model; PS2_CDVD_INSTANT (+delivery-fired ticks) and PS2_CDVD_BUDGET_KB=256 per-frame delivery-volume meter with refill epoch = g_vblank_count minus cdvd-fired ticks | tick-epoch metered |
| 9 | Audio | SDL device callback is rate MASTER (never stretched); turbo ducks gain | device wall |

### 5.2 The emulated time domain as-is

`timedom` (host_app/time_domain.h, [S94-TIMEDOM], gate PS2_TIME_EMU default ON): one monotonic `g_emu_count` that every game-readable timer counts in. In the frame loop it advances exactly REGIME ticks per sceGsSyncV boundary (carry: one emu tick per raw tick while quantum has room; top-up settle at the boundary); REGIME (1..5 fields/frame) follows a hysteresis RegimeDetector (windows 15/120 boundaries, up>30%, down>90%, arrival-gap DOWN rule; PS2_TIME_EMU_REGIME pins it — =1 is the "perfect 60 Hz PS2" policy); a lead governor caps emu-ahead-of-wall at 1 tick while lag is absorbed and never repaid; out of the loop (no boundary >100 ms: loads/FMV) and under freerun/warp/turbo it rides raw ticks 1:1; boot/restore ReAnchor keeps the counter continuous. Unit-pinned in tests/test_time_domain.cpp.

### 5.3 Pacing / present cadence

- **Mode Q** (quantum.h, PS2_QUANTUM default ON): loop admission decoupled from the present grid — EE boundary parks only on the gfx depth bound + the next 60 Hz tick, so iterations naturally quantize 60→30 (retail slip shape); PS2_FPS_CAP becomes present-side decimation (absolute 1/cap grid, skip-forward-never-debt); FMV bypass rules reused from [R193c]. PS2_PACE_SMOOTH / PS2_CAP_STEPDOWN inert under Mode Q.
- **EE runahead bound** (ee_pace.h, PS2_EE_PACE default OFF, requires PS2_MTVU + defer): park at the boundary while my_ee > processed + Lag() (PS2_EE_RUNAHEAD default 2, clamp 1-4); injected CRTC presents carry arg1=0 and can never satisfy the bound; 100 ms valve; census histograms.
- **present_pace.h** ([R170]/[R193] Pacer) + graphics thread: EE game-frame VSyncs (arg1=1) vs injected CRTC fallback presents (arg1=0) for DISPFB-flip-only scenes; [R203-RAGUARD] PS2_MTVU_MAX_FRAMES depth clamp shared with the MTVU producer.
- **Turbo** (turbo.h [P2-TURBO]): ONE scalar K on the ONE tick generator; period 16667/K µs; counts-per-tick and boundaries-per-event never change; wall-latency models divide by K; audio ducked. PS2_TURBO manual 2-64 (0 = warp/freerun semantics); PS2_LOAD_TURBO auto-engage DEFAULT OFF since s71 (user directive: loads at true 1×) with streak/engage/release/input-inhibit hysteresis (GAP 150 ms, ENGAGE 1500 ms, RELEASE 250 ms, INPUT 2000 ms).

### 5.4 Proposed single time-domain design

Keep `timedom` as the core and make it the *only* clock authority — a `time/` module exporting exactly:

1. **TickSource** (the generator): one thread (or one host-timer callback on platforms without cheap threads) producing raw ticks; freerun/turbo/warp = policies ON the generator (period selection + release triggers), not parallel mechanisms. Owns tick classification (consumed/vsync/timeout/cdvd) as a census, not as behavior.
2. **EmuDomain** (today's timedom, unchanged semantics): quantum carry + boundary settle + regime detector + lead governor + idle-release; the ONLY readable counter for guest-visible time. `DomainTick()` is the sole API; delete direct `g_vblank_count` reads from consumers (ee_timers already goes through DomainTick; heartbeat and CDVD epoch should too — PS2_HB_TICKCLOCK=1 becomes the only shape).
3. **Derived clocks** as pure functions of EmuDomain: T0-T3 (counts/tick constants + subtick interpolation as a *presentation* layer, exactly the S40 clamped contract), the budget predicate anchor, heartbeat beats, CDVD refill epoch, COP0 Count (add the missing free-run model here: counts/tick constant).
4. **BoundaryProtocol**: the single frame-boundary sequence — NoteArrival → pace park (depth bound) → tick wait (quantum) → OnBoundary settle → timer pump → ISR dispatch — expressed once in gs_hle rather than distributed across gs_hle/ee_pace/quantum/present_pace call sites.
5. **PresentPolicy** (graphics side): decimation + FMV bypass + injected CRTC fallback, consuming EmuDomain only for stamps; never able to influence loop admission (Mode Q made this true; make it structural).
6. **Config collapse**: PS2_TIME_EMU / PS2_T0_SUBTICK / PS2_HB_TICKCLOCK / PS2_BUDGET_EMU / PS2_QUANTUM merge into one `time.mode = emu` (their =0 legacy arms are purge targets after the equivalence gate passes); PS2_TIME_EMU_REGIME, PS2_TURBO, PS2_LOAD_TURBO*, PS2_FPS_CAP, PS2_EE_RUNAHEAD survive as the five real knobs.

Determinism note for the EQUIVALENCE plan: the emu domain is already tick-deterministic; the two wall-sensitive leaks are subtick low bits (documented, PS2_T0_SUBTICK) and regime flips under host load — pin PS2_TIME_EMU_REGIME=1 and PS2_T0_SUBTICK=0 for differential runs.

---

## 6. Savestate format and cross-engine contract

Format (docs/SAVESTATE_SCHEMA.md; host_app/savestate_format.{h,cpp}; writer/reader unit-tested): FileHeader magic "PS2SAVE1", version 2 (readers accept 1-2; version stamps 3 when TIMR or SND sections are enabled — savestate.cpp:1380-1383), section_count, vsync_frame, wall_ms; then SectionHeader{fourcc tag, version, flags(bit0 REQUIRED), crc32, payload_len} × N; trailer CRC over everything. Unknown+REQUIRED → reject; unknown optional → skip; missing newer sections degrade with documented behavior.

28 sections: RAM0 (32 MB), SPAD (16 KB), META (2 MB arena), VRAM (4 MB), VU0C/VU0D/VU1C/VU1D, THRD (per guest thread: kernel fields + raw CpuContext + shadow chain innermost-first, ctx_size guard), SEMA, EVFL, INTC, DMAC, GSPR, GSST v3 (incl. serialized vertex_queue/draw_buffer latches — what makes post-island saves possible), VIFS, GIFS, PADS, FILE (CDVD handle table; ISO is process-global), CNTR (vsync/vblank/pushes/processed/SyncV/dispfb-flip counters), MISC (id allocators, boundary tid), HEAP (HLE heap + meta-arena with **allocation identity** — post-load allocs must vend the same addresses), STRM (cdvd_stream + 48 STREAM.IRX slots), SIFS, VU1R (MTVU worker private CpuContext; load restarts worker), SDRX, MCHL (card-seen edges), IOPM (2 MB IOP RAM + iop_ctx + IOP thread/sema PODs — the deep-load popup fix; host FILE*/thread handles deliberately not serialized, both processes boot identical IOP workers).

Admissibility: save only at a QUIESCENT frame boundary (thread inside hle_sceGsSyncV/hle_WaitForVblank; MTVU + render queue drained; GPU RTs synced down to gs_vram; DMA idle; FMV/IPU idle); named saves retried ≤5 boundaries (user saves 240 with loud give-up).

Cross-engine contract (what generated code must provide, savestate.cpp:78-95):
- R5 rule: host C++ stack carries NO PS2 state; a thread's resumable state = ThreadInfo + CpuContext + RAM.
- Shadow call chain: ee_call_stack_push/pop wrappers in every generated function; manual bodies push/pop via the exported hooks; park-site harvest (NotePark at waitSema/sleepThread/waitEventFlag on the parking thread, where the thread_local chain is readable) + RegisterThreadCtx for live contexts.
- Resume-address containment: `recomp_fn_id_for` / `recomp_fn_id_for_containing` dense ids validate every restored chain frame against the generated function table — this is the actual cross-ENGINE gate: a v2-vs-legacy (or IR) build can only load a state whose chain frames resolve to functions with compatible extents and mid-entry labels. Manual bodies are flagged (no mid-entry) via the sorted manual-addr registry.
- Restore rebases time (timedom ReAnchor + timer baselines), restarts the MTVU worker with the VU1R context, re-parks IOP workers, silences voices (sound OnSavestateLoad), reclassifies CRTC merge ([GB3]).

For the refactor: the section registry (tag → serialize/restore/validate triple, today ~30 free functions wired in savestate.cpp) should become a declared table; the schema doc is already the source of truth and must gate any struct move (GSST/THRD/VU1R serialize raw structs with sizeof guards — layout changes are format changes).

---

## 7. Portability blockers — Vita-class target

Each with evidence and mitigation. "Vita-class" = ~444 MHz quad A9, 512 MB RAM, GLES2-era GPU (no GL4, no compute), pthread-like threads OK.

| # | Blocker | Evidence (file:line) | Mitigation |
|---|---|---|---|
| B1 | **1 GB stack per guest EE thread** (and unbounded host-stack recursion of recompiled code: 35k nested dispatch frames observed) | ps2_threading.cpp:526, :676 (+ history comment :510-515); vu1_async.cpp:80-90 (32 MB MTVU stack; AOT bodies ~2.4 MB frames) | IR codegen must bound host stack: flatten guest calls to a tail-dispatch loop (return-to-trampoline) or cap inline depth with explicit continuation at park sites; measure worst chain from THRD chains in existing saves (128-frame shadow cap already exists) |
| B2 | **Host memory footprint**: 32 MB RAM + 2 MB arena + 4 MB VRAM + 2 MB IOP + VU memories + GL textures + 91 MB code segment (host_app binary links all shards) + render queues | memory.h:13-32; build-v2 shard sizes; SAVESTATE sections | On 512 MB: code size is the risk — IR should emit denser code (shared epilogues, table-driven ops), make AOT/vu-aot optional link groups (already CMake-optional), drop diagnostic strings (91 MB includes probe text) |
| B3 | **OpenGL 4.1 core + compute shaders** | opengl_backend.cpp:2850-2852 (context 4.1 core); :4066-4129 glCreateShader(GL_COMPUTE_SHADER), glDispatchCompute, glMemoryBarrier, glBindBufferBase (SSBO), glFenceSync/glClientWaitSync, glGetTexImage, glBlitFramebuffer, glMapBufferRange | The backend abstraction already exists (graphics/graphics_backend.h + a working Vulkan backend with GLSL450→SPIR-V, vulkan_backend.cpp:144); require: no-compute fallback for cs_rasterize_* (CPU raster or fragment path), GLES-safe readback (PBO/glReadPixels only), and keep backend-neutral gs state in gs_state (out of A4 scope details in A3) |
| B4 | **SDL2 everywhere** (window, GL context, audio, input, timers) | main.cpp (30 uses), sound/mixer.cpp, input/* | SDL2 has a Vita port; alternatively isolate behind existing input/ + sound/ + platform seams — already mostly layered; main.cpp's direct SDL usage must move into platform/ |
| B5 | **ucontext fibers (deprecated) + Win32 fibers** | platform/macos/fiber_macos.cpp:26-135, platform/win32/* | Dead code with the std::thread scheduler — CMakeLists.txt references neither ps2_scheduler_old_fiber.cpp, fiber_macos.cpp nor main_old.cpp (verified grep count 0); delete in the purge |
| B6 | **Thread count**: 6 fixed host threads + one per guest EE thread + per-IOP-thread + tickers | §2.3 census | Fine on 4 cores IF stacks are bounded (B1); consider collapsing IOP tickers onto TickSource (§5.4) and making MTVU optional (already gated) |
| B7 | **x86-64/host assumptions are mild but real**: `_mm_pause`-class spin hints in render.h (5 uses); NEON path already exists for VU fast ops (vu_fast_exec kVuLaneMask NEON FTOI0; sse2neon absent — direct NEON in vif/gif/texture_decoder/vu_fast_exec per __builtin/arm_neon grep) | render.h; vu_fast_exec.h; vif.cpp; gif.cpp; texture_decoder.cpp | A9 has NEON — paths exist; replace spin hints with std::this_thread::yield wrapper in platform/ |
| B8 | **fenv dependence** (FE_TOWARDZERO global on EE threads; per-op FE_TONEAREST swaps in ps2_div_s etc.; VU ChopGuard) | cpu_state.h:26-56; main.cpp:605; ps2_threading.cpp:366; vu.cpp:47-62 | ARM FPCR rounding-mode writes are cheap but per-op fesetround is a libc call — IR should lower div/sqrt to explicit round-mode-tagged ops so a backend can batch mode switches; NEON default flush-to-zero actually HELPS PS2 denormal semantics |
| B9 | **File IO model**: process-global ISO FILE* + pread shim; logs/ tree with unbounded diagnostic artifacts (15 MB runtime_log.txt observed, PPM dumps); getenv-driven config | hle_heap.cpp head (ps2_pread [U1-WIN]); logs/ inventory; 556 env gates | pread exists on Vita newlib-ish SDKs (or emulate under one IO mutex); replace env gates with a config file + typed table (§0.4); cap/disable g_logFile by default |
| B10 | **64-bit host assumed** (uintptr in handle maps, size_t sizes; no explicit 32-bit audit) and **std::function-based dispatch tables** (heap-allocating, cache-hostile on A9) | recompiled.cpp:148 (std::map<uint32_t, std::function>); function_ranges | IR emission: replace std::function/map with flat arrays of raw fn pointers + binary-searched ranges (also wins on desktop); do a one-pass 32-bit-cleanliness audit if a true 32-bit target is ever chosen (Vita is 32-bit!) — the u128 GPR unions are fine, but uintptr-keyed identity maps must key on guest addresses |
| B11 | **Generated-TU compile cost on any toolchain** | CMakeLists.txt:181-217 (2068 s / 2.6 GB single TU history) | Keep sharding a first-class IR-backend output parameter; target-independent IR serialization would let codegen run per-shard incrementally |
| B12 | **GL-thread + queue architecture assumes a compositor-grade GPU driver thread** | graphics_thread.cpp (242 KB single consumer) | Backend seam exists; Vita would use its own submit thread — no change to CPU-side contract if RenderQueue stays the boundary |

---

## Appendix A — artifact/size inventory (measured)

- host_app: 107 .cpp + 111 .h at root (+ subdirs common/ graphics/ gui/ hle/ input/ platform/ sce_hle/ sound/ tests/); main.cpp 845 KB; vu.cpp 658 KB; sif_rpc_handlers.cpp 296 KB; memory.cpp 278 KB; gs_state.cpp 264 KB; graphics_thread.cpp 243 KB; opengl_backend.cpp 1.39 MB (A3 scope).
- Generated: recompiled.h 521 KB (9,327 fns); common TU 2.6 MB; 32 shards 91 MB; iop_recompiled 2.5 MB; VU AOT 233 MB inc + 351 MB lib (disabled via vu_aot.ini).
- Env gates: 556 distinct PS2_* (list at /tmp/s113/a4/env_vars_list.txt); top files by getenv count: opengl_backend.cpp 90, vu.cpp 66, render.h 50, main.cpp 45.
- Docs: docs/loop_state/ 635 session-impl docs; schemas: SAVESTATE_SCHEMA.md, RING_LOG_SCHEMA.md, hle_harness.md, vu1_trace_harness.md, recomp_semantics_tests.md; recompiler_tool/CONVENTIONS.md.
- Corpora/verification: vutrace_cases 62 / cases2 126 / collect 3,293; tests/ 113 entries incl. vu1_replay, vif_replay, gs_replay, recomp_semantics/, recompiler_v2/, recompiler_verify/, test_time_domain, test_savestate_format, test_recomp_overrides_config; tools/ 71 entries (recomp_gates, recomp_lint.py, sce_lib_finder, vu1_trace_diff.py, coldboot_gate.py …).
- Hook surface: 191 dual-hook sites (156 addrs) + 235 manual installs + sce_hle registry bindings + libc installs.

## Appendix B — method / scripts

/tmp/s113/a4/: 01_inventory.sh, 02_portability.sh, 03_structure.sh, 04_hooks.py, 05_timing.sh, 06_deep.sh, 07_more.sh, 08_gaps.sh, 09_hooktable.py; outputs *.txt/*.md alongside. All reads were performed against the working tree on 2026-09-22; no files under $PS2R_OLD_ROOT were modified.
