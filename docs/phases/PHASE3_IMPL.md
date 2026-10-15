# PHASE3_IMPL — execution record for Phase 3 (`engine/ee/` + the recompiled output)

Plan: `docs/phases/PHASE3_PLAN.md` (revised by p3a per the p3pr review, §9 there). Rules: `docs/phases/STAGE_RULES.md`.
OLD = `$PS2R_OLD_ROOT` (frozen, read-only). All commands bounded via `verify/run_bounded.sh`.

---

## 3A — plan corrections + EE cpu state + memory + tests (stage p3a, 2026-09-26)

### Part 1 — PHASE3_PLAN.md corrections (p3pr review, 8 items)

| # | Item | Where |
|---|---|---|
| 1 | §3.4 seam contract rewritten COMPLETE: **27 rows / 101 roster entries** (§3.4.4; the first p3a pass said 118 by hand-sum — corrected in loop iteration 1, see below), each with a Phase-3 provider (real / park / title pass-through / inert / defined-by-generated-TU); the wrong rows fixed (no `hle_heap::` namespace — 12 `HLE_002c*` free functions `hle_heap.h:263-276`; `g_func_trace_enabled` is `static` in the common TU `:79`; `g_ee_call_stack` thread_local DEFINED at `recompiled.cpp:94` → `ShadowCallStack` wraps, no storage; `ctx.vuRegs`; `crashring_fatal_no_jump` `[[noreturn]]` `:4410` → park body never returns; park owners `0x2d1a50` seam `:26841` AND `0x2d2180` registry row `sce_hle/sce_hle_bindings.inc:228`) | §3.4 |
| 2 | Pass-through bodies title-side: `games/<t>/hle/glue/{sce_graph,sce_dma,crt0,libc_heap}_glue.cpp` + `hle/workarounds/setup_display_registers.cpp`; the address-free DispatchTable primitive REJECTED with the reason | §3.4.1 |
| 3 | Meta arena → `games/<t>/hle/workarounds/meta_arena` (CONSTRUCTION §4.3); engine pair dropped; `AddressMap::RegisterExtraRegion` is the generic hook; `restore.mhash` coverage stated | §3.4.2, §2.2, §2.8 |
| 4 | RAM image contract decided: retail PT_LOAD load + data map as title input + post-load patches; probe added; `loader/*` cite dropped | §3.4.3, §2.2, §7.1 |
| 5 | Syscall count 271 → **119 live `case` labels + `default:`** (`syscalls.cpp:2315-2504`; the first pass's `grep -c "case "` = 120 counted the commented-out `// case 0x74: sceSifSetDma` at `:2496`; `grep -cE '^\s*case '` = 119) | §2.3, §4.1, §8.7, 3C2, G5, §9 row 5 |
| 6 | BUG-035 both halves | §2.4, 3C1 |
| 7 | 3C → 3C1/3C2, 3D → 3D1/3D2 with green criteria | §7 |
| 8 | 9 citation fixes enumerated (a)–(i) | §9 |

Measured this stage (inputs to the corrections): `HLE_002c*` references in the common TU = 12 (census grep);
`g_vu0.Execute` 76 / `g_vu0.ResumeIfWaiting` 16 / `g_vu1.Execute` 16 (`g_vu0` in 14 shards: 007, 008, 009, 011, 012,
014, 015, 016, 017, 023, 026, 027, 029, 030; `g_vu1` in 5: 009, 011, 012, 029, 030); THRD `ctx_size` = **1600**
in all **11** anchors (`OLD/save_states/{8× ps2state_slot0_*, artdet_f37152, slowspot_f37092_s1789428268,
slowspot_f40298_s1789693059}.ps2st` = the `verify/baseline/` legs minus `coldboot`; 2 THRD records each;
`/tmp/p3a/thrd_ctx_size_all.py` over the packed `ThreadRecHdr` of `savestate.cpp:547-559`); `ram_data_map_v50.txt` 19,281 entries md5 `b064c68028af08ad282b30e22b1fa5a3`;
`SLUS_123.45` md5 `36583bd799a803cae7b8f2a7b2ca6592`, 2,151,940 bytes.

### Part 2 — files landed

| File | Content | OLD source read |
|---|---|---|
| `engine/core/types.h` | `u8..u64`, `s8..s64`, `u128`/`s128` (two-halves aggregates) | `common/Pcsx2Types.h` (names only) |
| `engine/ee/cpu/cpu_state.h` | `GPR_reg`, `VR_reg`, `GPRregs`, `CP0regs`, `FPRreg`, `FPUregs`, `VUregs` (+ `ReadControlReg`/`WriteControlReg`/`Reset`), `CPURegs`, `EmotionEngineState` = `CpuContext`; static_asserts: `CPURegs` 720, `FPUregs` 264, `VUregs` 608, `fpuRegs` @720, `vuRegs` @992, **sizeof 1600** | `cpu_state.h:56-294` |
| `engine/ee/cpu/ps2_float.{h,inl,cpp}` | `Ps2Float` + `ps2_float_clamp/ps2_div_s/ps2_sqrt_s/ps2_rsqrt_s` (inline, `.inl`) + `SetGuestThreadRounding` | `cpu_state.h:9-54` |
| `engine/ee/cpu/cpu_context_init.{h,cpp}` | `CpuContextInit::Reset(ctx, entry, sp)` | `main.cpp:529-600` (shape), `cpu_state.h:268-280` |
| `engine/ee/mem/quad_word.h` | `QuadWord` | `memory.h:36-50` |
| `engine/ee/mem/main_memory.{h,cpp}` | `MainMemory` 32 MB, `Phys()`, bounds-checked `Pointer` | `memory.h:13`, `memory.cpp:470-490` |
| `engine/ee/mem/scratchpad.{h,cpp}` | `Scratchpad` 16 KB | `memory.cpp:383-395` |
| `engine/ee/mem/address_map.{h,cpp}` | `AddressMap`: OLD's translate order + `RegisterHwWindow` (virtual, inside `0x10000000–0x1FFFFFFF`) + `RegisterExtraRegion` (physical, above RAM); IO image (64 KB) and BIOS (4 MB) built in | `memory.cpp:358-505` |
| `engine/ee/mem/fast_mem.{h,inl,cpp}` | `FastMem`: `Read<T>/Write<T>/ReadQuad/WriteQuad` fast path + `*Slow` (router for IO registers, map otherwise) | `memory.h:60-260`, `memory.cpp:1145+`, `:4850`, `:4893` |
| `engine/ee/mem/mmio_handler.h` | `MmioHandler` interface | — |
| `engine/ee/mem/mmio_router.{h,cpp}` | `MmioRouter`: `IsIoRegister`, last-wins ranges, unhandled 256-byte page census | `memory.cpp:531-536`, `:539-981` |
| `engine/ee/mem/mmio_sink.{h,cpp}` | `MmioSink`: 12-row idle table (GS_CSR word `0x8`; GS priv/timers/IPU/GIF/VIF/FIFO/DMAC/INTC/SIF/D_ENABLER/D_ENABLEW = 0), byte-lane sub-word reads, writes dropped, counters | `memory.cpp:539-981`, `dmac.cpp:1045-1131`, `dmac.h:75`, `gif.cpp:221-246` |
| `engine/ee/mem/elf_loader.{h,cpp}` | `ElfLoader::Parse/Load/ReadFile` (ELF32 LE EM_MIPS, PT_LOAD copy + zero-fill, all-or-nothing) | ELF spec; `memory.cpp:166-221` (what it replaces) |
| `engine/ee/mem/data_map_applier.{h,cpp}` | `DataMapApplier::ParseLine/ApplyText/ApplyFile` (OLD's grammar) | `memory.cpp:240-312` |
| `engine/ee/CMakeLists.txt` | `ps2r_ee` STATIC (`-Wall -Wextra -Werror`, links `ps2r_core`); root `add_subdirectory(engine/ee)` | — |
| `games/<title>/data/ram_data_map_v50.txt` | byte copy (md5 `b064c68028af08ad282b30e22b1fa5a3`) | `game_files/ghidra_functions/ram_data_map_v50.txt` |
| `games/<title>/data/post_load_patches.txt` | the 3 post-map writes | `memory.cpp:310-312` |
| `games/<title>/data/README.md` | provenance + application order | — |
| `tests/ee/CMakeLists.txt` + 5 test binaries / 6 ctest entries | `EeCpuStateLayout`, `EeCpuStateLayoutAnchor` (same binary, anchor mode; SKIP 77 when the frozen anchor is absent), `Ps2FloatSemantics`, `FastMemDiff` (all 27 original pools), `AddressMapAndMmioSinkIdleValues`, `ElfLoaderTitle` (SKIP 77 when the frozen ELF is absent) | `tests/fastmem_diff/test_fastmem_diff.cpp:96-122` (grid, 27 pools), `tests/recomp_semantics/cases.h` (FPU rows, by rule), `savestate.cpp:547-559` (`ThreadRecHdr`) |
| `tools/recomp_symbol_census.py` | the §3.4.4 roster (101 entries + 11 include paths) as an executable check: `--dir <recomp dir> [--check]`; over `OLD/build-v2-nog`: 101/101 present, 11/11 includes, 0 missing | `build-v2-nog/recompiled{.h,.cpp,_shard_*.cpp}` (read-only census) |
| `engine/ee/{cpu,mem}/README.md` | file tables + flags consumed | — |

Flags consumed (§5 outcomes applied at write time): `PS2_FASTMEM` NORM (ON; the `fastmem_on()` getenv arm and
the slow-only arm are not written), `PS2_MEMWATCH` DIAG (probe gauntlet not transplanted; no diag hook yet — the
`core/diag` observer is Phase 9), `PS2_S15_WATCH` / `PS2_S8_MTXWATCH` / `PS2_DETWATCH`-site code DELETE/DIAG (not
written), `GAME_FILES_DIR` SETTING (reader lands with the title composition, 3F). OLD's every-256th-write slow-path
tick (`memory.h:114-121`, the KEY-REDIRECT carrier) is NOT in `FastMem`: the 3E workaround registers its own
observer.

Decisions taken in 3A:
- `MmioSink` GS_CSR row is 4 bytes wide (OLD's `0x8` arm matches the exact word; a 64-bit read's high word is 0).
- `AddressMap` serves GS privileged registers by MMIO only (`Translate` → nullptr there, as OLD's `get_pointer`).
- `FastMem` unmapped reads return 0 and unmapped writes drop (OLD `read_slow` fall-through); the fast/slow test
  skips write-back checks on unmapped/MMIO addresses because the sink drops writes by contract.
- File names follow the lint: `fast_mem.{h,inl,cpp}` (class `FastMem`), `cpu_context_init.{h,cpp}` (class
  `CpuContextInit`); the plan's `fastmem`/`cpu_context` spellings are superseded.

### RAM-image probe (`./build/tests/ee/test_elf_loader <OLD ELF> data/ram_data_map_v50.txt data/post_load_patches.txt`)

```
ELF: entry=0x00100008 segments=1 range=[0x00100000,0x003dae80)
  PT_LOAD vaddr=0x00100000 off=0x00001000 filesz=0x20a0b8 memsz=0x2dae80 flags=7
RAM-IMAGE PROBE: entries=19281 covered_by_elf=19278 outside_elf_filesz=3 mismatched_entries=0 mismatched_bytes=0
  PATCH 0x002e6f40: elf=0x10008000 patch=0x10008000 (same)
  PATCH 0x002e6f44: elf=0x10009000 patch=0x10009000 (same)
  PATCH 0x002e804c: elf=0x00000001 patch=0x00000000 (DIFFERS)
data map applied: entries=19281 bytes=77124 range=[0x002e6b80,0x0030a0bb)
post-load patches applied: 3
PASS ElfLoaderTitle
```

Recorded as PHASE3_PLAN §7.1 **P3-RAMIMAGE-1** (map ⊆ ELF, 0 mismatches; 3 straddling zero entries at
`0x30A0B5..B7` land in `.bss`), **P3-RAMIMAGE-2** (OLD's patch `0x2e804c=0` differs from the retail word
`0x00000001` — kept as a title patch, adjudicated at 3F), **P3-ELFRANGE** (the plan's `0x0020A0B8` was a size,
not an end; real PT_LOAD ends: file-backed `0x0030A0B8`, memsz `0x003DAE80`).

### Loop iteration 1 (p3ar review of the 3A submission) — files touched

| File | Change |
|---|---|
| `docs/phases/PHASE3_PLAN.md` | §2.1 (11 anchors named), §2.3 (119 + default), §3.4 rows 7/14/15/27 (counts, `sce_hle_bindings.inc:228`, shard lists), row-count paragraph → 101 + new §3.4.4 roster, §4.1 (`EeCpuStateLayoutAnchor` row, 27 pools, 119), §6 G5, §7 3B/3C2 rows, §8.7, §9 rows 1/5, new §9.1 |
| `tests/ee/test_fastmem.cpp` | the 3 dropped pools restored (27, `static_assert`), comment states the port is complete and why IOP RAM reads 0, 3 direct spot checks, PASS line prints the pool count |
| `tests/ee/test_cpu_state_layout.cpp` | anchor mode (`argv[1]` = `.ps2st`): `ps2r::savestate::ReadStateFile` → THRD walk → `ctx_size == sizeof(CpuContext)` per record; exit 77 when the file is absent; static mode unchanged |
| `tests/ee/CMakeLists.txt` | `EeCpuStateLayoutAnchor` entry (`SKIP_RETURN_CODE 77`) over `${PS2R_OG_ROOT}/save_states/ps2state_slot0_f36935.ps2st` |
| `tools/recomp_symbol_census.py` (new) + `tools/README.md` | the roster as an executable check |
| `docs/phases/PHASE3_IMPL.md` | this record; Part 1 rows 1/5; measured block (11 anchors, shard lists); gate status; HANDOFF |

### Gate status after 3A (loop iteration 1 — p3ar's five items applied)

```
$ ./verify/run_bounded.sh 900 /tmp/p3a/build5.log -- cmake --build --preset macos-dev -j4   # exit 0, 0 warnings
$ ./verify/run_bounded.sh 600 /tmp/p3a/ctest5.log -- ctest --preset macos-dev
100% tests passed, 0 tests failed out of 51
```
Includes `LintOneClass`, `LintEnginePurity` (0 findings: `engine/` holds no game addresses — the data map, the
patch list and the ELF path are title inputs under `games/<t>/`), the Phase 1/2 suites, and the six EE entries:
- `EeCpuStateLayout`: `sizeof(CpuContext)=1600` (static pins; the literal is the value measured over all 11 anchors);
- `EeCpuStateLayoutAnchor` (new in iteration 1): `PASS … 2 THRD records in …/ps2state_slot0_f36935.ps2st carry
  ctx_size == sizeof(CpuContext) == 1600` (has_ctx=1, chain_depth 4 and 1); with the anchor path absent the binary
  prints `SKIP EeCpuStateLayoutAnchor: anchor not present` and exits 77 (verified by hand);
- `FastMemDiff`: `PASS FastMemDiff: 27 pools, 54000 grid reads agree (fast path served 2997/10800 sample addresses),
  236 unhandled MMIO pages` — the three pools the first port dropped are back (`iop_ram_end` reads 0 both ways:
  IOP RAM has no backing in Phase 3, `Translate` → nullptr; `kseg1_bios_mirror` sees the BIOS window through all
  three mirrors; `high_seg_ram_wrap` lands in low RAM via `& 0x1FFFFFFF` and is fast-path served);
- `AddressMapAndMmioSinkIdleValues`: 12 idle rows; `Ps2FloatSemantics`; `ElfLoaderTitle` (probe unchanged).
`tools/recomp_symbol_census.py --dir OLD/build-v2-nog --check` → `ROSTER TOTAL = 101 entries; MISSING = 0`, exit 0.
`macos-diag` preset not rebuilt this stage (no diag code added).

### HANDOFF (3A → 3B)

**Done:** the eight plan corrections (§3.4 27 rows / **101 roster entries** enumerated in §3.4.4, §3.4.1–§3.4.3,
§7.1, §9) plus the five p3ar iteration-1 items (§9.1: syscall **119**, roster 101 + census script, 27-pool
fastmem grid, two citation fixes, anchor-reading layout test + the 11-anchor reconciliation); `core/types.h`;
`ee/cpu` (3 pairs) and `ee/mem` (9 pairs + 2 PODs); title data inputs; `tools/recomp_symbol_census.py`; 5 test
binaries / 6 ctest entries; tree green (51/51).

**Not done (3A scope, carried):** `EeCpuStateLayout`'s per-member OFFSETS are still derived from `cpu_state.h`
declaration order, not read from a binary — only `sizeof` is cross-checked against the anchors (the THRD payload
carries the raw struct without a member table; rule 7 forbids building OLD). `MmioSink` has no `run.meta` writer
yet (`MmioRouter::UnhandledPageCount()` is the value; `VerifyHost` consumes it in 3F). No `Config.paths.game_files`
reader yet (3F composition). The `.text` bytes now present in RAM are never executed (the recompiled output is the
code) — the reference interpreter (4.2 ports) may read them. The census script checks roster PRESENCE only; the
UNLISTED-symbol scan over the NEW output (any `hle_*`/`g_*`/`HLE_*`/`memory::`/`ps2_threading::` identifier the
generated TUs reference that is not in `ROSTER`) is 3B's addition.

**Next command (stage 3B):** per PHASE3_PLAN §3.1 — `tools/regen_from_frozen.sh` (two bounded runs, diff empty),
`games/<t>/recomp/` + `MANIFEST.sha256` + `INVOCATION.md`, `python3 tools/recomp_symbol_census.py --dir
games/<t>/recomp --check` (expect 101/101 + 11/11; any extra symbol = plan defect, record), `engine/ee/recomp_abi/
{host_app/*.h, anchor/, recomp_abi.cpp}` forwarding to `FastMem`/`Ps2Float`/`QuadWord`/`CpuContext`,
`ee/cpu/{dispatch_table, shadow_call_stack, seams.cpp}`, CMake `ps2r_title_recomp` (`-O2 -fno-strict-aliasing
-fwrapv` shards, `-O0` common TU); then `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`.

---

## 3B — recompiler output + seam contract + shard build (stage p3b, 2026-09-26)

### Part 1 — the four p3ar iteration-1 items (closed first)

| # | Item | Applied |
|---|---|---|
| 1 | `lint_engine_purity.py` range | `EE_TEXT_HI` `0x0020FFFF` → `0x003DAE80` (PT_LOAD memsz end, EXCLUSIVE: `lo <= v < hi`), docstring + comment cite P3-ELFRANGE; selftest gained `.data` (`0x002e804c` → finding), `.bss` last word (`0x003dae7c` → finding) and memsz-end (`0x003dae80` → clean) cases, `LintEnginePurity selftest: PASS`; tree scan 0 findings with the widened range (no size-constant collision present); `purity_allowlist.txt` states that only 1/2/3 MiB SIZE constants may ever be listed; §7.1 P3-ELFRANGE disposition extended |
| 2 | §2.4 BUG-035 half (b) | now reads "a mutator whose caller neither holds the run token nor is applying ENQUEUES (a holder calling directly, or the holder's drain applying an entry, mutates in place; any other caller enqueues)" |
| 3 | §3.4 row 26 sites | "common TU and every shard: `crashring_enabled` / `crashring_record_indirect` 2,619× each from the shards' indirect-call guards" (re-measured over `build-v2-nog`: common 2 / header 1 / shards 2,619 each; `fatal_no_jump` common 2) |
| 4 | §3.4.3 | the nonexistent `--ram-image-probe` mode replaced by the real invocation (`test_elf_loader <ELF> <map> <patches>`, probe runs whenever the map is given); map range written as `[0x2E6B80, 0x30A0BB)` |

### Part 2 — (A) regeneration from the frozen tool

`tools/regen_from_frozen.sh` (new; `--check`, `--out DIR`): verifies the four frozen md5s (tool `6235a2f9…`, ELF
`36583bd7…`, `tiny_stubs.txt` `045de1a5…` 216 lines, `tiny_returners.txt` `b6537d0d…` 535 lines), pins cwd to
`build/regen/{a,b}/cwd` (the tool's `logs/recompiler_tool_v2/runtime_log.txt` lands there, never in OLD), runs the
§3.1 command twice through `run_bounded.sh 1800`, requires `diff -r --exclude=cwd --exclude=recomp_gen.log a b`
EMPTY, extracts the report and fails on any missing expected line, installs `a/` into `games/<t>/recomp/` and writes
`MANIFEST.sha256`, `MANIFEST.md` (md5 per file + provenance + date), `INVOCATION.md`.

```
regen: run a 10 s, run b 10 s
regen: determinism: diff -r a b is EMPTY (34 emitted files)
regen: report lines OK; header declares 8818 FUN_ + 1 entry = 8819 functions
regen: installed into …/games/<title>/recomp (34 files); manifests written
regen --check: … matches MANIFEST.sha256
```
Report lines (from the tool, quoted in `INVOCATION.md`):
```
[v2] discovery over …/SLUS_123.45 (.text 1939976 bytes, 5 data sections, entry 0x00100008)
[v2] discovered 8819 functions, 177 jump-table sites, 8848 unreached gap words
[v2] no --ghidra-input: emitting the pure discovery set (no aliases/compat/pins)
[v2] emission model: 8819 functions (0 aliases, 0 pinned), 177 jr table sites
Found stub file: tiny_stubs.txt        / Auto-registered 0 tiny 'jr ra; nop' stubs
Found stub file: tiny_returners.txt    / Auto-registered 36 tiny 'return N' stubs
[W16R1][FIX-E1] …/recompiled.cpp: injected 180 generator hook addrs (8659 default-registered)
[v2] emitted …/recompiled.h + …/recompiled.cpp (32 shard(s))
```
Shape: 34 files, **91 MB** total (common TU 2,515,275 B, header 493,490 B; the plan's "~151 MB" estimate was high);
`getenv` sites: common TU 6, shards 0 (as measured in the plan). The full tool log (121,534 lines, mostly
"Recompiling block") stays in `build/regen/`.

### Part 3 — (B) census + the UNLISTED-symbol scan

`tools/recomp_symbol_census.py` gained the scan: every identifier of the seam families (`hle_*`, `HLE_*`, `HLE_FUN_*`,
`GetPtcRecord`, `g_*`, `memory::*`, `ps2_threading::*`, `ps2_*`, `recomp_*`, `crashring_*`, `ee_*call_stack*`,
`call_census_*`, `runtime_*`, `handle_*`, `StreamPoll*`, the dispatch names) referenced by the generated TUs, minus the
roster, minus the names the TUs DEFINE (a definition = body `{` before `;`, or `=`, or a plain non-extern non-function
declarator; `extern "C" T x;` and `T f(...);` are declarations only). Negative check: a synthetic shard with
`extern void hle_NotInRoster(...)` + `g_bogus_global` → both reported UNLISTED, exit 1.

Result over `games/<t>/recomp` (ctest `RecompSymbolCensus`, 94 s):
```
INCLUDES row 1: 11/11 present in the common TU
ROSTER TOTAL = 108 entries; MISSING = 0 []
UNLISTED SCAN: 107 family identifiers referenced; 2 outside ROSTER: 2 defined by the generated TUs (not seams), 0 UNLISTED EXTERNS
  tu-internal  g_func_call_count            (recompiled.cpp:77 static)
  tu-internal  recomp_apply_gen_hook_list   (recompiled.cpp:4274 static)
```
The roster grew 101 → **108** from three findings the stage recorded in PHASE3_PLAN §7.1: **P3-SEAM-1** `recomp_fcf_miss`
(found by the scan: declared `:4195`, called `:4270`, defined nowhere — the real `recomp_seams.cpp` seam; the plan's
"`recomp_dispatch_fallback`/`recomp_mark_hooked` are the seams.cpp bodies" was wrong, both are defined by the common
TU), **P3-SEAM-2** five `HLE_FUN_002a*` loader hooks and **P3-SEAM-3** `GetPtcRecord` (found by the first compile of
the common TU: the census regex missed the `HLE_FUN_` spelling and `GetPtcRecord` was in no family — both families
added). Plus **P3-VROPS**: the VU0-macro shards use `VR_reg op VR_reg` (OLD member operators) → free operators in the ABI
forwarder.

### Part 4 — (C) files landed

| File | Content |
|---|---|
| `engine/ee/recomp_abi/anchor/README.md` | the include anchor (`-I …/anchor` → `../host_app/x.h`) |
| `engine/ee/recomp_abi/host_app/{cpu_state,memory,syscalls,sif_hle,gs_hle,hle_heap,ps2_threading,cdvd_stream,timer_hle,vu,sif_rpc_handlers}.h` | 11 declaration-only forwarders; `cpu_state.h` also pulls `ps2_float.h` (OLD declared the float helpers there) and defines the four `VR_reg` component operators (P3-VROPS); `memory.h` routes `memory::read<T>/write<T>/read_quad/write_quad` to `RecompAbi::Memory()`; `vu.h` declares `Vu0Macro`/`Vu1Window` |
| `engine/ee/recomp_abi/recomp_abi.{h,cpp}` | `RecompAbi::BindMemory/Memory/HasMemory`; inert globals: `g_logFile` (unopened), `g_call_census_enabled=false` + empty bodies, `StreamPollCompleteMarker`→false, `g_alloc_trace_seq`/`g_alloc_live(_mutex)`/`g_pose_wb_count`/`g_ring_watch_addr`, objects `g_vu0`/`g_vu1`, `GetPtcRecord`→nullptr |
| `engine/ee/cpu/park.{h,cpp}` | `Park::Forever(reason, pc)` `[[noreturn]]`: records `ParkRecord`, calls the installed `Sink` (3F: run.meta + token release), then waits on a never-notified condvar; a throwing sink is the only way out (the unit test's path) |
| `engine/ee/cpu/seams.cpp` | every NEW-provided roster body (table below); the ONLY definer of the row-26 `extern "C"` trio (loop 1, P3-SEAM-4) |
| `engine/core/log/ring_log.{h,cpp}` (loop 1) | extern-C trio REMOVED (was a second strong definition); `FatalNoJump` → `FatalNoJumpReport(pc)` (dump + report, returns, no `_exit`); `BindGeneratedGate(bool*)` (latched at Bind and on every `Configure`) |
| `engine/ee/cpu/dispatch_table.{h,cpp}` | `DispatchTable`: `Initialize` (runs the generated `initialize_recompiled_functions` once, then `RingLog::BindGeneratedGate(&crashring_enabled)` — loop 1), `FunctionCount/AddressOfId/IdOf`, `Install` THROUGH `update_function_impl`, `IsHooked`, `Invoke` — defines nothing the common TU defines |
| `engine/ee/cpu/shadow_call_stack.{h,cpp}` | `ShadowCallStack`: no storage; `Depth/Top/Push/Pop` over the common TU's `extern "C"` accessors |
| `games/<t>/hle/glue/{sce_graph,sce_dma,crt0,libc_heap}_glue.cpp` + `README.md`, `games/<t>/hle/workarounds/setup_display_registers.cpp` | 19 one-line pass-throughs to the hooked addresses' generated bodies (`0x2c1bd8`/`0x2c1ab0`/`0x2c1190`, `0x2c12e0`/`0x2c1030`, `0x2d69a0`, 12× `0x2c….`, `0x19aff0` — read from the common TU's install sites) |
| `games/<t>/recomp/CMakeLists.txt` | `ps2r_title_recomp` STATIC: 32 shards `-O2`, common TU `-O0`, all `-w -fno-strict-aliasing -fwrapv`, `-I recomp_abi/anchor`; glue TUs included; fails configure unless exactly 32 shards |
| `engine/ee/CMakeLists.txt`, root `CMakeLists.txt` | 3B sources added to `ps2r_ee`; `add_subdirectory(games/${PS2R_TITLE}/recomp)` |
| `tests/ee/test_dispatch_table.cpp` + `tests/ee/CMakeLists.txt` | `RecompDispatchTableLink` (links the library), `RecompSymbolCensus`, `RecompManifestIntact` |
| `tests/lint/lint_one_class.py` | exemptions: `/ee/recomp_abi/host_app/` (the one generated-code-ABI class) and `/ee/cpu/seams.cpp` (free-function seam bodies) |
| `tests/core/check_getenv_sites.py` | the PHASE3_PLAN §3.3 dated exemption: exactly `games/<t>/recomp/recompiled.cpp` with exactly 6 arms (a changed count fails) |
| `engine/ee/recomp_abi/README.md`, `games/<t>/recomp/README.md`, `tools/README.md` | records |

**Seam providers by kind (108 roster entries):**

| Kind | Count | Entries |
|---|---|---|
| defined by the generated TU (NEW defines nothing) | 31 | row 7 minus `recomp_fcf_miss` (21), row 8 (6), row 10 (4) |
| not a seam (TU-internal static) | 1 | `g_func_trace_enabled` |
| real — types + forwarders | 11 | `CpuContext`, `QuadWord`, `memory::*` ×4 → `FastMem`, `ps2_*` ×4 → `Ps2Float` (the `ps2_*` bodies are the 3A `.inl`); **loop 1 (P3-SEAM-4):** `crashring_enabled` (the guards' plain-bool gate, bound to `RingLog` by `DispatchTable::Initialize`, follows the verbose EE bit) + `crashring_record_indirect` → `RingLog::EmitV(CH_EE, TAG_INDIRECT_JUMP)` |
| park (`Park::Forever`, never returns) | 24 | `hle_sceSif{Init,Bind,Call}Rpc` (`sif_rpc_absent`), 10 `hle_sceCd*` + 5 `HLE_FUN_002a*` (`iop_dependent`), `crashring_fatal_no_jump` (`RingLog::FatalNoJumpReport` first: ring_crash.rlog + crash_report.txt, then the park) + `recomp_fcf_miss` (routes through it, as OLD's `recomp_seams.cpp`) (`fatal_no_jump`), `g_vu0.Execute`/`g_vu1.Execute` (`vu_micro_absent`), `ps2_threading::exitThread` (`thread_manager_absent`, 3C1 replaces) |
| interim park (real provider in the named stage) | 9 | `runtime_syscall_dispatcher`/`handle_mtc0_write` (3C2 SyscallTable), `hle_TimerWaitMicroseconds` (3C2), `hle_InitTLB`/`hle_ContextRestore` (3C2 kernl_boot), `hle_sceGsSyncV`/`hle_sceGsSyncPath` (3D2 gs_sync) — 7 bodies; plus `g_sceGsSyncV_count`/`g_vsync_frame` defined in `seams.cpp` until 3D2 |
| inert | 13 | `g_logFile`, `g_call_census_enabled`, `call_census_enter/exit`, `StreamPollCompleteMarker`, 5 diag globals, `GetPtcRecord`, `g_vu0.ResumeIfWaiting`, `ps2_threading::lookupThreadInfo` (nullptr until 3C1) |
| title pass-through (declared engine-side, defined in `games/<t>/hle/`) | 19 | rows 16 (3), 17 (2), 19 (1), 21 (1), 22 (12) |

31+1+11+24+9+13+19 = 108 (loop 1 moved the two row-26 entries from inert to real).

### Part 5 — (D) the shard build, measured

Validation compile (common TU + shard 000 + glue) surfaced P3-SEAM-2/3 and P3-VROPS (above), then all 32 shards
compiled. Clean measured build of the library alone (`ninja -t clean ps2r_title_recomp`, then
`/usr/bin/time -l run_bounded.sh 900 -- cmake --build build --target ps2r_title_recomp -j4`):

```
100.62 real   343.98 user   19.12 sys      exit=0
maximum resident set size 4,050,255,872   (largest single compile process, -j4)
libps2r_title_recomp.a = 489,510,184 bytes; objects 496 MB
```
p3br's independent clean `-j4` rebuild (loop 1): `100.58 real / 354.65 user`, max RSS 4,900,864,000 (4.90 GB). Wall
time reproduces; the aggregate RSS varies run to run (4.05 → 4.90 GB: `time -l` reports the largest child, and which
of the -j4 slots the common TU shares its peak with differs) — consistent with the per-TU common-TU figure below.
The bound the plan states (≤ 3 GB) is exceeded by the common TU alone; the 3B′ remedy hand-off stands.
Per-TU isolation (each object deleted and rebuilt alone under `/usr/bin/time -l`):
```
recompiled.cpp.o          (-O0, common TU)   55.27 real   maxrss 4,833,673,216  (4.83 GB)
recompiled_shard_009.cpp.o (-O2, VU0-macro)  10.11 real   maxrss   648,953,856  (0.65 GB)
```
Wall 101 s ≪ the plan's 15 min bound. The shards are far inside the ≤ 3 GB bound; **the `-O0` COMMON TU peaks at
4.83 GB** (its `initialize_recompiled_functions` builds 8,819 `std::function` map entries + 8,659 `function_ranges`
lambdas in one function). Per the stage brief this is RECORDED, not fought: the plan's remedy (`-O1` shards) does not
apply; the candidate remedies for 3B′ are `-O1` on the common TU (OLD compiles it at `-O0` — a build-tuning setting
only if the generated init code is proven unaffected) or accepting 4.83 GB on the dev machine with the 8 GB
`run_bounded` RSS cap as the guard.

### Gate status after 3B

```
$ ./verify/run_bounded.sh 900 /tmp/p3b/build_fix3.log -- cmake --build --preset macos-dev -j4   # exit 0
$ ./verify/run_bounded.sh 600 /tmp/p3b/ctest2.log -- ctest --preset macos-dev
```
```
100% tests passed, 0 tests failed out of 54     (51 carried + RecompDispatchTableLink + RecompSymbolCensus + RecompManifestIntact)
```
Re-run after loop 1 (`/tmp/p3b/ctest_loop1.log`): `100% tests passed, 0 tests failed out of 54`, 128.05 s.
New ctest entries: `RecompDispatchTableLink`
(`PASS RecompDispatchTableLink: 8819 functions, hook installed through update_function_impl, shadow stack
round-trip, crash ring gate + record + report, park unwound`; pins: 8,819 dense ids ascending from `0x00100000` to
`0x002d9968`, `IdOf∘AddressOfId`, every id hooked after init = classic dispatch because `PS2_EE_RAWDISP` is unset —
`recompiled.cpp:4302-4307`, `Install` off a function start fails; loop 1: `crashring_enabled` off before
`Initialize`, on after (verbose EE configured), re-latched by later `Configure` calls, `crashring_record_indirect`
lands ONE `INDIRECT_JUMP` on the thread ring only on the verbose tier, `crashring_fatal_no_jump` writes
`ring_crash.rlog` + `crash_report.txt` into a scratch dir and never returns; the binary references `RingLog` AND the
trio = the duplicate-definition link guard), `RecompSymbolCensus`, `RecompManifestIntact`. Lints: `LintOneClass` 138 files 0, `LintEnginePurity` 136 files 0 (widened range),
`CoreSingleGetenvSite` PASS with the dated generated-file exemption.

### HANDOFF (3B → 3C1)

**Done:** the four p3ar items; the four p3br loop-1 items (below); `tools/regen_from_frozen.sh` + `games/<t>/recomp/` (34 files, deterministic, manifests,
`INVOCATION.md`); census 108/108 + 11/11 + 0 unlisted (scan added; three plan defects found and recorded as
P3-SEAM-1/2/3, P3-VROPS); `engine/ee/recomp_abi/` (11 forwarders + `recomp_abi` + anchor); `engine/ee/cpu/{park,
seams.cpp, dispatch_table, shadow_call_stack}`; `games/<t>/hle/glue/*` (19 pass-throughs); `ps2r_title_recomp`
builds (101 s wall, -j4) and links into `RecompDispatchTableLink`; ctest green; lints green.

**Not done / carried to 3B′ (pre-registered split) and 3C1:**
- Compile RSS: the `-O0` common TU peaks at 4.83 GB (> the 3 GB bound); shard 009 at 0.65 GB. 3B′: measure the other
  31 shards the same way (`rm build/…/recompiled_shard_NNN.cpp.o && /usr/bin/time -l cmake --build build --target
  games/<t>/recomp/CMakeFiles/ps2r_title_recomp.dir/recompiled_shard_NNN.cpp.o`), then decide the common-TU remedy and
  record it as a build-tuning setting in PHASE3_PLAN §3.3 (`-fwrapv`/`-fno-strict-aliasing` never change).
- `ps2r_title_recomp` is not yet linked into `nativeee` (the app has no composition root for it until 3F); the
  plan's "links into nativeee (unused yet)" is deferred to 3F with the boot leg.
- `Park::Forever` has no sink installed outside the test: 3F installs the `run.meta` writer + `EeRunToken` release.
- The interim parks (rows 6, 18, 20, 15) and `ps2_threading::*` are replaced by 3C1 (`ThreadManager`: `exitThread`,
  `lookupThreadInfo(1)->gp`) and 3C2 (`SyscallTable`, `kernl_boot`, `kernl_timer`), 3D2 (`gs_sync`); each replacement
  deletes the body from `seams.cpp` and the census stays 108.
- `RecompSymbolCensus` takes 94 s (regex over 91 MB); acceptable, but a `--fast` mode (shards scanned once for the
  family set) is a cheap 3B′ improvement if ctest time matters.

- Naming hygiene (not a review item, noted for 3C1/3F): the tree has two classes named `Park` — `ps2r::Park`
  (`core/threads/park.h`, the deadline wait primitive) and the global `::Park` (`ee/cpu/park.h`, the guest-thread
  terminal park). Distinct namespaces, both lints pass; a TU including both must qualify. Rename candidate:
  `ee/cpu/park` → `GuestPark`/`guest_park` when 3F installs the sink (one rename, one stage).

### Loop iteration 1 (p3br review of the 3B submission) — the four items as applied

| # | Item | Applied |
|---|---|---|
| 1 (blocking) | row-26 trio defined twice (`seams.cpp` + Phase 1's `ring_log.cpp`; `nm` showed both; `ld: 3 duplicate symbols` once a TU referenced `RingLog::Configure`; bodies disagreed: `_exit(1)` vs park) | ONE provider = `seams.cpp` (layering: `core` knows nothing above it, the park is `ee/`). `ring_log.{h,cpp}`: trio removed; `FatalNoJump` → `FatalNoJumpReport(pc)` (flush, `ring_crash.rlog`, `crash_report.txt` + stderr with the guest GPR block, RETURNS — no `_exit`); `BindGeneratedGate(bool*)` (Configure's direct write to `crashring_enabled` replaced; the bound gate is latched at Bind and on every Configure so the two calls commute). `seams.cpp`: `crashring_enabled` (bound), `record_indirect` → `EmitV(CH_EE, TAG_INDIRECT_JUMP)`, `fatal_no_jump` = report then `Park::Forever("fatal_no_jump", pc)`; `recomp_fcf_miss` → `crashring_fatal_no_jump` (OLD's routing). `DispatchTable::Initialize` binds the gate after `initialize_recompiled_functions`. Tests: `test_ring_log` now tests the bind (off → on → unbound untouched → late bind latches); `test_dispatch_table` = the link guard (references `RingLog` + the trio) and pins gate/record/report/park. Proof: `nm ring_log.cpp.o | grep _crashring` = 0, `seams.cpp.o` = 3 (`S _crashring_enabled`, `T _crashring_fatal_no_jump`, `T _crashring_record_indirect`); `test_dispatch_table` binary contains `RingLog::Configure`. Recorded: PHASE3_PLAN §3.4 row 26, §2.1 seams row, §2.9, §3.4.4 kind sum (real 11 / inert 13), §7.1 **P3-SEAM-4**; CONSTRUCTION_PLAN `core/log/ring_log` row annotated. |
| 2 | `games/<t>/data/README.md:15` still cited `--ram-image-probe` | replaced by the real invocation (`test_elf_loader <ELF> <map> <patches>`, probe = the `ElfLoaderTitle` test) with the map end `0x0030A0BB` exclusive; `grep -rn ram-image-probe` now hits only the IMPL record of the fix |
| 3 | RSS variance (p3br: 4.90 GB aggregate vs the recorded 4.05 GB) | recorded under Part 5 with the explanation (largest-child accounting under -j4); the 3B′ common-TU remedy hand-off unchanged |
| 4 | stale 3A-rename objects `cpu_context.cpp.o` / `fastmem.cpp.o` in `build/engine/ee` | `ninja -C build -t cleandead` → "Cleaning... 2 files."; neither remains |

Gate after loop 1: `cmake --build --preset macos-dev -j4` exit 0 (bounded, `/tmp/p3b/build_loop1.log`); ctest re-run
54/54 (Gate status block above); `LintOneClass` / `LintEnginePurity` pass unchanged (no new files, seams.cpp still exempt).

**Next command (stage 3C1):** read `OLD/host_app/ps2_threading.{h,cpp}`, `ps2_scheduler.cpp`, `ee_sched/*`,
`tests/{r139_threads,test_ee_sched.cpp}`; write `engine/ee/hle/{thread_manager, semaphores, event_flags, alarms,
rpc_callback_queue}`, `engine/ee/sched/{ee_run_token, ee_event_queue}`; replace the `ps2_threading::` bodies in
`seams.cpp`; tests `ThreadManagerThbase`, `EeRunTokenAndEventQueue`, `RelaunchOrder`; then
`cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`.

## 3C1 — kernel objects + run token + event queue (stage p3c1, 2026-09-26)

### Files landed (one primary type per pair; global namespace like the rest of `engine/ee/`)

| File | Content | OLD source (read-only) | Shape |
|---|---|---|---|
| `engine/ee/hle/kernel_codes.h` | THS_* / TSW_* / KE_* / WEF_* / EA_MULTI / SA_THPRI constants (no primary type) | `ps2_threading.h:20-66` | verbatim values |
| `engine/ee/hle/thread_manager.{h,cpp}` | `ThreadManager`: 255 ids (1 = main), 128 priorities, states RUN/READY/WAIT/SUSPEND/WAITSUSPEND/DORMANT, Create/Delete/Start/Exit/ExitDelete/Terminate/ReferStatus/Sleep/Wakeup(banking)/CancelWakeup/Suspend/Resume/ReleaseWait/ChangePriority/Rotate/Disable/EnableDispatch; one pthread per guest thread (`SetStackBytes`, default 1 GiB virtual = OLD's value); `GuestBody` hook for the dispatch loop (3F); `AdoptCurrentThread` (main thread at boot / test threads); `SetParkObserver` (savestate THRD harvest hook), `SetIntcRaise` (kIntcRaise applier, 3D2 installs); `InstallEventApplier` = the queue applier over the direct bodies; the header quotes thbase rules R1–R10 | `ps2_threading.cpp` threads half (token arms only: `PS2_EE_RUNTOKEN` NORM ON), `ps2_scheduler_old_fiber.cpp` iWakeupThread banking rule (read) | split / rewrite |
| `engine/ee/hle/semaphores.{h,cpp}` | `Semaphores`: FIFO vs SA_THPRI release (S2), OVF, Wait/Poll/ReferStatus, KE_WAIT_DELETE / KE_RELEASE_WAIT, `RemoveWaiter` for ReleaseWaitThread; header quotes S1–S5 | `ps2_threading.cpp:1057-1378` | split |
| `engine/ee/hle/event_flags.{h,cpp}` | `EventFlags`: AND/OR, clear-on-wait 0x10 (waited bits) / 0x20 (all), EA_MULTI, release-scan in queue order with immediate clear (E3), pure `Satisfied`/`AfterRelease`; header quotes E1–E6 | `ps2_threading.cpp:1781-2104` | split |
| `engine/ee/hle/alarms.{h,cpp}` | `Alarms` (the brief's "AlarmTable"; file = snake_case(type) rule): SetAlarm/ReleaseAlarm in the TICK domain, `FireDue(now_tick)` is the ONLY place a handler runs (boundary step) | `syscalls.cpp:2386-2393` (NotImplemented stubs) | rewritten (count-domain hook) |
| `engine/ee/hle/rpc_callback_queue.{h,cpp}` | `RpcCallbackQueue`: Push (any thread) / Drain (holder) / WaitUntilNonEmpty; empty in Phase 3 | `syscalls.cpp:40`, `main.cpp:669/4856` consumers | verbatim mechanism |
| `engine/ee/sched/ee_run_token.{h,cpp}` | `EeRunToken`: single holder, GuestEnter/Exit, BlockBegin/End, ParkBegin/End (+ nested `ParkScope`), MakeReady (seq at the readying event), PreemptPoint, Rotate, SetPriority, Drop, Shutdown, BeginRelaunch/EndRelaunch (BUG-036), nested `HostGate`; `CurrentTid`, `ElectionHeld`, `ResetForTest` | `ee_sched/ee_run_token.{h,cpp}` | verbatim minus the env hatch (`PS2_EE_RUNTOKEN` NORM ON) and the `[EESCHED]` stderr lines (`PS2_EESCHED_DIAG` DIAG — see handoff) |
| `engine/ee/sched/ee_event_queue.{h,cpp}` | `EeEventQueue`: 256-slot FIFO ring, `Enqueue` + park pokes, `DrainOnToken` (applying flag for the whole drain), `DrainIfPendingOnHolder` (half a, counted refusals), **the ONE routing predicate** `CallerMayMutate()` + the single helper `RouteIfForeign(ev)` (half b), `SetApplier`, nested `ParkReg` | `ee_sched/ee_event_queue.{h,cpp}` | verbatim; `ApplyEeEvent` free function → installed applier |
| `engine/ee/cpu/guest_park.{h,cpp}` | `GuestPark` (renamed from `Park`; the 3B handoff's naming-hygiene item) | — | rename |
| `engine/ee/cpu/seams.cpp` | row 13 REAL: `ps2_threading::exitThread` → `ThreadManager::ExitThread` (throws `ThreadExitUnwind`), `lookupThreadInfo(tid)` → `ThreadManager::Lookup`; interim park bodies deleted | — | seam |
| `engine/ee/recomp_abi/host_app/ps2_threading.h` | `ThreadInfo` is now `::ThreadInfo` (ThreadManager's record; the generated code reads `->gp`) | — | forwarder |
| `engine/ee/CMakeLists.txt`, `tests/ee/CMakeLists.txt` | 7 new sources; tests `ThreadManagerThbase`, `EeRunTokenAndEventQueue`, `RelaunchOrder` (TIMEOUT 120) | — | — |

Routing sites (every wake-type mutator's first line is `EeEventQueue::Instance().RouteIfForeign(...)`; no other site
decides routing): `Semaphores::Signal`, `EventFlags::Set`, `EventFlags::Clear`, `ThreadManager::WakeupThread`,
`ThreadManager::ResumeThread`, `ThreadManager::ReleaseWaitThread` (6; INTC raise arrives in 3D2 as the 7th through
`kIntcRaise` → `SetIntcRaise`). `grep -rn RouteIfForeign engine/` lists exactly these six call sites plus the definition.

thbase rules quoted in `thread_manager.h` (R1 priorities/FIFO/no time-slicing, R2 preemption at dispatch points,
R3 StartThread, R4 wakeup banking incl. self-wakeup, R5 CancelWakeup, R6 suspend counting, R7 ReleaseWait, R8
ChangePriority→tail / Rotate head→tail, R9 Terminate/Delete/Exit, R10 ids + priority clamps), S1–S5 in
`semaphores.h`, E1–E6 in `event_flags.h`.

### Tests

| ctest | Binary | Checks | Content |
|---|---|---|---|
| `ThreadManagerThbase` | `tests/ee/test_thread_manager.cpp` | 42 | OLD `tests/r139_threads` kernel checks ported under the BUG-035 token model: T3a–d (self-wakeup banking), T4a–d (sema signal-before-wait), T5a–c (two-waiter conservation, OVF backpressure + rotate), T6a–d (host-side SetEventFlag routed and drained by the parked waiter; ≥100 ms real park); NEW: B1a–h (StartThread run order = priority then FIFO through the real pthread path; KE_NOT_DORMANT; ReferThreadStatus READY→DORMANT; DeleteThread; R10 clamps), B2a–e (sema FIFO vs SA_THPRI observed on the wait queue; OVF; Poll; unknown id), B3a–h (event-flag truth table; 0x10 clears the waited bits, 0x20 all; Poll pre-clear pattern; ILPAT/ILLEGAL_MODE; Clear mask; KE_EVF_MULTI), B4a–e (alarms fire only at `FireDue`, never on wall time; release; id order) |
| `EeRunTokenAndEventQueue` | `tests/ee/test_ee_sched.cpp` | 30 | the 24 OLD `test_ee_sched.cpp` checks (single runner, no async deschedule, election, preemption, CTP tail, FIFO queue, poke, b035a ×6, b035b ×4, b035c ×2) + b035d ×5: the six routing sites enumerated — a foreign caller enqueues each (state untouched, queue +1) under a BUSY holder; the holder's next scheduling point applies all six in place; predicate false off-token / true for the holder |
| `RelaunchOrder` | `tests/ee/test_relaunch_order.cpp` | 6 | BUG-036: BeginRelaunch holds elections + clears the owner; no resumed thread runs while held; EndRelaunch = one election → prio 10 runs before prio 64 regardless of file order (1 before 2) and host arrival order (both orders tried); the old incarnation's GuestExit is a no-op; the set drains |

NOT ported from r139 (recorded, not silent): T1a–e, T2a–e, T7a (11 checks) test the OLD `perf::EEGameSlice/EEGamePause/
EeSlotFlush` per-guest-thread busy/wait booking — `PS2_PERF` DIAG machinery that `core/diag/perf_profiler` does not
carry (no per-thread slots). T6c/T6d are re-expressed as the kernel-observable facts they proved (the waiter really
parked ≥100 ms with the token released; the queue is empty after the drain). 26 − 11 = 15 kernel checks ported 1:1 (T3a–d 4 + T4a–d 4 + T5a–c 3 + T6a–d 4; p3c1r nit (a) corrected the earlier 27/16).

### Fixes found by the tests (recorded)

- `StartThread` joined the previous host thread BEFORE the DORMANT check (OLD order): a second StartThread on a READY
  thread whose host thread waits for the token deadlocked the caller. NEW checks DORMANT first, then joins.
- A started thread reported `THS_RUN` before it was elected (OLD set RUN in the worker prologue). NEW keeps `THS_READY`
  until `GuestEnter` returns — `ReferThreadStatus` of a readied-but-not-running thread is READY, as thbase says.

### Gate status after 3C1

```
$ ./verify/run_bounded.sh 900 /tmp/p3c1/build6.log -- cmake --build --preset macos-dev -j4      # exit 0
$ ./verify/run_bounded.sh 900 /tmp/p3c1/ctest.log -- ctest --preset macos-dev                   # exit 0
100% tests passed, 0 tests failed out of 57      (54 carried + ThreadManagerThbase + EeRunTokenAndEventQueue + RelaunchOrder)
Total Test time (real) = 134.78 sec
```
`LintOneClass`: 156 files scanned, 0 findings. `LintEnginePurity`: 151 files, 0 game tokens. `RecompSymbolCensus`:
ROSTER TOTAL = 108, MISSING = 0, 0 UNLISTED EXTERNS (census unchanged: row 13 changed provider, not roster).
`RecompDispatchTableLink` passes with `GuestPark`. §7 3C1 green criterion: 3 tests green; thbase election + KE_* codes
pinned; holder-only drain AND the `ApplyingOnThisThread` mutator rule pinned; lints 0.

### HANDOFF (3C1 → 3C2)

**Done:** everything in the file table; seams row 13 real; census 108; `GuestPark` rename; 3 suites (78 checks); tree green.

**Not done / carried:**
- `Config.engine.guest_stack_mb` (SETTING) is NOT a Config field yet: `ThreadManager::SetStackBytes` takes the value; the
  composition root (3F) reads it from Config when the field is added together with the settings-table regeneration
  (`tools/gen_settings_table.py`, `SettingsTableReproducible`). Default = OLD's 1 GiB.
- `PS2_EESCHED_DIAG` (DIAG): the `[EESCHED]` event stream (enter/run/block/preempt/rotate/exit/ready/enq/drain lines,
  one `write(2)` per line) is not transplanted; it belongs to a `core/diag` category under `PS2R_DIAGNOSTICS`. The
  harness's paired-run interleave comparison needs it — add in 3F with the composition root (hook points are the
  existing `RunPostAcquire`/`ReleaseLocked` edges).
- `InitMainThread` / `InitHeap` / `EndOfHeap` are 3C2 (`BootEnv` + `SyscallTable`); the syscall layer decodes
  `ThreadParam`/`SemaParam`/`EventFlagParam` from guest memory and writes `ThreadStatus`/`SemaStatus` back — the
  kernel objects take PODs only.
- `kIntcRaise` applier is a no-op until 3D2 calls `ThreadManager::SetIntcRaise(Intc::Raise)`.
- Savestate `startThreadResumed` (relaunch a thread at a saved chain) is 3E: `ThreadManager` exposes `SetParkObserver`
  and `ThreadInfo::ctx`; the relaunch protocol itself (`BeginRelaunch`/`MakeReady`/`EndRelaunch`) is pinned by
  `RelaunchOrder`.
- `engine/ee/hle/README.md` / `sched/README.md` "Belongs here" lists still name the plan's class set (unchanged; the
  new files match them); `kernel_codes.h` is a constants header (no primary type).

**Next command (stage 3C2):** read `OLD/host_app/syscalls.cpp:2315-2504`, `ps2_syscall_wrappers.*`, `ps2_argv.h`,
`libc_hle.*`, `sce_hle/*`, `sif_hle.cpp` boot-env half, `main.cpp:5242-5300`; write `engine/ee/hle/{syscall_table,
dyn_interp, boot_env, libc, sdk_binding, kernl_boot, kernl_timer, kernl_file_io, kernl_sif_rpc, libscf_bindings.cpp,
libdev_bindings.cpp}` over `ThreadManager`/`Semaphores`/`EventFlags`/`Alarms` (syscall numbers 32–70 map 1:1 onto the
methods above; 24/25/30/31 → `Alarms` with `DerivedClocks` ticks in 3D1); replace the `runtime_syscall_dispatcher` /
`handle_mtc0_write` / kernl_* interim parks in `seams.cpp`; then
`cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`.

## 3C2 — syscalls + boot env + libc + SDK binding manifest + libkernl families (stage p3c2, 2026-09-26)

### p3c1r nits closed first
(a) r139 arithmetic above corrected to 26 − 11 = 15. (b) `seams.cpp:3` `GuestGuestPark` → `GuestPark`.
(c) `EeEventQueue::RingFullDrops()` — every ring-full drop in `Enqueue` is counted (a nonzero count is a
lost guest wake = construction finding; the harness reads it in 3F). (d) the ChangeThreadPriority-self
fidelity question is recorded in PHASE3_PLAN §7.1 (P3-THPRI-SELF).

### Files landed (one primary type per pair; global namespace like the rest of `engine/ee/`)

| File | Content | OLD source (read-only) | Shape |
|---|---|---|---|
| `engine/ee/hle/syscall_table.{h,cpp}` | `SyscallTable`: `Dispatch(v1, ctx)` = the 119 live labels (`kLabels`, `static_assert == 119`) + `default:` (counted + ring-logged, never fatal); `$v1` normalisation (negative → abs, low 8 bits); threads 32–58 → `ThreadManager` (ThreadParam 7-word decode with OLD's gp/priority heuristic; ReferThreadStatus 12-word write, tid 0 = self, returns tid); 60–62 → `KernlBoot`; semas 64–70 (SemaParam clamp rule); event flags 80–88 (result written when a3 ≠ 0); alarms 24/25/30/31 → `Alarms` (tick from `SetTickClock`, handler runs guest code through `SetGuestCall`); INTC/DMAC Add/Remove/Enable/Disable handler registry (`IntcHandlers()/DmacHandlers()` — 3D2's `Intc` consumes) + `_EnableIntc/_DisableIntc/_EnableDmac/_DisableDmac` mask toggles returning "changed"; SIF idle results (plan §8.4): SetDma → 0 without a command entry else running id, DmaStat → −1, SetDChain → 1, SetReg/GetReg over the 3 system words + a mirror of the 6 physical registers with the SIFINIT/CMDINIT auto-ack on SMFLAG (the MMIO sink drops writes until Phase 4's `sif/` handler); GsPutIMR/GetIMR + SetGsCrt recorded (`GsImr()`); SetSysCall → custom table + kernel vector word at `0x800002E0+4i`, redirected numbers run guest code through the hook or trip `DynInterp`; GetOsdConfigParam 8 words; Deci2 → −1; GetMemorySize 32 MB; the 41 RFU/cache/TLB/Sbus labels = counted no-ops with `$v0` untouched (OLD shape). `HandleMtc0`: Status writable mask `0xF0FF003F`, Cause IP0/IP1, Compare clears IP7, Count (rd 9) reseats the free-running counter, others direct | `syscalls.cpp:91-160, 163-315, 317-373, 484-510/775-780, 791-800, 819-900, 1528-1930, 1938-2180, 2235-2504`, `ps2_syscall_wrappers.cpp`, `ps2_threading.cpp:363-400/887-905/1057-1075/1781-1790` | split / rewrite |
| `engine/ee/hle/dyn_interp.{h,cpp}` | `DynInterp::Trip(pc, ctx)` [[noreturn]]: counts, records pc, `RingLog::EmitV(CH_EE, TAG_INDIRECT_JUMP)`, `park=unrecompiled_pc`. No interpreter body (rabbitizer link deferred to a tier that is never needed for the boot leg) | `syscalls.cpp:2182 dynamic_decode_and_execute` (mechanism only) | tripwire |
| `engine/ee/hle/boot_env.{h,cpp}` | `BootEnv` + `BootParams` POD (argv, argv_block, sbrk_break_word, heap_limit_cap, default_heap_base — ALL title-supplied) + `ArgvLayout`; `BuildLayout` (argc@+0, 7 pointer slots @+4, strings @+0x20), `WriteArgv`, `PreSeed`, `MainThreadSp/StackBase`, `HeapLimit`, `kRamTop` | `ps2_argv.h`, `main.cpp:538-576` pre-seed, `ps2_threading.cpp:912-1050` | split; `PS2_ARGV` NORM (retail only; the legacy vector is not representable — no env) |
| `engine/ee/hle/libc.{h,cpp}` | `Libc::Memcpy/Memmove/Memset/Strlen` guarded register-exact cores (128-bit residues incl. the caller's stale-half reads); `SetProtectedBand` (OLD's VTABLE-PROTECT band becomes a title registration; engine default none) | `libc_hle.{h,cpp}` cores verbatim (minus MIPWR diag, `PS2_LIBC_HLE` NORM ON) | verbatim |
| `engine/ee/hle/sdk_binding_manifest.{h,cpp}` | `SdkBindingManifest`: engine rows `{library, name, kind SdkBind/SdkWrap, handler}` — NO address field; `RegisterBuiltins()` (kernl_boot/timer/file_io/sif_rpc + libscf + libdev + the 4 libc SdkWrap rows); `LoadTitleManifest(tsv)`; `Resolve()` joins on (library, name); `Install(rows, installer, capture)` in title-file order through the INJECTED install primitive (DispatchTable::Install in 3F) — rows without a handler get a trap body `park=sdk_unimplemented` (OLD: hard-error stub) | `sce_hle/sce_hle_registry.cpp:527-604` (install shape) | rewritten (same seam) |
| `engine/ee/hle/kernl_boot.{h,cpp}` | `KernlBoot`: InitMainThread (sp rule, adopts tid 1 RUN prio 0, `$gp/$sp`, argv at `args`), InitHeap (base −1 rule, limit via BootEnv, seeds the title's break word), EndOfHeap; seams `InitTLB` (0: flat map, ARCH §4.11) and `ContextRestore` (pc ← `$ra`, `$sp/$ra` from the thread record when the record's ctx is not the caller's); rows InitTLB/InitTLB32MB/InitTLBFunctions/EIntr/DIntr/StartThread(→ syscall 34)/InitThread/InitAlarm/_InitSys/InitSystemCallTableAddress/supplement_crt0/_sceIDC/sceSifRebootIop(1)/sceSifSyncIop(1)/sceSifInitRebootNotify | `libkernl.cpp:132-160, 384-440, 460-500, 685-690`, `ps2_threading.cpp:912-1050` | split by family |
| `engine/ee/hle/kernl_timer.{h,cpp}` | `KernlTimer::TimerWaitMicroseconds`: deadline = clock + `(int)(f12 × tick_rate@obj+0x70)` in the TICK domain; parks `tick_clock_absent` until 3D1 installs `SetClock(clock, wait)`; returns 1; rows InitTimer/StartTimerSystemTime | `timer_hle.cpp` (the seam; OLD slept 1 ms wall — not transplanted), `libkernl.cpp:460-461` | rewritten (tick domain) |
| `engine/ee/hle/kernl_file_io.{h,cpp}` | `KernlFileIo`: sceOpen/sceClose/write/get_iob → `park=iop_dependent`; sceFsInit/Reset/_sceFsWaitS/_sceFsSemInit/_sceFsIobSemaMK/sceFsSifCallRpc → 0 | `libkernl.cpp:315-370, 581-583, 715-765` | park owner (Phase 4 rebinds) |
| `engine/ee/hle/kernl_sif_rpc.{h,cpp}` | `KernlSifRpc::Park` = THE park owner (`sif_rpc_absent`): 26 libkernl rows (InitRpc/InitCmd/BindRpc/CallRpc/CheckStatRpc/RegisterRpc/SetRpcQueue/GetNextRequest/RpcLoop/AddCmdHandler/LoadModule ×2/_lf_bind/IopHeap ×4/WriteBackDCache/SetT2/sbrk/kFindAddress/kCopy/InitExecPS2/PatchIsNeeded/iGetTimerSystemTime/SetNextComp) + the row-14 seams | `libkernl.cpp` SIF family | park owner |
| `engine/ee/hle/libscf_bindings.cpp`, `libdev_bindings.cpp` | libscf: tobcd/frombcd/IsT10K(0)/GetRomName(0)/AdjustTime/GetSummerTime/GetTimeZone(0); `sceScfGetLocalTimefromRTC` parks `rtc_clock_absent` (Phase-4 fixed-clock consumer); libdev: 4 resets → 0 | `libscf.cpp`, `libdev.cpp` | move |
| `games/<t>/hle/sdk_bindings.tsv` | 291 rows `library\tname\taddress\tsig` (sig empty until the in-tree finder), provenance header with the .inc md5 `d58cca28c810317f8894a5ae87ae6637` | `sce_hle_bindings.inc` | DATA |
| `games/<t>/hle/title_profile.{h,cpp}`, `hle/CMakeLists.txt` (`ps2r_title_hle`) | `TitleProfile::ExampleTitleUS()`: serial, crc, elf path, entry, load/file ends, sdk 3.0.1, `boot{argv0, argv_block 0x30a980, sbrk_break_word 0x2ea844, heap_limit_cap 0x01F00000, default_heap_base 0x00200000}`, `sdk_bindings_file` | `runtime-environment.md`, `ps2_argv.h`, `ps2_threading.cpp initHeap` | data (first form) |
| `engine/ee/cpu/seams.cpp` | rows 6/18/20 REAL (SyscallTable, KernlTimer, KernlBoot); row 14 → `KernlSifRpc::Park`; census unchanged at 108 | — | seam |
| `engine/ee/mem/fast_mem.h` | trivial `Ram()` accessor (Libc addresses RAM bytes) | — | — |
| `engine/ee/sched/ee_event_queue.{h,cpp}` | `RingFullDrops()` counter (nit c) | — | — |

### Tests (`tests/ee/test_syscall_table.cpp`, one binary, five ctest names)

| ctest | Checks | Content |
|---|---|---|
| `SyscallTableCoverage` | 22 | 119 labels named (0x74 = SetOsdConfigParam); every label dispatches to its handler — none reaches `default:` (blocking labels 4/35/36/50/68/86 covered by name); unknown numbers counted + last recorded; `$v1` normalisation; idle results (GetMemorySize, DmaStat −1, Deci2 −1, MSFLAG→SMFLAG ack, system-register round trip); CreateSema/PollSema through guest memory; AddIntcHandler + `_EnableIntc` changed/unchanged; mtc0 Compare/Status/Count rules; RFU label leaves `$v0` |
| `BootEnvRetailArgv` | 10 | block at `0x30a980`: argc 1, ptr `base+0x20`, NULL, bytes == `"cdrom0:\SLUS_123.45;1\0"` (OLD `build_layout(retail_tokens())` image); layout 3 words + 22 bytes; InitMainThread sp/`$sp/$gp` + argv at `args`; InitHeap(−1,−1) = `0x01F00000` (sp clamp then the title cap — OLD parity) + break word seeded; EndOfHeap |
| `LibcHleDiff` | 7 | 400 random memcpy/memmove/memset vectors == C library, v0 = dest; 4 guard fall-backs (KSEG, garbage upper bits, RAM end, protected band); memmove backward and quad-residue register effects; memset M3 residues; 300 strlen vectors over S1/S2/S3; no-NUL fall-back |
| `SdkBindingManifestResolves` | 10 | TSV 291 rows, all inside PT_LOAD, distinct; **resolves 291/291 (64 engine handlers + 227 trap rows)**; installer receives 291 distinct addresses, 0 unresolved; engine rows carry no address; `sceSifInitRpc` → `KernlSifRpc::Park`; memcpy = SdkWrap with jr-$ra |
| `KernlParksTickAndSif` | 6 | timer parks `tick_clock_absent` without a clock; with a clock: deadline = now + (int)(f12×rate), 4 tick advances, returns 1; negative delay → 0; `KernlSifRpc::Park` never returns (sink unwind) with reason `sif_rpc_absent` and the guest pc |

### Fixes found by the tests (recorded)
- The coverage sweep first hung: a0 = 0 targets the CALLER for SuspendThread (parks forever) and throws
  `ThreadExitUnwind` for TerminateThread; the sweep now targets an unknown tid (200) for 33–58. The kernel
  semantics are OLD's (tid 0 = self); only the test changed.
- `sceSifSetReg(MSFLAG)`'s SMFLAG ack was written to the MMIO address only — the sink drops writes, so
  `GetReg(SMFLAG)` read the idle value. The physical-register mirror inside `SyscallTable` reproduces OLD's
  `g_sif` mirror until Phase 4's `sif/` MMIO handler displaces it.
- Two lint findings on the first pass: `sdk_binding.h` renamed to `sdk_binding_manifest.h` (file =
  snake_case(type)); `kDefaultHeapBase = 0x00200000` moved out of `engine/` into `BootParams.default_heap_base`
  (title data) — LintEnginePurity caught a kernel-default constant that sits inside the load range.

### Gate status after 3C2

```
$ ./verify/run_bounded.sh 900 /tmp/p3c2/build5.log -- cmake --build --preset macos-dev -j4      # exit 0
$ ./verify/run_bounded.sh 900 /tmp/p3c2/ctest2.log -- ctest --preset macos-dev                  # exit 0
100% tests passed, 0 tests failed out of 62      (57 carried + the 5 3C2 names)
Total Test time (real) = 129.53 sec
```
`LintOneClass`: 179 files, 0 findings. `LintEnginePurity`: 171 files, 0 game tokens. `RecompSymbolCensus`: 108, 0
missing, 0 unlisted (rows 6/14/18/20 changed provider, not roster). §7 3C2 green criterion: the 3 named tests
green (+2); `SdkBindingManifest::Install` runs over the title rows in file order through the injected primitive;
`runtime_syscall_dispatcher` / `handle_mtc0_write` linked to the shards through `seams.cpp` → `SyscallTable`;
every §3.4 row has a provider; lints 0.

### HANDOFF (3C2 → 3D1)

**Done:** everything in the file table; the five interim parks replaced; census 108; 5 suites (55 checks);
nits (a)–(d) closed; tree green.

**Not done / carried (honest gaps):**
- **227 of the 291 manifest rows are trap rows** (`park=sdk_unimplemented`) in the 3C2 form; OLD implemented
  ~100 of them (52 libkernl + libmc/libpad/libcdvd/libgraph/libdma/libipu/libmpeg/libsdr stubs). Boot-critical
  candidates that OLD bodied and NEW does not yet: none reached before `sceSifInitRpc` per the plan's boot
  order, but this is UNVERIFIED until the 3F boot leg runs — if the leg parks on `sdk_unimplemented`, the
  row name + pc in `run.meta` names the next body. Non-trivial OLD libkernl bodies parked under
  `sif_rpc_absent` on purpose (sbrk/kFindAddress/kCopy/InitExecPS2/PatchIsNeeded/SetT2/iGetTimerSystemTime/
  SetNextComp): each is IOP- or timer-dependent in OLD; 3D1/Phase 4 rebind them.
- `SyscallTable::SetGuestCall` (DispatchTable::Invoke) and `SetTickClock` are installed by the composition
  root (3F) / 3D1; until then a SetSysCall-redirected number trips `DynInterp` and alarms arm at tick 0.
- `KernlTimer::SetClock(clock, wait)` is 3D1's (DerivedClocks T0 count + the boundary wait); the seam parks
  `tick_clock_absent` until then. The wait hook must advance the emu clock at the boundary, never sleep.
- `KernlBoot::ContextRestore` is the thbase RFU-style return; its first real exercise is the 3F boot leg
  (the generated seam at `0x2d6880`). If the leg shows the kernel expects a full register restore (not just
  `$sp/$ra`), extend from the thread record's ctx — recorded as a 3F item, not guessed now.
- `games/<t>/hle/glue/*` pass-through TUs (§3.4.1, rows 16/17/19/21/22) WERE written in 3B (`crt0_glue`,
  `libc_heap_glue`, `sce_dma_glue`, `sce_graph_glue` + README — 19 pass-throughs, compiled into
  `ps2r_title_recomp`); 3C2 added nothing to them. (Corrected in 3D1: the original sentence here wrongly said
  they were not written.) `engine/ee/hle/README.md` "Belongs here" list not updated for the 3C2 files.
- `SdkBindingManifest` `signature` column is empty (reserved for the in-tree finder); the join key is
  (library, name). The `Capture` for SdkWrap rows is `DispatchTable`'s current body at install time (3F).
- `TitleProfile` first form only (no quirks/save/recomp fields yet — 3E).

**Next command (stage 3D1):** read `OLD/host_app/time_domain.h`, `ps2_vblank.{h,cpp}` (generator half),
`turbo.h`, `warp.h`, `budget_math.h`, `main.cpp:3226-3608`, `tests/test_time_domain.cpp`, `tests/r140_budget`;
write `engine/ee/timing/{tick_source, emu_domain, regime_detector, derived_clocks, budget_clock,
phase_classifier, turbo_governor, warp}`; install `KernlTimer::SetClock` and `SyscallTable::SetTickClock` from
`DerivedClocks`; then `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`.

## 3D1 — tick source + emu domain + derived clocks + budget (stage p3d1, 2026-09-26)

### p3c2r items closed first
1. `games/<t>/hle/sdk_bindings.tsv` gained the four `libc.a` rows (`memcpy 0x002c6ce0`, `memmove 0x002c6d8c`,
   `memset 0x002c6e88`, `strlen 0x002c72d4`; provenance OLD `libc_hle.cpp:456-459`, noted in the header) → 295
   rows. `SdkBindingManifestResolves` now asserts 295/295, **exactly 68 engine handlers (64 sce + 4 libc)**, and the
   REVERSE-drift set: engine rows with NO title match == `{libkernl.a sceSifFreeIopHeap}` exactly (printed; a new
   engine row that forgets its TSV row, or a TSV row that loses its handler, fails here instead of parking at boot).
   The set is as predicted by the review: `sceSifFreeIopHeap` is the only engine row the finder did not see in this
   ELF.
2. 3C2 HANDOFF sentence about `games/<t>/hle/glue/*` corrected in place (they WERE written in 3B; 3C2 added nothing).
3. `SdkBindingManifest::Install` trap body: `GuestPark::Forever("sdk_unimplemented", a, "<library> <name>")` — the
   INVOKED address (the dispatcher's `a`, i.e. the row's address) + the row name; `ParkRecord` gained a `detail`
   field (3F's `run.meta` writer prints it).
4. `KernlBoot::InitMainThread` heap base: `kInitMainThreadHeapBase = 0x00400000` named constexpr with the thbase
   rule (provisional [4 MiB, initial $sp) record before the title's InitHeap re-seats it; a kernel layout constant
   above the PT_LOAD memsz end, so LintEnginePurity is unaffected).
5. `SyscallTable::Dispatch` case 4 (Exit): `[[fallthrough]]` with the comment that `ExitThread` never returns
   (ThreadExitUnwind) — the fallthrough is unreachable and only satisfies control-flow analysis.

### Files landed (`engine/ee/timing/`, one primary type per pair, global namespace)

| File | Class / content | OLD source (read-only) | Shape / flags |
|---|---|---|---|
| `tick_source.{h,cpp}` | `TickSource`: the ONE generator; `Policy{Deterministic, Play}` (deterministic: no thread, `Tick()` demanded at boundary step 2; play: the pacer sleeps `PeriodUs()` then calls `Tick()` — the pacer's clock never crosses the interface, only the count does); `Mode{Realtime 16667 us, Freerun 0, Turbo 16667/K (K 2..64), Warp 0}`; `Bypass()` = mode ≠ Realtime | `ps2_vblank.{h,cpp}` VBlankWorkerFunc (generator half — the wall sleep is the play pacer's, not written here), `turbo.h` `kBasePeriodUs/PeriodUsForK` | split; `PS2_VBLANK_FREERUN` NORM (a mode), `PS2_TURBO` DELETE, `PS2_TICK_DETERMINISTIC` → `Policy` (Config `time.tick_deterministic`) |
| `emu_domain.{h,cpp}` | `EmuDomain`: `emu_count` (the only game-visible tick), `OnRawTick(raw)` carry with cap base+regime, `NoteArrival`, `OnBoundary(raw)` settle through `BoundaryAdvance` (lead governor ≤ 1, lag never repaid, monotonic), idle release **in raw ticks** (`kIdleReleaseTicks = 6` = 100 ms of 60 Hz fields — OLD's `kIdleReleaseNs` was the only wall term in the domain and no longer exists), bypass 1:1, `ReAnchor(raw)`, `SetRegimePin` (Config `engine.time_regime_pin`; deterministic profile pins 1) | `time_domain.h` [S94] live state + `BoundaryAdvance` (logic verbatim, wall-ns → raw-tick counting) | split; `PS2_TIME_EMU` NORM ON, `PS2_TIME_EMU_INTRA` NORM, `PS2_TIME_EMU_REGIME` → pin SETTING |
| `regime_detector.{h,cpp}` | `RegimeDetector`: windows 15 (fast, up > 0.50 / down > 0.95) then 120 (up > 0.30 / down > 0.90), UP = final_gap > n, DOWN = arrival_gap + 1 < n (the no-flap rule), bounds 1..5 | `time_domain.h` detector | verbatim |
| `derived_clocks.{h,cpp}` | `DerivedClocks(const EmuDomain&)`: the nine clocks (table below), `kCountsPerTick[4] = {2457600, 153600, 9600, 262}`, `kCop0CountsPerTick = 4915200`, `kRtcFixedEpochS = 1118836800` (2005-06-15 12:00:00 UTC), `EpochToCivil`, `HeartbeatTicksFor(usec) = max(1, round(usec/16667))`, `kCdvdKbPerTick = 256`; `InstallKernelClocks(SyscallTable&, wait)` → `KernlTimer::SetClock(T0Counts, wait)` + `SyscallTable::SetTickClock(VblankCount)` | `ee_timers.cpp` T0 constants, `time_domain.h`, `syscalls.cpp` mtc0 model, `turbo.h` HB, `ps2_vblank.h` delivery meter | split; `PS2_EE_TIM0` NORM, `PS2_T0_SUBTICK`/`PS2_T0_TICKPACED` → no wall subtick exists (see gaps), `PS2_RTC_FIXED` SETTING (`SetRtcFixed`), `PS2_CDVD_BUDGET_KB` NORM (256), `PS2_T0TRACE`/`PS2_T0_NOISR` DELETE |
| `budget_clock.{h,cpp}` | `BudgetClock`: `ThreshCounts` (ps2 single mul + saturating cvt.w.s), `PeriodCounts` (divu zext, /0 → 0xFFFFFFFF), `FoldLiteral`, `FoldClosed` (O(1), exact on the reachable domain), `Headroom` (the guest predicate's v0, sltu), the mode-1 synthetic clock `Query(anchor, base_eff, step) = anchor + base_eff + reads×step` with the per-anchor read count. MECHANISM ONLY: rate/fps/phase/anchor come from the guest object, base/base_low/step/lowdraw from the title (3E `quirks.budget`) | `budget_math.h`, `main.cpp:3226-3608` hook (mode 1 arm) | split; `PS2_BUDGET_EMU` NORM ON (mode 1; mode 2 = the wall re-import, not written), `PS2_BUDGET_BASE/_BASE_LOW/_LOWDRAW/_PHASE/_STEP` NORM → title params, `PS2_BUDGET_DIAG`/`PS2_LVL_DIAG` DIAG (not yet wired), `PS2_LOADER_HLE` early-out → 3E `hle/loader_sync` |
| `phase_classifier.{h,cpp}` | `PhaseClassifier::OnFrame(draws, lowdraw_max, base_low, base_hi, enabled)`: low after 2 consecutive `draws < lowdraw_max` frames, leave on the first full frame, streak saturates 0xFFFF | `budget_math.h` [S112-BOOTPHASE] | verbatim |
| `turbo_governor.{h,cpp}` | `TurboGovernor`: manual (`engine.turbo` 0/1=freerun/2..64), load turbo (`cdvd.load_turbo` SETTING default OFF: engage after 1500 ms continuous activity with gaps ≤ 150 ms and input quiet ≥ 2000 ms, release 250 ms after activity or at once on input, K = 4); inputs are the pacer's ms — pacing policy only, never a clock value | `turbo.h` (K scalar, load-turbo hysteresis constants) | split; `PS2_LOAD_TURBO` SETTING, `_ENGAGE/_GAP/_INPUT/_RELEASE_MS` NORM constants, `PS2_TURBO_DUCK` NORM (Phase 7 mixer consumer, not here) |
| `warp.{h,cpp}` | `Warp`: `SetTarget(harness.warp_to_frame)`, `OnFrame(frame)` releases at the target, `Apply(source, fallback)` | `warp.h` | verbatim; `PS2_WARP_TO` SETTING |
| `engine/ee/CMakeLists.txt`, `tests/ee/CMakeLists.txt` | 8 timing TUs in `ps2r_ee`; `test_timing` binary (3 ctest names) | — | — |

### The nine game-observable clocks (each a `DerivedClocks` accessor; header comment carries the same table)

| # | Clock | Accessor | OLD source | Domain now |
|---|---|---|---|---|
| 1 | EE timers T0–T3 COUNT | `TimerCounts(clks)` / `T0Counts()` | `ee_timers.cpp` (SyncV count × 9600 + WALL sub-tick interpolation) | emu ticks × counts-per-tick[CLKS]; **no wall term** (BUG-014/031/032 + PHASE0 §9 pinned the interpolation as the defect source) |
| 2 | VBlank tick | `VblankCount()` | `ps2_vblank.cpp` wall worker count | the emu tick itself (EE-synchronous) |
| 3 | frame boundary / SyncV FIELD parity | `FrameBoundaryCount()`, `SyncVFieldParity()` | `gs_hle.cpp g_sceGsSyncV_count`, CSR FIELD | boundary count / emu tick & 1 |
| 4 | frame-budget anchor | `BudgetAnchorCounts()` | `main.cpp` 0x191bd0 hook frame-top snapshot ([BH4]: the game's obj+0x80) | emu T0 counts at the last boundary (the title's stored anchor VALUE is title data; this is its domain) |
| 5 | COP0 Count | `Cop0Count()` | `syscalls.cpp` mtc0/mfc0 model | emu ticks × 4915200 (EE core 294.912 MHz / 60; mtc0 reseat offset stays in `SyscallTable::HandleMtc0`) |
| 6 | RTC | `RtcEpochSeconds()`, `Rtc()` | `sce_hle` sceCdReadClock + `PS2_RTC_FIXED` | fixed 2005-06-15 12:00:00 when `game.rtc_fixed`; else epoch + emu ticks / 60 (never the host date) |
| 7 | IOP heartbeat | `IopHeartbeatBeats(usec)`, `HeartbeatTicksFor` | `turbo.h` PS2_HB_TICKCLOCK re-clocking | emu ticks / max(1, round(usec/16667)) |
| 8 | CDVD latency / refill epoch | `CdvdEpochTick()`, `CdvdBudgetKbSince()` | `ps2_vblank.h` delivery meter, `PS2_CDVD_BUDGET_KB=256` | emu tick, 256 KB per tick |
| 9 | audio | `AudioRateMasterIsDevice()` (constexpr true) | mixer rule | NOT derived — the device clock is the rate master and never enters the domain (Phase 7) |

### Tests (`tests/ee/test_timing.cpp`, one binary, three ctest names — 106 checks)

| ctest | Checks | Content |
|---|---|---|
| `EmuDomainAndRegime` | 57 | RegimeDetector: fast-window raise at exactly the 15th sample, full-window DOWN, the on_A1 no-flap pin (arrival 1 / final 2 holds n=2 for 360 boundaries), strict 36/120 vs 37/120, bounds 1..5; BoundaryAdvance: the 7 OLD pins + the 10 000-vector sweep (adv ≥ carried, adv ≤ quantum, lead ≤ 1); EmuDomain: 3-raw-tick frame → 1 emu tick with lag −2 never repaid, top-up to base+regime with lead cap 1, carry 1/2/absorb at pin 2, idle release after 6 ticks (1:1 ride, re-anchor continuous), bypass 1:1, adaptive lift to regime 2 and exact 2/frame, pin holds under 200 frames of evidence, ReAnchor; TickSource modes/clamps; TurboGovernor engage/release/input/default-off/manual; Warp |
| `BudgetClock` | 34 | thresh 976/781/585/576/390 + saturation + NaN; period 9600 / zext / divu-by-0; fold pins (0, 9600, 9601, 9678, 9679→79, 19278→9678, 19279→79); the 78-count sliver reads headroom TRUE; sltu strictness at remaining == thresh; closed == literal over the period/phase grid and 200 000 random vectors; huge-elapsed minimal k; the synthetic clock (anchor + 8800 + n×96: rem 704 < 976 not granted, base 0 granted, anchor change resets); PhaseClassifier truth table (enter after 2 low, leave on first full, glitch ignored, strict `<`, disabled, saturation) |
| `DerivedClocksPurity` | 15 | two 400-boundary runs with identical raw/boundary sequences and DIFFERENT wall stubs (16.667 ms steps vs 3.333 µs steps vs +1) yield identical streams of all eight derived clocks; a run with jittered raw-tick counts between the same boundaries (frames 1.., inside the idle window) is identical too (regime 1 absorbs host jitter into lag); counts-per-tick facts; (1)–(9) each pinned; RTC fixed civil date and unpinned +1 h 01 min by emu seconds; `EpochToCivil(946684799)`; heartbeat rounding; CDVD budget; `InstallKernelClocks` gives `KernlTimer` its clock |

### Fixes found by the tests (recorded)
- **LibcHleDiff heap overflow (pre-existing, 3C2 test bug, surfaced under `ctest -j4`):** the 400-vector loop drew
  `d`/`s` offsets up to `0x7FFF0` but the host reference vectors `ref`/`exp` are `0x20000` bytes — `std::memmove(exp
  + off, …)` wrote past the heap block. Standalone runs happened to survive; under parallel ctest load the corrupted
  heap made vector #? mismatch (failed 1/1 in the first full run, passed 6/6 standalone). The offsets are now bounded
  to the window (`% 0x1D000` bytes / `% 0x1D00` quads, so `off + n < 0x20000`). Engine `Libc` unchanged — a test
  defect only.
- `DerivedClocksPurity` RTC expectation first used tick counts off by the run's 7 bypass ticks; the check now drives
  the domain to an exact second boundary (`DomainTick() >= 60×3660+53`) instead of adding a fixed count.

### Gate status after 3D1

```
$ ./verify/run_bounded.sh 900 /tmp/p3d1/build8.log -- cmake --build --preset macos-dev -j4      # exit 0
$ ./verify/run_bounded.sh 900 /tmp/p3d1/ctest3.log -- ctest --preset macos-dev                  # exit 0
100% tests passed, 0 tests failed out of 65      (62 carried + EmuDomainAndRegime, BudgetClock, DerivedClocksPurity)
Total Test time (real) = 129.77 sec
```
`LintOneClass`: 196 files, 0 findings. `LintEnginePurity`: 187 files, 0 game tokens (the rate/period constants
576000 / 9600 / 2457600 / 153600 / 262 / 4915200 are BUSCLK-derived hardware facts and sit outside the PT_LOAD token
band; the budget hook's anchor address and the tuning constants 8800/96/48 appear only in the test and are title data
for 3E). §7 3D1 green criterion: the 2 named tests green (+1); the nine clocks each have a named accessor with its
domain in the header comment; `SetRegimePin(1)` is the deterministic profile's pinned regime (pinned in
`EmuDomainAndRegime`); lints 0.

### HANDOFF (3D1 → 3D2)

**Done:** the five p3c2r items; the eight `engine/ee/timing/` pairs; `DerivedClocks::InstallKernelClocks` (the
composition root calls it with the boundary wait hook); three suites (106 checks); the LibcHleDiff test overflow;
tree green.

**Not done / carried (honest gaps):**
- **T0 sub-tick interpolation is not written and has no wall source to write from.** OLD's `PS2_T0_SUBTICK` term read
  the wall stamp of the last emu tick; NEW's domain carries no stamp (`EmuDomain` is a pure function of raw ticks and
  boundaries). Under the deterministic profile (`time.t0_tickpaced = 1`) this is the shipped shape. For PLAY, if an
  intra-frame COUNT gradient proves necessary (a game loop that polls T0 between boundaries and expects motion), 3D2's
  `EeTimers` may add a PRESENTATION-only interpolation whose input is the raw-tick count within the frame (raw ticks
  are still not wall values) — never a host clock. Decide when the boot leg / a play leg shows a consumer.
- `test_ee_timers_timr` is mostly the TIMR savestate codec (`EncodeTimr/DecodeTimr` — 3E's `TimerCodec` section) plus
  the mode-bit facts; only the `kCountsPerTick` / CLKS facts moved here (`DerivedClocksPurity`). `test_ee_pace`
  targets `ee_pace.h` → 3D2's `BoundaryProtocol`; not ported here.
- `KernlTimer`'s wait hook: `InstallKernelClocks(table, wait)` passes the caller's hook through; 3D2's
  `BoundaryProtocol` supplies the real one (advance the tick at the boundary, never sleep). The unit test uses a stub
  that ticks the domain.
- `TurboGovernor` / `Warp` are not yet connected to `TickSource` by any owner (3D2's sequencer or 3F's root decides
  who calls `Apply` per boundary); `EmuDomain::SetBypass` must be fed `TickSource::Bypass()` by the same owner.
- `Config` wiring: `time.tick_deterministic` → `TickSource::Policy`, `engine.time_regime_pin` → `SetRegimePin`,
  `game.rtc_fixed` → `SetRtcFixed`, `engine.turbo` → `SetManual`, `cdvd.load_turbo` → `SetLoadTurbo`,
  `harness.warp_to_frame` → `Warp::SetTarget` — all plain setters, none called yet (composition root, 3F).
- `PS2_BUDGET_DIAG` / `PS2_LVL_DIAG` DIAG categories not wired (core/diag consumer when the hook lands in 3E/3F).
- `engine/ee/timing/README.md` "Belongs here" list already names these classes; "Transplanted from" unchanged.

**Next command (stage 3D2):** read `OLD/host_app/ee_timing/{tick_sync,frame_rendezvous,boundary_sequencer}.*`,
`quantum.h`, `ee_pace.h`, `timeline.h` (WAITFIX arm), `ee_timers.{h,cpp}`, `intc.{h,cpp}`, `gs_hle.cpp:405-745`,
`sif_hle.cpp:194-431`, `tests/{test_ee_timing,test_ee_pace,test_ee_timers_timr}.cpp`; write
`engine/ee/timing/{tick_sync, frame_rendezvous, boundary_sequencer, boundary_protocol}`, `engine/ee/intc/{intc,
ee_timers, timer_codec}` (register on `MmioRouter`, `EeTimers` reads `DerivedClocks::TimerCounts`),
`engine/gs/hle/sce/gs_sync` (the boundary OWNER: `sceGsSyncV` / `WaitForVblank` → `BoundarySequencer::RunBoundary`);
the sequencer's step 2 calls `TickSource::Tick()` + `EmuDomain::OnRawTick/OnBoundary` and feeds `SetBypass`; then
`cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`.

## 3D2 — boundary + INTC + EE timers + boundary owner (stage p3d2, 2026-09-26)

### p3d1r items closed first
1. `TickSource::Bypass()` = Freerun ‖ Warp only (OLD `ps2_vblank.cpp BypassNow` = freerun ‖ warp ‖ load-turbo
   freerun form). Finite-K turbo is NOT a bypass: its extra raw ticks stay under the EmuDomain quantum, so the game
   still sees REGIME fields per boundary; only the host pace changes. Stated in `tick_source.h`; test pin flipped
   (`turbo K=4 -> NOT a bypass`).
2. `TurboGovernor` engaged form → `Mode::Freerun`, K = 0 (OLD `turbo.h AutoK()` default 0 = FREERUN-engaged; finite
   K "for testing/diagnosis" only). `kAutoK` removed. FLAG_LEDGER reconciliation item 11 records the engaged-form and
   bypass rules (`PS2_LOAD_TURBO_K` has no Config field).
3. `EmuDomain::SetRegimePin(-1)` resyncs `regime_ = det_.n()` (the detector kept sampling under the pin).
4. `tick_source.h`: 16667 us = 1/60 s (a perfect 60 Hz console, not 59.94).
5. `DerivedClocks::InstallKernelClocks` LIFETIME comment (closures capture `this` + the domain reference; the
   composition root owns both for the run); `ClocksPurity` resets `KernlTimer` + `SetTickClock(nullptr)` before its
   stack objects die.
6. `ThreadManager::ExitThread` / `ExitDeleteThread` already carry `[[noreturn]]` (verified; no change).

### Files landed (one primary type per pair, global namespace)

| File | Class / content | OLD source (read-only) | Shape / flags |
|---|---|---|---|
| `engine/ee/timing/tick_sync.{h,cpp}` | `TickSync`: causes Boundary / CdvdDelivery / Demand / Deferred; `SetBody`, `FireInline` (one body per fire, concurrent-body counter), `NotePending` + `FirePending` (off-thread demands fire as Deferred at the next fixed point) | `ee_timing/tick_sync.{h,cpp}` | verbatim policy; `PS2_TICK_SYNC` SETTING → `TickSource::Policy` (deterministic = on); `PS2_FRAME_LOCKSTEP` TEST (not written) |
| `engine/ee/timing/frame_rendezvous.{h,cpp}` | `FrameRendezvous`: ticket barrier `Issue/Ack/WaitFor(ticket, running)/Wake`, no wall valve (5 s stall report, 60 s abandon = defect reports only); **Phase-3 form: tickets self-ack at Issue (`ImmediateAck()`), `ArmForPresenter()` is Phase 6's one call** | `ee_timing/frame_rendezvous.{h,cpp}` | verbatim; `PS2_PRESENT_EDGE` NORM |
| `engine/ee/timing/boundary_sequencer.{h,cpp}` | `BoundarySequencer`: the 8 fixed slots (table below), `SetStep`/`HasStep`, `RunBoundary(ctx)` (false = stopped; in-flight counter unwind-safe), `InstallPhase3Steps(deps)` wires slots 1–3 and the TickSync Boundary body; owns its `TickSync` | `ee_timing/boundary_sequencer.{h,cpp}`, `gs_hle.cpp:345-405 Ps2TickSyncBoundary` (step bodies) | verbatim order; bodies from engine pieces |
| `engine/ee/timing/boundary_protocol.{h,cpp}` | `BoundaryProtocol`: pace predicate `ShouldPark(declared, processed, lag)` + `Depth` clamp + `engine.ee_runahead` [1,4] default 2; the KernlTimer wait hook `Wait(deadline)`: deterministic = one Demand tick in-line (non-guest → deferred), Play = cv park until `NotifyTick`, bounded by one field period (WAITFIX: deadline, never a poll quantum) | `quantum.h`, `ee_pace.h` (P1 predicate), `timeline.h` WaitFixOn arm | split; `PS2_QUANTUM` NORM ON, `PS2_EE_PACE` NORM (OFF arm dropped), `PS2_WAITFIX` NORM ON, `PS2_EEFEED`/`PS2_MTVU*`/`PS2_RQ_*` NORM (no code here; Phase 5/6 consumers) |
| `engine/ee/intc/intc.{h,cpp}` | `Intc` (MmioHandler, singleton): STAT W1C / MASK toggle at `0x1000F000/F010`, 15 causes, `Raise` = `RouteIfForeign(kIntcRaise)` else `fetch_or` (the 7th routing site; `Register` installs `ThreadManager::SetIntcRaise(RaiseInPlace)`), `Dispatch(ctx, cause)` (STAT & MASK gated, handlers from `SyscallTable::IntcHandlers()`, $a0/$gp/$ra set, PC + GPR file saved/restored, ack this bit only), `DispatchPending`; `SetGuestCall` (root binds `DispatchTable::Invoke`); **MASK word bound into `SyscallTable::BindIntcMask` so `_EnableIntc` and the register are one state** | `intc.{h,cpp}` | verbatim + class |
| `engine/ee/intc/ee_timers.{h,cpp}` | `EeTimers` (MmioHandler): T0–T3 COUNT/MODE/COMP/HOLD at `0x10000000 + n*0x800`, lazy settle in the emu tick domain (`DerivedClocks::VblankCount` × `kCountsPerTick[CLKS]`), CUE hold, ZRET period, free-run wrap OVFF + compare EQUF, pending event counters, `PumpAtBoundary` / `PumpAtCountRead` (ISR through `Intc::Raise` + `Dispatch`, ≤128 events per timer per pump, re-entrancy guard), `Snapshot/Restore` (rebase to now), `SetCtxProvider` | `ee_timers.{h,cpp}` [R195-TIM0] | split (MMIO + ISR; clock math in timing); `PS2_EE_TIM0` NORM ON, `PS2_T0_SUBTICK`/`PS2_T0TRACE`/`PS2_T0_NOISR`/`PS2_TIMEDOM_DIAG` DELETE/DIAG (no wall term exists) |
| `engine/ee/intc/timer_codec.{h,cpp}` | `TimerCodec`: MODE bit constants, `ApplyModeWrite` (W1C acks), `Clks`, `ComposeClock(acc, count) = (acc << 16) | count`, nested `Regs` (the TIMR record), `Encode/Decode` (136-byte payload, count_base ≤ 0xFFFF invariant) | `ee_timers.h:1-177` | pure |
| `engine/gs/hle/sce/gs_sync.{h,cpp}` (+ `ps2r_gs_sync` lib) | `GsSync`: `Bind(sequencer, domain)`, `Register(manifest)` (libgraph `sceGsSyncV`), `SyncV` (count++, `RunBoundary`, `g_vsync_frame` = boundary count, v0 = field parity), `WaitForVblank` mirror, `SyncPath` (idle, v0 = 0); **owns the row-15 seam symbols** (`hle_sceGsSyncV`, `hle_sceGsSyncPath`, `g_sceGsSyncV_count`, `g_vsync_frame`) — removed from `ee/cpu/seams.cpp` | `gs_hle.cpp:405-745`, `sif_hle.cpp:194-431` (boundary halves) | split |
| `engine/ee/hle/syscall_table.{h,cpp}` | `BindIntcMask(uint32_t*)`; `_EnableIntc/_DisableIntc` toggle the bound word; `ResetForTest` clears it | — | — |
| CMake | 7 new TUs in `ps2r_ee`; `ps2r_gs_sync` (engine/gs); `test_dispatch_table` + `test_boundary` link it | — | — |

### The boundary slot table (fixed; `BoundarySequencer::Order`)

| Slot | Step | Owner | Phase-3 content |
|---|---|---|---|
| 1 | PresentRendezvous | FrameRendezvous | `Issue()` + `WaitFor(ticket, running)` — immediate ack (no presenter); Phase 6 `ArmForPresenter` + VSync push |
| 2 | TickAdvance | TickSync / EmuDomain / TickSource / Alarms / EeTimers / Intc | `SetBypass(source.Bypass())`, `NoteArrival`, ONE Boundary tick body (`Tick` → `OnRawTick` → INTC VBON/TIM2/VBOF), `FirePending` (deferred demands), `OnBoundary` settle, `Alarms::FireDue(DomainTick)`, `EeTimers::PumpAtBoundary` (TIMn ISRs), `Intc::DispatchPending` (the tick's ISRs) |
| 3 | EventDrain | EeEventQueue | `DrainIfPendingOnHolder()` — foreign wakes (incl. queued INTC raises) land here in FIFO order |
| 4 | MaintDuties | Phase 4 | EMPTY hook |
| 5 | IopDispatch | Phase 4 | EMPTY hook |
| 6 | EeStreamRpc | Phase 4/7 | EMPTY hook |
| 7 | Savestate | 3E | EMPTY hook (a LOAD returns false → sequence stops before 8) |
| 8 | Verify | 3F | EMPTY hook |

### Tests (`tests/ee/test_boundary.cpp`, one binary, three ctest names — 89 checks)

| ctest | Checks | Content |
|---|---|---|
| `TickSyncBoundaryOrder` | 45 | order of the 8 slots + names; Phase 3 fills 1–3, 4/5/6/7/8 empty; recorder visits all 8 in order, a stop at 7 skips 8, in-flight → 0; one tick body per boundary (101 boundaries → 101 raw/emu ticks at pin 1); two deferred demands fire at the next tick step and stay under the pinned quantum; holder raises land in STAT directly; rendezvous immediate-ack form + the presenter form (blocks until ack, later ack releases earlier ticket, monotonic, shutdown releases false, 0 stalls); **INTC raise from a foreign thread is queued (STAT untouched on the producer), applied at the boundary's drain in FIFO order, dispatched in ascending cause order at the next tick step with per-cause ack; masked causes stay pending**; STAT W1C, MASK toggle == the syscall's word; `UnhandledPageCount() == 0` (INTC + timers displaced the sink); **two identical 60-boundary sequences (T0 free-run OVFE, T1 HBLANK ZRET CMPE, T3 CMPE, a mid-run COUNT rebase) → identical T0..T3 / STAT / dispatch / tick streams**; GsSync field parity + counters |
| `BoundaryProtocolPace` | 15 | `ShouldPark` == declared > processed + lag for lag 1/2/4 over a 50×60 grid; dead-runahead (injected presents never release); depth clamp; runahead default/clamps; a timer wait demands exactly one field; exhausted quantum absorbs ≤ idle window then terminates; a 5-field `KernlTimer` wait completes in ≤ 5 + 6 demands through the hook; non-guest demand deferred then fired |
| `EeTimersTimr` | 29 | idle registers; 6 ticks → 57600, 7 → wrap with OVFF + TIM0 ISR pumped at the COUNT read and acked; MODE W1C; CUE hold; COUNT rebase; ZRET (524 mod 500 = 24) + EQUF + TIM1; free-run COMP crossing + CLKS=0 counts; masked TIM2 raised not dispatched; snapshot/restore rebase; codec round trip / truncated / oversize / empty / count_base invariant; 0xe82 facts; `ApplyModeWrite`; `ComposeClock` |

### Fixes found by the tests (recorded)
- A deferred (demand) tick under a PINNED regime is absorbed by the quantum (raw 104 / emu 102 after two demands at
  boundary 102): correct by the field-locked rule; the test expectation was corrected, not the domain.
- `SyscallTable::AddHandler` rejects `pc == 0` (kernel rule) — the test's first handler encoding used the cause as pc.

### Gate status after 3D2

```
$ ./verify/run_bounded.sh 900 /tmp/p3d2/build7.log -- cmake --build --preset macos-dev -j4      # exit 0
$ ./verify/run_bounded.sh 900 /tmp/p3d2/ctest2.log -- ctest --preset macos-dev                  # exit 0
100% tests passed, 0 tests failed out of 68      (65 carried + TickSyncBoundaryOrder, BoundaryProtocolPace, EeTimersTimr)
Total Test time (real) = 134.49 sec
```
`LintOneClass`: 213 files, 0 findings. `LintEnginePurity`: 203 files, 0 game tokens. §7 3D2 green criterion: 3 tests
green; `UnhandledPageCount()==0` with INTC + timers registered (pinned); slots 4/5/6 exist and are empty (pinned); one
tick per boundary (pinned); lints 0.

### HANDOFF (3D2 → 3E)

**Done:** the six p3d1r notes; 7 engine pairs + `GsSync` (+ `ps2r_gs_sync`); seams.cpp row 15 retired; 3 suites (89
checks); tree green.

**Not done / carried (honest gaps):**
- **Direct sema 1/3 HLE of the tick body is NOT transplanted.** OLD `FireVblankTickBody` called `iSignalSema(1)` /
  `iSignalSema(3)` directly (game semaphore ids = title constants) instead of running the game's INTC handlers. NEW
  raises VBON/TIM2/VBOF and DISPATCHES the registered handlers (slot 2), so the game's own VBON handler signals its
  semaphore. If the boot leg (3F) shows the game never registers a VBON handler before waiting, add a title-side
  workaround row (`hle/workarounds/`) that registers the signal — never in `engine/`.
- **GS_CSR.VSINT at the tick** is not set: the MMIO sink has no CSR hook. Phase 6's GS-priv register owner sets it from
  slot 2 (register a step body or a tick observer). Note for 3F: a boot leg that polls CSR.VSINT parks.
- `GsSync::Register` (libgraph `sceGsSyncV`) is NOT in `RegisterBuiltins` (that would change the pinned 68-engine-
  handler count); the composition root (3F) calls it after `RegisterBuiltins` and updates `SdkBindingManifestResolves`
  to 69. The generated-TU seam `hle_sceGsSyncV` is live regardless.
- `EeTimers::PumpAtCountRead` needs `SetCtxProvider` (root: `ThreadManager::Instance().Current()->ctx`); unset =
  no ISR at reads (boundary pump still runs).
- Savestate TIMR/INTC sections (3E): `EeTimers::Snapshot/Restore` + `TimerCodec::Encode/Decode` are the providers'
  inputs; `Intc` STAT/MASK + `SyscallTable::IntcHandlers()` form the INTC section. Restore order: `EmuDomain::ReAnchor`
  → `EeTimers::Restore` (OLD `Restore` order preserved by the caller).
- Play-policy pacer (`TickSource::Policy::Play`) has no owner yet. Rule already fixed by the classes: the pacer is a
  non-guest thread, so it calls `TickSync::NotePending` + `BoundaryProtocol::NotifyTick`; the tick BODY still fires
  only on the EE (slot 2 / a guest demand). Wire it in Phase 6/7 when a pacer exists.
- `TurboGovernor::Apply` / `Warp::Apply` are still uncalled (3F root, per boundary before slot 2).
- No TIM2 model change: the tick raises INTC TIM2 every field (OLD's observable behaviour) in addition to `EeTimers`'
  T2 events; if the game programs T2 as a real timer the double source shows up as extra TIM2 ISRs — adjudicate on
  the boot leg.
- `docs/phases/PHASE3_PLAN.md` §2.6 says `PumpAtBoundary, PumpAtCountRead` — implemented; §2.7 `ShutdownLadder`
  (S29 unwind) NOT written here (needs the emulator-running flag + `ThreadExitUnwind`; 3F root).

**Next command (stage 3E):** read `OLD/host_app/hle_meta_arena.{h,cpp}`, `main.cpp:3095-3108`, `memory.cpp:3149-3221`,
`savestate.cpp` (providers), `savestate_format.*`, `docs/SAVESTATE_SCHEMA.md`, `verify/memory_checkpointer.cpp`,
`runtime-environment.md`, `manual_switch_dispatchers.cpp` (install list); write `games/<t>/{title_profile, hle/budget,
hle/loader_sync, hle/workarounds/{meta_arena, key_redirect, README}, savestate/{ee_sections, thread_sections,
kernel_sections}}` (TIMR via `TimerCodec`, INTC via `Intc` + `SyscallTable::IntcHandlers()`), `verify/state_hash.py`,
`restore.mhash`; then `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`.

## 3E — workarounds quarantine + title profile + savestate restore + restore.mhash (stage p3e, 2026-09-27)

### p3d2r notes closed first
1. `test_boundary` `main` rejects an unknown section name (exit 2 + usage) instead of silently running nothing.
2. The foreign-raise test restores `ThreadManager::InstallEventApplier()` after swapping in its model, then proves the
   production path once end-to-end: foreign `Raise(kVif0)` queued (STAT untouched) → boundary drain → ThreadManager's
   applier → `SetIntcRaise` → `RaiseInPlace` on the REGISTERED controller → STAT bit set.
3. `GsSync::Bind(sequencer, const DerivedClocks*)`: v0 = `DerivedClocks::SyncVFieldParity()`, `g_vsync_frame` =
   `FrameBoundaryCount()` (clock 3 accessors; nothing re-derived from the domain).
4. `Intc::Dispatch` sign-extends `$a0/$gp/$ra` into the 64-bit GPRs (`SignExtend32`; MIPS lw/addiu semantics).
5. `PHASE3_PLAN.md` §7.1 row `P3-TIMPUMP-SUPERSET`: NEW pumps every dispatchable timer at ANY Tn COUNT read (OLD: T0 only).
6. `Intc::Register` binds `this` (file-static `g_registered`; the applier no longer routes to `Intc::Instance()`).

### Files landed (one primary type per pair; lints 0)

| File | Class / content | OLD source (read-only) | Shape |
|---|---|---|---|
| `verify/state_hash.py` | PS2SAVE1 envelope parser (magic, version 1–4, per-section CRC32, trailer CRC, truncation) + FNV-1a64 over RAM0→SPAD→META raw payloads; TSV line per anchor | `savestate_format.{h,cpp}`, `verify/memory_checkpointer.cpp` order | oracle script |
| `games/<t>/verify/restore_mhash_expected.tsv` | the 11 anchor hashes from `state_hash.py` (all v4 files; 32 MB / 16 KB / 2 MB) | OLD `save_states/*.ps2st` | frozen table |
| `games/<t>/hle/workarounds/meta_arena.{h,cpp}` | `MetaArena` (HOOK §3.1 row 0x181560): OWNS the 2 MB backing, `Register(AddressMap&)` → extra region `[0x02000000,+2 MB)` named META; `Alloc` (FIFO quarantine > 256, bump, reuse), `Free` (tombstone +0x10/+0x14/+0x18, clear +0x1c below bit 18), `RoutesAllocation(size, ra)` / `Contains`, HEAP-tail codec `SerializeState/ValidateState/RestoreState` (OLD layout, via `PayloadReader`) | `hle_meta_arena.{h,cpp}`, `main.cpp:3197-3255` (predicate) | quarantined; logging (DIAG) dropped |
| `engine/ee/savestate/memory_sections.{h,cpp}` | `MemorySections`: RAM0/SPAD/META `SectionSpec`s (serialize = raw bytes, validate = exact size, restore = memcpy) + `Spans()` in MemoryCheckpointer order + `Hash()` | `savestate.cpp:1098-1100, 1578-1580, 1722-1724` | engine provider (no addresses) |
| `engine/ee/savestate/state_restorer.{h,cpp}` | `StateRestorer`: `RestoreImage/RestoreFile` (parse → required-present check → `pre_restore_` step (ReAnchor/teardown slot) → `SectionRegistry::Restore` over the OWNED tags, later-phase tags skipped), `SaveImage` (Capture + lowest covering version), `RestoredMemoryHash`, `LastSections/LastMeta` | `savestate.cpp DoLoad/DoSave` order | orchestrator (memory bound; other phases register their specs) |
| `games/<t>/hle/title_profile.{h,cpp}` | full form: `quirks{xgkick_defer=true, round_sprite_mode=1, budget{anchor=0 (DerivedClocks::BudgetAnchorCounts), base 8800, base_low 0, step 96, lowdraw 48}}`, `save{schema_version=4, anchor_dir, expected_hashes, manual_body_ledger}`, `recomp{shard_count=32}` | `runtime-environment.md`; the four budget defaults are OLD `main.cpp` fallbacks: base 8800 (:3540), step 96 (:3515), lowdraw 48 (:3575), base_low 0 (:3581) (`budget_math.h:115` is only a comment) | data |
| `games/<t>/verify/manual_body_ledger.tsv` | 243 rows {address, old_file, row_class, first_leg=-, verdict=open, retirement_gate}: 235 `manual_switch_dispatchers.cpp` installs (3 hand-written + 232 restitched) + 8 `anim_pose_hle.cpp` mirrors beyond them (0x00294480 in both) | `manual_switch_dispatchers.cpp` install list, `anim_pose_hle.cpp:210` table | ledger |
| `games/<t>/hle/workarounds/README.md` | 7-row ledger (address, HOOK row, defect, phase, file, status, retirement gate) | HOOK §2/§3 | ledger |
| `tests/ee/test_restore_mhash.cpp` (+ CMake) | ctest `RestoreMhashAnchors` (SKIP 77 when the anchor dir is absent) | — | oracle test |
| CMake | `ps2r_ee` += 2 savestate TUs; `ps2r_title_hle` += `meta_arena.cpp`; `test_restore_mhash` links both | — | — |

### restore.mhash — the 11-leg table (G3; `RestoreMhashAnchors` output, 2026-09-27)

| anchor | restore.mhash (NEW, MemoryCheckpointer over the restored image) | state_hash.py (file payloads) | re-serialize |
|---|---|---|---|
| artdet_f37152 | 0x8de41106ac4dad95 | 0x8de41106ac4dad95 | identical |
| ps2state_slot0_f36935 | 0xb8e09d48ba19fc73 | 0xb8e09d48ba19fc73 | identical |
| ps2state_slot0_f39807 | 0x277121d6d0d6a983 | 0x277121d6d0d6a983 | identical |
| ps2state_slot0_f40391 | 0x06c41ff2a4a6c009 | 0x06c41ff2a4a6c009 | identical |
| ps2state_slot0_f44111 | 0x5403edb668ca8e60 | 0x5403edb668ca8e60 | identical |
| ps2state_slot0_f46203_red_crystal | 0x7f2b0d91a8ec07b0 | 0x7f2b0d91a8ec07b0 | identical |
| ps2state_slot0_f46949_yellow_crystal | 0x334679dbee909b7b | 0x334679dbee909b7b | identical |
| ps2state_slot0_f57292 | 0xfc8deca68213dc9c | 0xfc8deca68213dc9c | identical |
| ps2state_slot0_f59991 | 0x9d674482db08223c | 0x9d674482db08223c | identical |
| slowspot_f37092_s1789428268 | 0xd5ff7161f930509e | 0xd5ff7161f930509e | identical |
| slowspot_f40298_s1789693059 | 0x6f74f07f86f2ef4a | 0x6f74f07f86f2ef4a | identical |

`restore.mhash: 11/11 anchors match` — the test also requires NEW's hash == FNV over the file's own RAM0/SPAD/META
payloads (third column of its output) and the re-serialized memory sections byte-identical to the file payloads.

### Gate status after 3E
```
$ ./verify/run_bounded.sh 900 /tmp/p3e/build4.log -- cmake --build --preset macos-dev -j4      # exit 0
$ ./verify/run_bounded.sh 900 /tmp/p3e/ctest4.log -- ctest --preset macos-dev                  # exit 0
100% tests passed, 0 tests failed out of 69      (68 carried + RestoreMhashAnchors)
Total Test time (real) = 143.83 sec
```
`LintOneClass` 0 findings, `LintEnginePurity` 0 game tokens (workarounds live title-side; `engine/ee/savestate` has no
addresses). §7 3E green criterion: G3 11/11 ✔; ledger files exist (7 rows README + 243-row TSV) ✔; lints 0 ✔.

### HANDOFF (3E → 3E′) — superseded by the 3E′ section below (p3er review: NEEDS_CHANGES on scope; the six items are closed there).

## 3E′ — savestate providers + workaround bodies + validate-first restore (stage p3e loop 2, 2026-09-27)

### p3er items closed (all six)

1. **Section providers written and registered** (engine-side — the layouts are the file format, address-free):

| File | Class | Wire layout (OLD source, read-only) | Notes |
|---|---|---|---|
| `engine/ee/savestate/thread_section.{h,cpp}` | `ThreadSection` | THRD: `u32 count` + per thread the packed 80-byte `ThreadRecHdr` (`savestate.cpp:547-560`) + `[CpuContext if has_ctx]` + `chain_depth × {func_addr, ra}` (`:562-583`) | `Parse` rejects `ctx_size != sizeof(CpuContext)` (the 3A pin), `count > 256`, `chain_depth > 128`, truncation, trailing bytes. `Restore` installs the records into `ThreadManager` (info only) and keeps every parked context + chain in FILE ORDER for the relaunch step (`RestoredFor(tid)`); `Serialize` emits restored records first in that order, so a restored image re-serializes byte-identical (OLD iterated an `unordered_map`, so the file order is arbitrary and must be preserved, not sorted). |
| `engine/ee/savestate/kernel_sections.{h,cpp}` | `KernelSections` | SEMA `u32 count` + `{sid, count, maxCount, initCount, attr, option}` (`:1132-1146`); EVFL `u32 count` + `{eid, attr, option, initBits, bits}` (`:1148-1161`) | same file-order preservation; wait queues are not in the format (capture admissibility: every guest thread parked outside an object wait). **Alarms have no section** — OLD never persisted its alarm table (no TAG; `grep -i alarm savestate.cpp` = none), so "kernel objects" = SEMA + EVFL. |
| `engine/ee/savestate/timer_section.{h,cpp}` | `TimerSection` | TIMR = `TimerCodec::Encode/Decode` (4 × 34 bytes), OPTIONAL (`:1215-1220`, `:1926-1940`) | restore = `EeTimers::Restore` (rebases `baseline_tick` to the domain's now, exactly as OLD restored TIMR after CNTR); every other field round-trips exactly (the anchor test checks this modulo the rebase). |
| `engine/ee/savestate/intc_section.{h,cpp}` | `IntcSection` | INTC `stat, mask, nextId, n × {id, cause, pc, gp, arg, flag, active:u8}` (`:1164-1183`, restore `:1796-1810`) | records emitted grouped by cause ascending, stable within a cause (OLD kept a `std::map<cause, vector>`) — also the dispatch order. |

   Registration order on `StateRestorer::Registry()` (the composition root / tests): memory (RAM0/SPAD/META) → THRD → SEMA → EVFL → TIMR → INTC. OLD's literal DoLoad order was SEMA/EVFL → thread table → INTC → … → TIMR (after CNTR); the tables are independent (no cross-reads at restore), so the plan's order is used and this difference is immaterial — recorded here rather than silently.

   Engine seams added for the providers (no behaviour change for live code): `ThreadManager::SnapshotTable/InstallTable` (+ `ThreadInfo::heap_base/heap_end`, the per-thread heap window OLD kept in `ThreadInfo` — pass-through fields for round-trip fidelity; `KernlBoot` still owns the live heap base), `Semaphores::SnapshotTable/InstallTable`, `EventFlags::SnapshotTable/InstallTable`, `Intc::RestoreRegisters(stat, mask)`, `SyscallTable::InstallIntcHandlers(records, next_id)` + `NextIntcHandlerId()`. **Fidelity fix found while wiring INTC:** `SyscallTable` used ONE id counter for INTC and DMAC handlers; OLD had separate counters (`g_nextIntcId` `intc.cpp:13`, `g_next_dmac_handler_id` `dmac.cpp:112`, both starting at 1) and the INTC section carries the INTC one. Split into `next_intc_id_` / `next_dmac_id_` (the ids the game receives from `AddIntcHandler`/`AddDmacHandler` now match OLD).

   `RestoreMhashAnchors` extended: all five providers registered (`EeTimers` over a `DerivedClocks(EmuDomain)` rig + a fresh `Intc`); per anchor it now also requires every THRD record parsed with `ctx_size == sizeof(CpuContext) == 1600` (the provider rejects otherwise; the printed record count equals the installed table size), EVERY owned section re-serialized byte-identical (TIMR modulo the baseline rebase, all other fields exact), and `pre_restore_` run exactly once per accepted file.

2. **`hle/workarounds/free_list_0x181560.{h,cpp}`** (namespace `free_list_0x181560`, free-function group per lint rule 5): `AllocHook(arena, orig)` — `FUN_00181560(size)` with `MetaArena::RoutesAllocation(size, ra & 0x1fffffff)` and a free cell → `v0 = payload`, `pc = $ra`, generated body skipped; exhausted/unrouted → `orig`. `FreeHook(arena, orig)` — arena pointer → `arena.Free`, `pc = $ra`; else `orig`. `InstallHooks(arena, capture, install)` binds exactly `MetaArena::kAllocSite`/`kFreeSite` through the injected install primitive (the manifest's address-keyed TITLE lane: `DispatchTable::Install` in the root, a recording lambda in the test). OLD `main.cpp:3197-3250` (the `[META-ARENA] EXHAUSTED` log and the `[META-FREE-AUDIT]` counter were DIAG → dropped).

3. **`hle/workarounds/key_redirect.{h,cpp}`** (`KeyRedirect`, HOOK §3.8; OLD `memory.cpp:3505-3590` — the reviewer's `:3149-3221` range is the `[MGR18-WATCH]` diagnostic; the mutator is the `s_kr_tick` block at `:3508`): `Scan()` = the repair (read `*0x30bb00` global beach pointer and `*0x309508` object; once the key at `obj+0x500` has been seen naming the beach string, a key that is neither the global pointer nor any `levels\` path is rewritten to the global POINTER — string bytes never touched); `NoteWrite()` = the every-256th-write cadence. **Decision recorded:** `FastMem` gained no observer — the engine's write fast path stays a plain store (a per-write indirect call for a title workaround is a measurable regression on the hottest path and an engine-side dependency on a title row). The root decides the binding in 3F: OLD's cadence (every guest write) or the boundary; `Scan()` is exact regardless of cadence, and the level leg's `mem.mhash` is the arbiter.

4. **Validate-before-pre_restore:** `SectionRegistry::Validate()` split out (presence/ownership + every spec's `validate`); `Restore()` = `Validate()` + restore pass. `StateRestorer::RestoreImage` now runs `registry_.Validate(owned)` BEFORE `pre_restore_`, so once `EmuDomain::ReAnchor` / thread teardown is bound, a size-mismatched section is rejected before anything is disturbed. Proven by `SavestateValidateBeforePreRestore`: a THRD record with `ctx_size = 1601` is rejected with "CpuContext size mismatch" and `pre_restore_` runs 0 times; the intact image then restores with `pre_restore_` = 1.

5. **Budget caveat closed:** `title_profile.cpp` cites the OLD `main.cpp` fallbacks (base 8800 `:3540`, step 96 `:3515`, lowdraw 48 `:3575`, base_low 0 `:3581`); the 3E files-table row above is corrected; the "re-check in 3F" handoff item is gone.

6. **MetaArena HEAP-tail codec proof:** `SavestateMetaArenaCodec` — 300 allocs + 150 frees, `SerializeState → ValidateState → RestoreState → SerializeState` byte-identical, counters equal, truncated tail rejected. HEAP registration itself stays with 3F/Phase-4 (HLEHeap half first, OLD `:1274-1280`).

### Tests added (`tests/ee/test_savestate_sections.cpp`, one binary, five ctest names)
`SavestateMetaArenaCodec`, `SavestateValidateBeforePreRestore`, `SavestateSectionsRoundTrip` (synthetic THRD/SEMA/EVFL/INTC tables: `SaveImage → RestoreImage → SaveImage` byte-identical; values land in the live tables; INTC grouped by cause; id allocation continues above restored ids), `WorkaroundFreeListHook`, `WorkaroundKeyRedirect`.

### restore.mhash — 11/11 with every Phase-3 provider live (`RestoreMhashAnchors`, 2026-09-27)

Every anchor: `restore.mhash (NEW) == state_hash.py == payload FNV` (the eleven hashes are unchanged from the 3E table above), memory sections re-serialize identical, `THRD 2 records ctx_size==1600, 8 sections round-trip (TIMR modulo the baseline rebase)`, and the last line:
```
restore.mhash: 11/11 anchors match (pre_restore ran 11 times, once per accepted file)
```

### Gate status after 3E′
```
$ ./verify/run_bounded.sh 900 /tmp/p3e2/build7.log -- cmake --build --preset macos-dev -j4      # exit 0
$ ./verify/run_bounded.sh 900 /tmp/p3e2/ctest2.log -- ctest --preset macos-dev                  # exit 0
100% tests passed, 0 tests failed out of 74      (69 carried + the 5 SavestateSections/Workaround names)
Total Test time (real) = 144.16 sec
```
`LintOneClass` 0 findings (233 files), `LintEnginePurity` 0 game tokens (216 files): the providers in `engine/ee/savestate` carry no addresses; the two workaround bodies and both hook addresses live title-side. `RestoreMhashAnchors` passed inside the run with every provider registered (11/11).

### HANDOFF (3E′ → 3F)

**Done:** items 1-6 above; providers registered and proven over the 11 anchors; workaround bodies written with tests; validate-first restore; budget citations; codec proof; tree green.

**Not done / carried:**
- 3F root wiring: register the providers in the order above on the root's `StateRestorer`; bind `pre_restore_` = `EmuDomain::ReAnchor` + guest-thread teardown; relaunch workers from `ThreadSection::RestoredFor(tid)` (context + chain; `is_boundary` names the boundary thread); call `free_list_0x181560::InstallHooks(arena, DispatchTable capture, DispatchTable::Install)`; decide the `KeyRedirect::NoteWrite` binding (write path vs boundary; arbiter = level-leg `mem.mhash`).
- `VerifyHost` `restore.mhash` line / `run.meta park=` keys; `battery.py --restore-only`.
- HEAP section: `HLEHeap` half (3F/Phase 4) first, then `MetaArena::SerializeState` as the tail.
- `hle/budget.{h,cpp}` / `hle/loader_sync.{h,cpp}` data pairs (params live in the profile).
- Threads created live serialize with `has_ctx = 0` until the park-capture path exists (later phase); SEMA/EVFL waiter lists are never serialized (admissibility rule).

**Next command (stage 3F):** the boot leg per PHASE3_PLAN §4.4 with the root wiring above; `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`.

## 3F — composition root + boot leg + Phase 3 gate (stage p3f, 2026-09-27)

### p3er notes closed first
1. **Section order** = the original's section order — memory (RAM0/SPAD/META) → THRD → SEMA → EVFL → INTC → TIMR (DoSave
   `savestate.cpp:1123-1215` emits THRD, SEMA, EVFL, INTC … TIMR; DoLoad `:1746-1926` restores SEMA/EVFL, threads, INTC,
   then TIMR last after CNTR). Applied in the test rig (`test_restore_mhash.cpp`) and the root (`ee_bootstrap.cpp`);
   `state_restorer.h` steps 6/7 swapped. The 3E′ sentence "memory → THRD → SEMA → EVFL → TIMR → INTC … the plan's order is
   used" is superseded by this one: NEW now registers in the original's order, so no order difference remains to record.
2. **SEMA/EVFL/INTC presence — stricter policy recorded, kept `required=true`.** OLD `Find()`'d the three tags and
   dereferenced the result unchecked (`:1746`, `:1761`, `:1796`); only THRD (`:1523`) and HEAP (`:1941`) had presence tests.
   "Optional" in OLD therefore meant "undefined behaviour when absent", and every OLD writer emitted all three; NEW rejects
   such a file with a named error instead. Comment at the specs (`kernel_sections.cpp`, `intc_section.cpp`).
3. **Absent TIMR → boot seed** as OLD `:1936-1939` / `ee_timers.cpp:432-448`: `SectionSpec::absent` hook (core registry,
   runs during `Restore` for an optional registered tag the file lacks) → `TimerSection::SeedBootDefault()` →
   `EeTimers::SeedBootDefault(t0_mode)`: all four timers reset, baselines at the current tick, `T0_MODE = t0_mode & ~(EQUF|OVFF)`.
   The value `0x0e82` (FUN_001919c8's boot init) is title data: `TitleProfile::quirks.boot_t0_mode`, passed into
   `TimerSection(EeTimers&, uint16_t)`; the engine carries no game constant.
4. **KeyRedirect beach bound 24** (`memory.cpp:3535` `p + 24 >= size`): `kBeachTailBound = 24` replaces `sizeof(kBeachKey)` (23);
   the levels-prefix bound stays `sizeof("levels\\") = 8` (OLD `:3550`).

### Files landed (one primary type per pair; lints 0)

| File | Class / content | OLD source (read-only) |
|---|---|---|
| `app/host/ee_bootstrap.{h,cpp}` | `EeBootstrap::Run(cfg, host, title, paths)` — the Phase-3 composition root in the §3E′ order: MainMemory/Scratchpad/AddressMap (+MetaArena region) → MmioSink/MmioRouter/FastMem → `RecompAbi::BindMemory` → Intc + EeTimers registered (displacing the sink) → EmuDomain (regime pin 1 under `time.tick_deterministic`) / DerivedClocks (`game.rtc_fixed`) / TickSource (Deterministic policy, Freerun when headless) / FrameRendezvous / BoundarySequencer (`InstallPhase3Steps` with Alarms, EeTimers, EeEventQueue) / BoundaryProtocol (`engine.ee_runahead`) / `GsSync::Bind` → SyscallTable seams (guest call = `DispatchTable::Invoke`, tick clock = `EmuDomain::DomainTick`, INTC mask word bound, trace) + Intc guest call + ThreadManager event applier + guest body → ElfLoader (entry checked against the profile) + DataMapApplier (data map, then post-load patches) → BootEnv configure + pre-seed → `DispatchTable::Initialize` → SdkBindingManifest (builtins + GsSync, title TSV, `Install` with `DispatchTable::Install`/`Capture`) → `free_list_0x181560::InstallHooks` → savestate providers (order above; `pre_restore_` = `EmuDomain::ReAnchor`) → GuestPark sink → the leg (cold boot: tid 1 adopted at priority 0, `CpuContextInit::Reset(entry, 0x01FFFF00)`, run token, `entry(ctx, entry)`; `--load-state`: `StateRestorer::RestoreFile`, `restore.mhash` file + `restore_mhash`/`restore_mhash_ok`/`restore_thrd_records` keys) | `main.cpp:5225-5300` (order), `main.cpp:529-600` (context) |
| `app/host/leg_closer.{h,cpp}` | `LegCloser::Close(park, pc, tid, detail)` — once: `park`, `park_pc`, `park_tid`, `park_detail`, `unhandled_mmio` (+ `unhandled_mmio_pages`), `syscalls_unknown`, `syscalls_not_implemented` into run.meta; `VerifyHost::Finalize` (finalized=1, four lane files); ring dump + log flush. The GuestPark sink adds `syscalls_total`/`syscalls_first` and `_Exit(0)`: a park is the leg's result | — |
| `app/host/main.cpp` (207 lines) | runs `EeBootstrap::Run` when `<paths.game_files>/example_title.rep/<elf_path>` exists, else the Phase-1 idle stub (recorded Warn); `PS2R_TITLE_DIR` compile definition names `games/<t>/` for the data map / manifest / expected hashes | — |
| `engine/ee/cpu/dispatch_table.{h,cpp}` | `DispatchTable::Capture(addr)` — the body currently dispatched (the SdkWrap "original" and the free-list hook capture) | — |
| `engine/ee/hle/syscall_table.{h,cpp}` | `SetTrace(fn)` observer (number + label per dispatch) | — |
| `engine/ee/hle/kernl_sif_rpc.cpp` | `Park` records the CALLER (`$ra`) as `park_pc`: the generated code materialises `ctx.pc` only at jumps, so inside a bound row `pc` is stale (the cold boot showed `0x100008`, the entry); `KernlParksTickAndSif` updated | — |
| `engine/core/savestate/section_registry.{h,cpp}` | `SectionSpec::absent` (item 3) | — |
| `engine/ee/intc/ee_timers.{h,cpp}`, `engine/ee/savestate/timer_section.{h,cpp}`, `games/<t>/hle/title_profile.{h,cpp}` | item 3 | `ee_timers.cpp:432-448` |
| `engine/core/config/{config.h,config.cpp,config_loader.cpp}`, `tools/gen_settings_table.py`, `docs/filemap/settings_table.tsv` (60 rows), `docs/phases/PHASE1_PLAN.md` §3 row | `paths.game_files` (default `game_files`; `--game-files`; excluded from the fingerprint like every `paths.*` key) — the `GAME_FILES_DIR` SETTING row's runtime reader | `CMakeLists.txt GAME_FILES_DIR` |
| `app/host/CMakeLists.txt` | links `ps2r_title_hle ps2r_title_recomp ps2r_gs_sync` — **the `ps2r_title_recomp` link into `nativeee` succeeded first time** (binary 166.7 MB; no common-TU change needed; the 4.83 GB `-O0` RSS follow-up from 3B stays a build-time-only item) | — |
| `docs/bugs/P4FIX-F1-callback-slot-zero.md` | W1/W2/W3 findings (below) | — |

### The boot leg (G4) — `nativeee --profile deterministic --headless --harness-out D --frames 1500 --game-files <OLD>/game_files`
(the exact CLI `verify/run_leg.py --tree new` composes, run through `verify/run_bounded.sh 120`; run_leg.py itself does not
yet pass `--game-files` nor accept `frames=0` for a park leg — handoff item.) Exit 0; `run.meta`:
```
frames=0
finalized=1
sdk_rows_installed=283
sdk_rows_not_fn_start=12
syscalls_total=6
syscalls_first=InitMainThread(60),InitHeap(61),FlushCache(100),GetThreadId(47),WaitSema(68),GetThreadId(47)
park=sif_rpc_absent
park_pc=0x2cdadc
park_tid=1
park_detail=
unhandled_mmio=1
unhandled_mmio_pages=0x10008000
syscalls_unknown=0
syscalls_not_implemented=0
```
Four lane files present (`frames.fhash`, `audio.ahash`, `mem.mhash` legitimately empty — no boundary before the IOP
handshake; `run.meta`). **3 reps identical** (run.meta minus `wall_ms`/`build_id`: md5 `559b24c2caf6dcd3878ee934f7448589` ×3).
The park is the plan's §1 step 3: the first `sceSif*` row reached from the caller at `0x2cdadc` (libkernl area) on tid 1,
after crt0 → `InitMainThread`/`InitHeap`/`FlushCache` and the game's first thread-id / semaphore calls. One sink page was
touched: `0x10008000` (DMAC channel registers — R17 idle values, Phase 5's range).

Observations recorded, not tolerated silently:
- **P3-MANIFEST-12:** 12 of the 295 manifest rows name addresses that are not function starts of the pure-discovery emission
  (`_sceMpegWaitIpuIdle@0x2be580, _sceMpegFlushBuf@0x2be9e0, _copyAddRefImage@0x2beb78, _copyRefImage@0x2bec68, adddate@0x2c6290,
  subdate@0x2c6348, AdjustTime@0x2c6448, iWakeupThread@0x2cffd0, SetT2@0x2d53b0, iSetTimerAlarm@0x2d6168, sceSifInitRebootNotify@0x2d6258,
  InitTLB@0x2d65d8`) — `DispatchTable::Install` rejects them, they execute as part of their enclosing generated function.
  `SyscallTableCoverage`'s `Manifest` check installed through an always-true recorder, so it did not see this. Recorded in
  run.meta (`sdk_rows_not_fn_start*`); disposition for 3F′/Phase 4: either the discovery adds these 12 starts (regen) or the
  rows are marked `no-hook` in the TSV with the reason; `SetT2`/`InitTLB` are syscall wrappers whose `syscall` still reaches the
  kernel table, so nothing is lost on the boot path.
- **P3-BOOT-WAITSEMA:** `WaitSema(68)` is dispatched before any `CreateSema` (sema ids start at 1); the kernel returns the
  thbase error code and the boot proceeds to the SIF park. Compare against the OLD boot trace in 3F′ before calling it a
  defect (the game may call its own semaphore wrapper with a static id).

### Anchor legs (G3 in the binary; G4's restore half) — `--load-state <anchor> --frames 0`, same profile, bounded 120 s

| anchor | restore_mhash (run.meta) == restore.mhash file | restore_mhash_ok | THRD records | park |
|---|---|---|---|---|
| artdet_f37152 | 0x8de41106ac4dad95 | 1 | 2 | none (`relaunch=deferred`) |
| ps2state_slot0_f36935 | 0xb8e09d48ba19fc73 | 1 | 2 | none (`relaunch=deferred`) |
| ps2state_slot0_f39807 | 0x277121d6d0d6a983 | 1 | 2 | none (`relaunch=deferred`) |
| ps2state_slot0_f40391 | 0x06c41ff2a4a6c009 | 1 | 2 | none (`relaunch=deferred`) |
| ps2state_slot0_f44111 | 0x5403edb668ca8e60 | 1 | 2 | none (`relaunch=deferred`) |
| ps2state_slot0_f46203_red_crystal | 0x7f2b0d91a8ec07b0 | 1 | 2 | none (`relaunch=deferred`) |
| ps2state_slot0_f46949_yellow_crystal | 0x334679dbee909b7b | 1 | 2 | none (`relaunch=deferred`) |
| ps2state_slot0_f57292 | 0xfc8deca68213dc9c | 1 | 2 | none (`relaunch=deferred`) |
| ps2state_slot0_f59991 | 0x9d674482db08223c | 1 | 2 | none (`relaunch=deferred`) |
| slowspot_f37092_s1789428268 | 0xd5ff7161f930509e | 1 | 2 | none (`relaunch=deferred`) |
| slowspot_f40298_s1789693059 | 0x6f74f07f86f2ef4a | 1 | 2 | none (`relaunch=deferred`) |

`restore_mhash_ok=1` on 11/11 (the binary's own MemoryCheckpointer hash after the file restore equals
`games/<t>/verify/restore_mhash_expected.tsv` = `state_hash.py`); every one of the 28 other files in `save_states/`
restores too (`restore_mhash_ok=unknown_anchor`: not in the expected table). **Not done:** the relaunch of the parked guest
threads from `ThreadSection::RestoredFor(tid)` (context + shadow chain, `is_boundary` naming the boundary thread) — the anchor
legs close right after the restore evidence with `park=none relaunch=deferred`, so the "each anchor → `park=sif_rpc_absent`"
half of G4 is NOT met.

### P4FIX-F1 (docs/bugs/P4FIX-F1-callback-slot-zero.md)
W1: both anchors restore sound (`restore_mhash_ok=1`, 2 THRD records each); the run-to-park half waits for the relaunch.
W2: not done (needs the FUN_001f4730 table range + the OLD `--tree v2` bounded run). W3: `RelaunchOrder` green.

### Gate status after 3F
```
$ ./verify/run_bounded.sh 900 /tmp/p3f/build6.log -- cmake --build --preset macos-dev -j4      # exit 0
$ ./verify/run_bounded.sh 900 /tmp/p3f/ctest_full2.log -- ctest --preset macos-dev              # exit 0
100% tests passed, 0 tests failed out of 74
Total Test time (real) = 148.28 sec
```

| # | Gate item | Status | Evidence |
|---|---|---|---|
| G1 | Regen determinism | ✔ (3B) | `RecompManifestIntact` green in this run; §3B |
| G2 | Recomp oracle suites | ✔ (3B/3C2) | ported suites + census green in this run |
| G3 | Restore oracle 11/11 | ✔ | `RestoreMhashAnchors` (test rig) + the binary's `restore_mhash_ok=1` ×11 above |
| G4 | Boot reachability | **partial** | coldboot → `park=sif_rpc_absent frames=0 finalized=1`, four lanes, 3 reps identical ✔; anchors → `park=sif_rpc_absent` ✘ (relaunch not wired) |
| G5 | Unit | ✔ | 74/74 incl. `ElfLoaderTitle`, `MmioSinkIdleValues`, `ThreadManagerThbase`, `SyscallTableCoverage` |
| G6 | Lints | ✔ | `LintOneClass` (235 files, 0), `LintEnginePurity` 0, `LintNoGetenv`, `FlagPhaseCountsReproducible`, `SettingsTableReproducible` (60 rows) |
| G7 | Ledgers | **partial** | workarounds README 7 rows ✔, `manual_body_ledger.tsv` 243 ✔, `INVOCATION.md` ✔; P4FIX-F1 W2 verdict ✘ (file created with W1/W3; W2 open) |
| DoD | | ✘ | boundary owner stub + sinks + empty sequencer slots exist ✔; anchor relaunch + W2 outstanding |

### Phase 3 gate after 3F: NOT PASSED (G4 anchor half + G7 W2 outstanding) — superseded by §3F′ below

### HANDOFF (3F → 3F′) — superseded by the 3F′ section below (p3fr review: NEEDS_CHANGES; the six items are closed there)

**Done:** the four p3er notes; the composition root (`EeBootstrap`, `LegCloser`, main.cpp 207 lines); `ps2r_title_recomp`
linked into `nativeee`; cold-boot leg parks at `sif_rpc_absent` deterministically (3 reps); 11/11 anchors restore in the
binary with `restore_mhash_ok=1`; `paths.game_files` setting; P4FIX-F1 W1 (restore half) + W3; tree green (74/74, lints 0).

**Not done / carried (in order):**
1. **Anchor relaunch** — after `RestoreFile`: `EeRunToken::BeginRelaunch()`, one host worker per `ThreadSection::RestoredFor(tid)`
   record (restore its `CpuContext` + shadow chain, `is_boundary` → the boundary thread), `EndRelaunch()` (thbase winner first —
   `RelaunchOrder` pins it), run until the first `sceSif*` row (`park=sif_rpc_absent`) or IOP wait (`park=iop_dependent`), 3 reps.
   Then the G4 anchor row and P4FIX-F1 W1's run-to-park half.
2. **W2 slot census** (`p4fix_slot=` on both trees; OLD `--tree v2` bounded 60 frames) → verdict into `docs/bugs/P4FIX-F1-*.md`.
3. `verify/run_leg.py --tree new`: pass `--game-files` (or resolve `game_files` relative to the repo), accept a park leg
   (`frames=0` when `park=` is set); `battery.py` Phase-3 mode (coldboot ×3 + 11 anchors ×3, `--restore-only` reads `restore_mhash_ok`).
4. P3-MANIFEST-12 disposition (regen with the 12 starts, or `no-hook` TSV rows); P3-BOOT-WAITSEMA compare with the OLD trace.
5. `KeyRedirect::NoteWrite` binding decision (write path vs boundary; arbiter = level-leg `mem.mhash`); HEAP section (HLEHeap half
   + `MetaArena::SerializeState` tail); `hle/budget` / `hle/loader_sync` data pairs; P3-RAMIMAGE-2 adjudication when a leg reaches
   `FUN_002d18f8`.
6. Then Phase 4 planning per D-SEQ-1: `iop/` in the OpenGOAL shape + `sif` + `cdvd` + `STREAM.IRX` + the sound core.

**Next command:** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`, then
`./verify/run_bounded.sh 120 /tmp/p3f2/cb.log -- ./build/app/host/nativeee --profile deterministic --headless --harness-out /tmp/p3f2/cb --frames 1500 --game-files $PS2R_OLD_ROOT/game_files`
(expect `park=sif_rpc_absent park_pc=0x2cdadc park_tid=1`), then item 1.

## 3F′ — anchor relaunch + harness park legs + Phase 3 gate (stage p3f loop 2, 2026-09-27)

### p3fr items closed (all six)
1. **Anchor relaunch wired.** `engine/ee/savestate/thread_relauncher.{h,cpp}` (`ThreadRelauncher`): `EeRunToken::BeginRelaunch` →
   for every THRD record with `started && has_ctx`: `ThreadManager::StartResumed(tid, ctx, chain-walk body)` + `MakeReady(tid,
   current_priority)` in file order → `EndRelaunch` (one election; `RelaunchOrder` pins the winner). The chain walk is OLD
   `ps2_threading.cpp:688-770` (`startThreadResumed`) transplanted: level 0 enters chain[0] at function start (the blocking stub
   re-executes the wait with the restored arguments) or, for `is_boundary`, at `ctx.$ra` (the post-HLE continuation); each outer
   frame re-enters at the returning frame's `ra` through the new `DispatchTable::InvokeAt(fn, enter, ctx)`; a frame naming no
   generated function parks the leg (`resume_no_function`) instead of silently re-running (OLD's manual-dispatcher mid-chain abort
   has no NEW counterpart: the pure-discovery emission has no manual dispatchers). `ThreadManager::RunGuestThread` now takes the
   worker's `WorkerArgs` (a `seed` context + `body` override for resumed workers; `SpawnWorker` shared with `StartThread`).
   The root (`ee_bootstrap.cpp`) relaunches after the restore evidence, `JoinAll`s (the park sink `_Exit`s the process), and
   records `relaunch=<count>` — the 11-anchor table below.
2. **W2 verdict recorded** in `docs/bugs/P4FIX-F1-callback-slot-zero.md`: the OLD crash line `src=0x1c *src=0x0` shows the
   `jalr *(r3+0x1c)` base `r3 ← EE[sp+0x10]` is a NULL object pointer on FUN_001f4730's caller frame, not an entry of a static
   callback table — a RAM-range FNV census of the state file has no target; and under NEW every anchor parks at the SIF row on
   tid 2 before FUN_001f4730 is reached. Adjudication ("0 in file" vs "zeroed after restore") is the first Phase-4 leg that passes
   the SIF park (`p4fix_obj=` on both trees). W1's run-to-park half is now done (f57292/f59991 rows below).
3. **`verify/run_leg.py --tree new`** passes `--game-files` (new `--game-files` flag, default `<repo>/game_files` = the frozen
   reference tree's copy) and accepts a park leg: `is_park_leg(meta)` (`park=` set and ≠ `none`) → `frames=0` accepted, the park
   keys (`park_pc`, `park_tid`, `unhandled_mmio`) required, zero-byte lane files accepted (present is still required). The G4
   legs below were produced THROUGH the harness (`run_leg: OK`).
4. **libc evidence + hit counter.** Recorded: the libc rows (memcpy@0x2c6ce0, memmove, memset, strlen) ARE installed (283 rows;
   not in `sdk_rows_not_fn_start_list`), and ZERO libc calls occur before the SIF park (p3fr's probe; confirmed by the new
   counter). `SdkBindingManifest` now counts hits per installed row (SdkBind: every call; SdkWrap: calls entered at the row's
   address) — `HitTotal()` / `HitList()` → run.meta `sdk_rows_hit=` / `sdk_rows_hit_list=` at the park. Cold boot:
   `sdk_rows_hit=2 sdk_rows_hit_list=sbrk@0x2cf930:1,_InitSys@0x2d5030:1`; anchors: `sceSifSetRpcQueue@0x2d2be8:1`. The first
   leg past the park will show `memcpy@0x2c6ce0:<n>` here without a debugger. `kMainThreadStackTop` (0x01FFFF00) moved to
   `TitleProfile::main_stack_top` (`title_profile.cpp`); `ee_bootstrap.cpp` carries no guest address (the only 5+-digit hex left
   in `app/host/` is the `free_list_0x181560` workaround's file/namespace name — the TITLE lane by design).
5. **Staleness/honesty.** `RunMeta` latches `start_ns_` at construction (arming), so `wall_ms` is real on a park leg (cold boot
   `wall_ms=177`); `OnPresent` still writes the early stamp at the first present. `main.cpp` header rewritten (process root;
   EeBootstrap owns the EE composition; the idle stub is the no-title-files path); the `--load-state` comment now says the
   payload is restored by EeBootstrap; the ELF directory is `TitleProfile::elf_dir` (`"example_title.rep"`), main.cpp 210 lines,
   0 guest addresses.
6. Handoff items carried unchanged (below).

### Files landed / changed (one primary type per pair; `LintOneClass` 239 files 0 findings, `LintEnginePurity` 0)

| File | Change | OLD source (read-only) |
|---|---|---|
| `engine/ee/savestate/thread_relauncher.{h,cpp}` | new `ThreadRelauncher` (above) | `ps2_threading.cpp:688-770`, `savestate.cpp:2194-2226` |
| `engine/ee/hle/thread_manager.{h,cpp}` | `StartResumed(tid, seed, body)`; `WorkerArgs` (class-scoped) with `seed`/`body`; `RunGuestThread(WorkerArgs&)`; `SpawnWorker` | — |
| `engine/ee/cpu/dispatch_table.{h,cpp}` | `InvokeAt(fn, enter, ctx)` | `recompiled_functions` call shape |
| `engine/ee/savestate/thread_section.h` | `Restored()` accessor | — |
| `engine/ee/hle/sdk_binding_manifest.{h,cpp}` | per-row hit cells, `HitTotal`/`HitList`, cleared by `ResetForTest` | — |
| `engine/core/verify/run_meta.{h,cpp}` | `wall_ms` from arming | — |
| `app/host/ee_bootstrap.cpp` | relaunch + `JoinAll` + `relaunch=` key; `sdk_rows_hit*` at the park; profile stack top | — |
| `app/host/main.cpp` (210 lines) | header, `elf_dir`, comments | — |
| `games/<t>/hle/title_profile.{h,cpp}` | `elf_dir`, `main_stack_top` | `main.cpp init_cpu_context` |
| `verify/run_leg.py` | `--game-files`, park-leg acceptance | — |
| `docs/bugs/P4FIX-F1-callback-slot-zero.md` | W1 run-to-park rows, W2 verdict | OLD `[IND-JUMP-BAD]` line (docs/loop_state) |

### The boot leg (G4) — through the harness: `verify/run_leg.py --binary build/app/host/nativeee --tree new --anchor coldboot --profile deterministic --frames 1500 --out D`
`run_leg: OK` ×3 (cb1..cb3) + 1 (cbz, post-lint rebuild). `run.meta` (keys beyond the frozen ones):
```
frames=0
wall_ms=177
finalized=1
sdk_rows_installed=283
sdk_rows_not_fn_start=12
syscalls_total=6
syscalls_first=InitMainThread(60),InitHeap(61),FlushCache(100),GetThreadId(47),WaitSema(68),GetThreadId(47)
sdk_rows_hit=2
sdk_rows_hit_list=sbrk@0x2cf930:1,_InitSys@0x2d5030:1
park=sif_rpc_absent
park_pc=0x2cdadc
park_tid=1
park_detail=
unhandled_mmio=1
unhandled_mmio_pages=0x10008000
syscalls_unknown=0
syscalls_not_implemented=0
```
Four lane files present (`frames.fhash`/`audio.ahash`/`mem.mhash` 0 bytes — no boundary before the IOP handshake; `run.meta`
1177 bytes). run.meta minus `wall_ms`/`build_id`: md5 `9a1ef7adae54aab0f1fc4b1c6f49e279` ×4 (the 3F value
`559b24c2caf6dcd3878ee934f7448589` differs only by the two new `sdk_rows_hit*` keys; every 3F key is unchanged).

### Anchor legs (G3 + G4's restore half) — `nativeee --profile deterministic --headless --harness-out D --frames 0 --game-files <OLD>/game_files --load-state <OLD>/save_states/<a>.ps2st`, bounded 120 s, 3 reps each (`/tmp/p3f2/anchors.sh 3`)

Every anchor: exit 0, `restore_mhash_ok=1`, `restore_thrd_records=2`, `relaunch=2`, `syscalls_first=GetThreadId(47)`,
`sdk_rows_hit_list=sceSifSetRpcQueue@0x2d2be8:1`, **`park=sif_rpc_absent park_pc=0x1e7fa4 park_tid=2`**, `unhandled_mmio=0`,
`finalized=1`; run.meta (minus wall_ms/build_id) identical across the 3 reps:

| anchor | restore_mhash | ok | park | park_pc / tid | md5 ×3 |
|---|---|---|---|---|---|
| artdet_f37152 | 0x8de41106ac4dad95 | 1 | sif_rpc_absent | 0x1e7fa4 / 2 | 165d44bf9b5d5230fdda1ebad1f5ac0c |
| ps2state_slot0_f36935 | 0xb8e09d48ba19fc73 | 1 | sif_rpc_absent | 0x1e7fa4 / 2 | 7bdc357481182947caeec9b091995c8a |
| ps2state_slot0_f39807 | 0x277121d6d0d6a983 | 1 | sif_rpc_absent | 0x1e7fa4 / 2 | 1a9105bc9cc6aa3f62a0e187c77a4ead |
| ps2state_slot0_f40391 | 0x06c41ff2a4a6c009 | 1 | sif_rpc_absent | 0x1e7fa4 / 2 | c86f0f49d13cd5a3c3b15e25bb7f2e4a |
| ps2state_slot0_f44111 | 0x5403edb668ca8e60 | 1 | sif_rpc_absent | 0x1e7fa4 / 2 | 433c4f1f17804c64c373c9557ef5c6e7 |
| ps2state_slot0_f46203_red_crystal | 0x7f2b0d91a8ec07b0 | 1 | sif_rpc_absent | 0x1e7fa4 / 2 | fde7f41b76f5052ee78c26f1abbe068e |
| ps2state_slot0_f46949_yellow_crystal | 0x334679dbee909b7b | 1 | sif_rpc_absent | 0x1e7fa4 / 2 | d9d28d93903652109e7d7b1b1066f06e |
| ps2state_slot0_f57292 | 0xfc8deca68213dc9c | 1 | sif_rpc_absent | 0x1e7fa4 / 2 | 6c6aa1c9763ee599de4fd385dc7be7aa |
| ps2state_slot0_f59991 | 0x9d674482db08223c | 1 | sif_rpc_absent | 0x1e7fa4 / 2 | 66884db1ceb2e177c83824b56f4059eb |
| slowspot_f37092_s1789428268 | 0xd5ff7161f930509e | 1 | sif_rpc_absent | 0x1e7fa4 / 2 | 93d75db77ab97e319c9c363171e6328c |
| slowspot_f40298_s1789693059 | 0x6f74f07f86f2ef4a | 1 | sif_rpc_absent | 0x1e7fa4 / 2 | 0955317aa64000d76255b535e81ce49e |

Reading: every anchor carries the same two started threads; the relaunch election picks tid 2 (the higher-priority thread, whose
innermost chain frame is the game's RPC-server setup), which re-runs its frame from the start — `GetThreadId` then the
`sceSifSetRpcQueue` row — and parks: the IOP-dependent wait, i.e. Phase 4's first item, as the plan's §1 step 3 predicts. tid 1
(the boundary thread) never gets elected before the process ends at the park, so its `ctx.$ra` re-entry path is wired but
unexercised in these legs (recorded, not claimed). The relaunch leaves RAM untouched between restore and park (`restore_mhash`
is computed before the relaunch; the mem lane has no boundary yet).

### Gate status after 3F′
```
$ ./verify/run_bounded.sh 900 /tmp/p3f2/build3.log -- cmake --build --preset macos-dev -j4     # exit 0
$ ./verify/run_bounded.sh 900 /tmp/p3f2/ctest2.log -- ctest --preset macos-dev                 # exit 0
100% tests passed, 0 tests failed out of 74
Total Test time (real) = 144.50 sec
```
(The first full ctest after the relaunch landed failed only `LintOneClass`: a comment index expression matched the
session-tag pattern; reworded, lints re-run green, full ctest re-run as quoted.)

| # | Gate item | Status | Evidence |
|---|---|---|---|
| G1 | Regen determinism | ✔ (3B) | `RecompManifestIntact` green |
| G2 | Recomp oracle suites | ✔ (3B/3C2) | ported suites + census green |
| G3 | Restore oracle 11/11 | ✔ | `RestoreMhashAnchors` + the binary's `restore_mhash_ok=1` ×11 (table above) |
| G4 | Boot reachability | ✔ | coldboot through `run_leg.py --tree new` → `park=sif_rpc_absent frames=0 finalized=1`, four lanes, `mem.mhash` empty, md5 ×4 identical; each of the 11 anchors → `park=sif_rpc_absent` (0x1e7fa4 / tid 2), 3 reps identical each — NEW self-consistency, recorded not claimed |
| G5 | Unit | ✔ | 74/74 incl. `ElfLoaderTitle`, `MmioSinkIdleValues`, `ThreadManagerThbase`, `SyscallTableCoverage`, `RelaunchOrder` |
| G6 | Lints | ✔ | `LintOneClass` (239 files, 0), `LintEnginePurity` 0, `LintNoGetenv`, `FlagPhaseCountsReproducible`, `SettingsTableReproducible` |
| G7 | Ledgers | ✔ | workarounds README 7 rows, `manual_body_ledger.tsv` 243, `INVOCATION.md`; P4FIX-F1 W2 **verdict recorded** in `docs/bugs/` (census has no static target; adjudication = first post-SIF leg) |
| DoD | | ✔ | boundary owner stub + MMIO sinks + empty sequencer slots exist (3D2); Phase 4 adds only its own files to reach boundary 0; this file carries every stage's handoff |

### Phase 3 gate: PASSED
(G1–G7 + DoD evidenced above. Honest limits stated with the evidence: the anchor legs park before any frame boundary, so the
boundary thread's `$ra` re-entry and the mem/audio/frames lanes are exercised only from Phase 4 on; W2's adjudication is the first
post-SIF leg; libc HLE bodies have unit-test evidence (`LibcHleDiff`) but no boot-leg hit yet — `sdk_rows_hit_list` will show it.)

### HANDOFF (3F′ → Phase 4 planning)

**Done:** relaunch (`ThreadRelauncher`, `StartResumed`, `InvokeAt`); harness park legs through `run_leg.py`; `wall_ms`; hit
counters; profile-owned ELF dir / stack top; P4FIX-F1 W1 complete + W2 verdict; gate table + verdict.

**Carried (unchanged from 3F, in order):**
1. `battery.py` Phase-3 mode (coldboot ×3 + 11 anchors ×3, `--restore-only` reading `restore_mhash_ok`) — the legs above were
   driven by `/tmp/p3f2/anchors.sh` (a helper script; the battery integration is a small follow-up).
2. P3-MANIFEST-12 disposition (regen with the 12 starts, or `no-hook` TSV rows with reasons); P3-BOOT-WAITSEMA compare with the
   OLD boot trace (WaitSema(68) before any CreateSema).
3. `KeyRedirect::NoteWrite` binding decision (write path vs boundary; arbiter = level-leg `mem.mhash`); HEAP section (HLEHeap half +
   `MetaArena::SerializeState` tail); `hle/budget` / `hle/loader_sync` data pairs; P3-RAMIMAGE-2 adjudication when a leg reaches
   `FUN_002d18f8`.
4. P4FIX-F1: `p4fix_obj=` (EE[sp+0x10] at FUN_001f4730 entry) on the first post-SIF leg, NEW and OLD `--tree v2` (bounded 60 frames).
5. **Phase 4 plan next (D-SEQ-1):** `iop/` in the OpenGOAL shape + `sif` (the `sceSifSetRpcQueue`/`sceSifBindRpc`/`sceSifCallRpc`
   rows the parks name: cold boot caller `0x2cdadc` tid 1, anchors caller `0x1e7fa4` tid 2) + `cdvd` + `STREAM.IRX` + the sound core.

**Next command:** `cmake --build --preset macos-dev -j4 && ctest --preset macos-dev`, then
`python3 verify/run_leg.py --binary build/app/host/nativeee --tree new --anchor coldboot --profile deterministic --frames 1500 --out /tmp/p4/cb`
(expect `run_leg: OK`, `park=sif_rpc_absent park_pc=0x2cdadc park_tid=1`), and
`python3 verify/run_leg.py --binary build/app/host/nativeee --tree new --anchor save_states/ps2state_slot0_f57292.ps2st --frames 0 --out /tmp/p4/a1`
(expect `park=sif_rpc_absent park_pc=0x1e7fa4 park_tid=2 relaunch=2`).
