# ARCHITECTURE — NativeEE Target Design

## Current design rule — native game intents (user, 2026-10-07)

The target is an OpenGOAL-style native port of **the title**.
DMAC/VIF/GIF, the IOP kernel and recompiled STREAM.IRX, and the IPU command
interface are **temporary scaffolding to retire**. Do not deepen their
hardware fidelity. When a gap or defect reaches one of these models, implement
the game's intent at a native boundary so that the affected model path stops
executing.

Use the read-only `the title-reversed` platform layer as the behavioral spec:
`Platform::Stream`, `Movie`, `Audio`, `Graphics`, the renderer's `buckets.cpp`
chain meanings, and `multistream/` RPC command meanings. Validate PAL-derived
bindings against the NTSC executable. Reusable host mechanisms belong in
`engine/`; title meanings, layouts and bindings belong in the game module.

[HLE_COVERAGE.md](HLE_COVERAGE.md#scaffolding-retirement) tracks each remaining
intent, its native replacement and the evidence required to remove the old
path. Existing models can support migration and diagnosis; a reached gap is
not authorization to add another hardware mode, command, scheduler or timing
rule. Retirement requires equivalent game-visible results and evidence that
the replaced path is no longer reached. Regression failures remain visible;
this rule does not relax tests or authorize a new baseline.

This rule supersedes conflicting hardware-completion, transplant-only and
“not a rewrite” directions in the historical design below.

Design-synthesis output (stage: design, session s113, 2026-09-22). Inputs: the five
audits in `../audit/` (A1 structure, A2 config/diagnostics, A3 GS/render, A4 CPU/HLE,
A5 verification) plus external reference-architecture research (§2). This document
describes the TARGET; how to get there is `MIGRATION_PLAN.md`; how to prove nothing
broke is `EQUIVALENCE_PLAN.md`.

[REV2 2026-09-23 D4-D8] Revised per five further binding user decisions, applied on
top of D1/D2/D3 and grounded in audit A6 (game-specificity): **(D4)** one class per
`.h`/`.cpp` pair, headers declarations-only, no textual `.inc` code includes, with a
CI lint in `tests/` (§13); **(D5)** a top-level `engine/` vs
`games/<title>/` split — engine holds all GENERIC-PS2 code plus
the mechanism half of every A6 MIXED cluster; the game module holds the data half
(VU1 program library, renderer profile, VRAM layout, timing quirks, STREAM.IRX
recomp, title profile, corpus) behind concrete `TitleProfile` / `Vu1ProgramLibrary` /
`RendererProfile` interfaces (§14, tree in §3); **(D6)** intentional modding hooks
via a per-title declarative manifest — never workarounds, always off for
correctness/equivalence (§15); **(D7)** per-target platform folders behind ONE
`Platform` interface with an honest console feasibility table (§16); **(D8)** a
seeded bug knowledge base at `docs/bugs/` (§17).


---

## 1. Goals and non-goals

### Goals

1. **Native-first.** the title as a native application: retain recompiled
   game logic where useful and replace PS2 platform machinery with native game
   services. Retire each scaffolding path as its game intents move across.
2. **Preserve game behavior.** Validate native replacements with the differential
   harness, game-state checks, console rendering/audio evidence and the existing
   corpus/replay assets. DT-8 defines the standing regression contract.
3. **Maintainable.** No file over ~1,500 lines, no function over ~200, zero mutable
   globals in headers, zero `getenv` outside the config loader, zero session tags in
   identifiers. One concept = one home (the A1 §7 concept map made canonical).
4. **Portable, including Vita-class.** Every architectural choice is checked against
   a 512 MB / quad Cortex-A9 / GLES2-class-GPU floor (A4 §7 blockers B1–B12). The
   design does not require GL 4.x, compute shaders, 64-bit hosts, gigabyte stacks,
   or a desktop-grade driver thread.
5. **One deterministic profile.** A single blessed configuration in which the whole
   program is tick-deterministic end to end — the foundation of both CI and the
   old-vs-new differential proof.

### Non-goals

- **Not a general PS2 emulator.** One game (SLUS-12345), one SDK (3.0.1), one IOP
  custom module (STREAM.IRX). Scope cuts that are safe for this game (no TLB, no
  CSM2 COU/CSA, no EFU pipeline) stay cut, but must be *documented* as cuts.
- **Not a graphics enhancement project (yet).** Scale>1, interp, texture packs etc.
  remain optional layers; correctness at scale=1 is the contract. RT64-style
  enhancement is a possible future consumer of the DrawIR, not a goal now.
- **Bounded native replacement.** Replace one specified game intent at a time.
  Preserve relevant rendering invariants, game semantics and supported save
  behavior while removing its dependence on hardware scaffolding.
- **No new behavior behind flags.** The env-gate era ends: a change is either the
  code, or it does not exist. Escape hatches are replaced by tests. *Informed by:
  DuckStation — hack options discouraged, one default configuration expected to
  run everything.*
- **Bug-for-bug by default.** OLD-tree behavior — including latent wrongness —
  is preserved unless a change passes the accepted-difference protocol
  (EQUIVALENCE_PLAN §7) with hardware-truth evidence. *Informed by: Unleashed
  Recompiled — original game bugs intentionally preserved; behavior-change
  reports require original-hardware footage.*

---

## 2. Reference architectures (researched 2026-09-22; paraphrased, no GPL text)

Full per-project study (repo layouts, IR/emission, hooks, config, testing,
portability, comparison matrix, lessons): `REFERENCE_ARCHITECTURES.md`. The table
below is the summary; sections edited from its lessons carry an
`Informed by: <project>` note.

| Project | What we take | Source |
|---|---|---|
| **N64Recomp / N64ModernRuntime / Zelda64Recomp** | The three-way split: recompiler *tool* (N64Recomp: static MIPS→C, portable output) / runtime *libraries* (`librecomp` = generated-code↔runtime bridge + leftovers like overlays and cart DMA; `ultramodern` = OS reimplementation: threads, controllers, audio, message queues, timers, RSP task handling, VI timing, with platform I/O supplied via callbacks) / *renderer* registered by the app (RT64). The game shell (Zelda64Recomp) is thin; per-game knowledge is DATA (TOMLs, symbol submodule). Single-file patch mode = link-time function overrides. Our analogue: `tools/recompiler` ↔ the subsystem libraries ↔ `render/backends/` ↔ `app/`. | https://github.com/N64Recomp/N64Recomp • https://github.com/N64Recomp/N64ModernRuntime • https://github.com/Zelda64Recomp/Zelda64Recomp |
| **OpenGOAL (jak-project)** | Decompile-to-source + recompile via an owned compiler; the runtime (`gk`) is a small C++ kernel implementing the PS2 OS surface + IOP module replacements + native renderers that consume the game's own DMA/VU data. The tfrag1→tfrag3 bring-up ladder (exact slow reference consumer → native per-family renderer) IS our VU1-HLE → DrawIR → class-renderer pipeline. Kernel discipline: as close to original as possible, every divergence commented. Golden `_REF` outputs pin regressions in CI. | https://github.com/open-goal/jak-project • https://opengoal.dev/docs/porting-info/drawable_and_tfrag/porting_tfrag/ |
| **XenonRecomp / XenosRecomp / Unleashed Recompiled** | Runtime-less tool ↔ game runtime split (`UnleashedRecompLib` generated+config vs `UnleashedRecomp` kernel/GPU/APU/UI). Literal PPC→C++ with TOML-driven jump tables, function boundaries, and **mid-asm hooks** (generator inserts calls, linker resolves); weak-alias function overrides keep the original callable. Shaders recompiled OFFLINE into a hash-keyed embedded cache; pipeline compilation woven into asset loading (never on the draw path); renderer over the small `plume` RHI (one file per API). Optimization flags staged strictly after correctness. Original bugs preserved; changes need original-hardware evidence. | https://github.com/hedge-dev/XenonRecomp • https://github.com/hedge-dev/XenosRecomp • https://github.com/hedge-dev/UnleashedRecomp • https://github.com/renderbag/plume |
| **libultraship (Ship of Harkinian) / RSDKv5** | The engine/game split as the port-enabling seam: LUS (libultra reimpl + Fast3D display-list renderer + SDL platform layer) is reused across SoH/2Ship/Starship on Win/Linux/macOS/iOS/Android; RSDK's engine-vs-game repos let the community port Mania to Switch/3DS/Wii/WebASM/**Vita** without touching game logic. Assets/mods live in archive layers, not engine special cases. | https://github.com/Kenix3/libultraship • https://github.com/RSDKModding/RSDKv5-Decompilation |
| **PCSX2 GS** | The proven GS layering: `GSLocalMemory`+`GSClut`+`GSBlock/GSTables` (ONE format layer: per-PSM descriptor table with addressing functions), `GSState` (register file, dual contexts, vertex kick, Flush), `GSRenderer`→`GSRendererHW` (draw-level policy: texture shuffles, channel shuffles, game hacks), `GSTextureCache` (`Source` = texture read of GS memory, `Target` = framebuffer/zbuffer, dirty-rect tracking, hash cache, overlap detection), and `GSDevice` implementations per API (OpenGL/Vulkan/Metal/D3D/SW) beneath it all. Draw order must be preserved (GS semantics). Our `gs/format`, `gs/state`, `gs/hle/targets`+`gs/hle/texsrc`, and `render/backends/*` mirror this decomposition. | https://deepwiki.com/PCSX2/pcsx2/3.1-graphics-synthesizer-core • https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/HW/GSRendererHW.cpp • https://wiki.pcsx2.net/PCSX2_Documentation/Graphics_Synthesizer,_GPUs_and_Dual_Cores |
| **Dolphin VideoCommon** | The unification lesson: per-backend feature implementations breed divergent bugs and feature inequality; move ALL emulation logic into a common layer and reduce each backend to an API adapter over abstract primitives (textures, staging textures, shaders, pipelines, framebuffers). After unification a brand-new backend (D3D12) was written quickly and 55% smaller. EFB peek/poke/copy logic unified with fast/slow paths chosen by backend *capability flags*, not backend forks. | https://dolphin-emu.org/blog/2019/04/01/the-new-era-of-video-backends/ (archived: https://web.archive.org/web/2024/https://dolphin-emu.org/blog/2019/04/01/the-new-era-of-video-backends/) • https://github.com/dolphin-emu/dolphin/pull/6385 |
| **RPCS3 RSX** | One common command-processing core (`rsx::thread` walking the GCM FIFO) with per-API `GSRender` subclasses (`GLGSRender`, `VKGSRender`) and *templated/shared* texture-cache and surface-store logic in `Emu/RSX/Common` reused by both. Confirms: coherence logic (surface cache, texture cache) belongs in the common layer, parameterized by a thin device interface. | https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/VK/VKGSRender.cpp • https://blog.rpcs3.net/2017/08/16/rewriting-vertex-processing-for-massive-performance-gains/ |
| **DuckStation / Panda3DS** | The lean-codebase ethos: playability, speed, and long-term maintainability as stated goals; hack options discouraged — ONE default configuration expected to run everything (our "one shipped mode"). Vectorized multithreaded software renderer as a first-class backend beside the HW ones. Panda3DS: HLE-first, deliberately small and contributable. | https://github.com/stenzek/duckstation • https://github.com/wheremyfoodat/Panda3DS |
| **PS Vita homebrew (vitasdk)** | The portability floor: open toolchain (vitasdk, newlib-based, CMake), GPU access via `sceGxm` — a GLES2-feature-class API; no OpenGL 4.x, no compute; the community GL path is vitaGL, a GL1/2-subset wrapper translating to sceGxm. Hardware: quad Cortex-A9 (apps get 3 cores, ~444 MHz), NEON, 512 MB system RAM (app budget ~256–330 MB) + 128 MB VRAM, 32-bit ARM. SDL2 has a Vita port. Real ports document explicit newlib-heap budgets (Doom 3: 300 MB; Re-Volt: 160 MB) and custom-heap builds of vitaGL; on-device shader compilation is expensive enough that ports ship compiler speedhacks — precompiled pipelines are a requirement. Consequences: bounded stacks, dense code, backend caps must express "no compute / limited formats", 32-bit-clean guest-keyed maps, explicit memory plan. | https://vitasdk.org/ • https://github.com/Rinnegatamante/vitaGL • https://github.com/Rinnegatamante/d3es-vita • https://github.com/Rinnegatamante/rvgl-vita • https://gl33ntwine.com/posts/develop-for-vita/ |

---

## 3. Layered module map

> **[CONSTRUCTION 2026-09-26]** The tree below is the design drawing. The scaffolded
> construction tree deviates from it in a few recorded places (added `engine/core/verify`,
> `engine/ee/sched`, `engine/platform/sdl`, `engine/sound/hle`, top-level `tools/`,
> `games/<t>/{recomp,data,hle/workarounds}`; `EEAnalyze` → `ee_analyze`; `gen/` tracked
> with a manifest instead of build-dir only; per-OS platform folders, non-GL backends and
> per-module `iop/hle/<module>/` dirs created by the phase that fills them). The authoritative
> drift table is `CONSTRUCTION_PLAN.md` §0.1; the layering rules and the engine/game split
> here are unchanged. §4.8's "38 fields" counts PRODUCT config fields; the harness/paths/meta
> plumbing fields are enumerated in `docs/phases/PHASE1_PLAN.md` §3 (R14).

[REV 2026-09-22 D1/D2/D3] Reorganized per the three user decisions: (D1) one
recompiler engine — v2 static discovery — with legacy/Ghidra/overrides retired;
(D2) no game-hooks concept and no `hle/game/` — every former override is
reclassified in `HOOK_RECLASSIFICATION.md`; (D3) **each PS2 subsystem directory
owns its state/register model AND its own `hle/` subfolder** — the HLE of a
device lives with the device, not in a central `hle/` tree.

[REV2 2026-09-23 D4-D8] One more axis on top of D3, from audit A6: **title-neutral
vs title-bound**. The tree gains a top-level split — `engine/` (all GENERIC-PS2
code + the mechanism half of every A6 MIXED cluster: the recompiler tools, the
ee/iop/vu/gs/… subsystems with their `hle/`, render, core, platform) and
`games/<title>/` (everything GAME-SPECIFIC + the data half of
MIXED per A6 §7: the VU1 program library, the renderer profile = classifier
taxonomy + Accept tables + VRAM-layout constants, the timing quirks
(XGKick/roundSprite/budget) as TitleProfile data, the STREAM.IRX recomp, the title
profile, the title savestate-section schema, corpus/baselines/savestates,
title-specific harness scripts, and the recompiled output). Build rule: **`engine/`
links with ZERO references into `games/`** — a title registers everything through
the §14 interfaces at boot (the same pattern `classrdr::Register` / `vu1hle::
Register` already use). CI greps `engine/` for the A6 §1.2 game-token regexes
(EE-range address literals, `FUN_`/`func_00xxxxxx`, the game VRAM base constants,
game strings, VU1-HLE program stamps) — the A6 §3.3/§4 constants are the seed
denylist and the count must trend to zero (`tests/lint/lint_engine_purity.py`,
§13). The D2 subsystem-HLE rule is unchanged and now nested: engine HLE still
binds only by library signature / hardware surface / timing observable; what D5
adds is a HOME for the game-side data those mechanisms consume, so "game knowledge
enters as data" acquires a directory with a linker-enforced boundary. The tree
also shows representative one-class-per-file pairs (D4, §13): a snake_case file
name IS the PascalCase class it holds.

Layering rule: an arrow may only point DOWN this list. `platform` and `core`
know nothing above them; `render/backends` know the `gs/hle/ir` record types and
`render/backend.h` but never gs state internals; generated code calls only the
`ee/` seams frozen in A4 §1.3. `games/<title>/` sits ABOVE `engine/` (it may
include engine headers; never the reverse); `app/` sits above both and is the
only place a title module is linked to the engine.

```
ps2r/
├── engine/                         # [REV2 D5] title-neutral: PS2 semantics + host services. Links with
│   │                               #   ZERO references into games/ (tests/lint/lint_engine_purity.py, §13)
│   ├── tools/                      #   build-time programs; never linked into the engine runtime libraries
│   │   ├── recompiler/             #     THE engine [REV D1]: v2 static discovery (8,819 fns, 177 jump-table
│   │   │   │                       #       sites, corpus audit GAPS=0 — REFACTOR_COMPLETE.md); legacy CLI,
│   │   │   │                       #       Ghidra JSON, recomp_overrides.ini pins RETIRED (MIGRATION Phase 2);
│   │   │   │                       #       [REV2 D5] zero per-address emission cases (A6 §7 row J: the
│   │   │   │                       #       recompiler.cpp:903-1008 chain + inject_fun_1017c8 retire under D1);
│   │   │   │                       #       consumes TitleProfile.recomp inputs (ELF, corpus pairs) as data
│   │   │   ├── config/             #       per-game metadata that remains, data-not-code: corpus pair files
│   │   │   ├── loader/             #       ELF sections, .text image, symbol seeds
│   │   │   ├── discovery/          #       leaders, two-engine jump tables, function set (v2 lineage)
│   │   │   ├── ir/                 #       IR node defs, MIPS→IR lifter, IR verifier (§5)
│   │   │   ├── emit/               #       IR→C++ emitter, shard writer, dispatch/entry tables
│   │   │   └── corpus/             #       corpus-as-audit (observed jr/jalr pairs vs static tables)
│   │   ├── iop_recompiler/         #     R3000A → C++ MECHANISM (per-title IRX inputs live in games/<title>/)
│   │   ├── sce_lib_finder/         #     SDK signature matching → the SdkBindingManifest input (§4.9) —
│   │   │                           #       under D2 the ONLY way an HLE row acquires an address
│   │   ├── EEAnalyze/              #     MIPS static-analysis lib (rabbitizer-backed)
│   │   ├── diagnostics/            #     kept offline dump/trace/log analysis scripts
│   │   └── triage/                 #     [REV3 2026-09-23 D9] bundle-consumer CLI: `triage list|repro|lift|perf`
│   │                               #       (docs/design/TRIAGE_CAPTURE.md §10); builds against subsystem libs +
│   │                               #       verify/ only — bundle paths are arguments, zero game tokens
│   ├── core/                       #   platform-independent primitives (no SDL, no GL, no getenv)
│   │   ├── config/                 #     THE Config schema + ini loader + deterministic profile preset (§8)
│   │   ├── log/                    #     compile-time category logger; sinks: file/stderr/ring (§9)
│   │   ├── diag/                   #     diagnostics registry + probe mechanism (PS2R_DIAGNOSTICS builds, §9)
│   │   ├── savestate/              #     PS2SAVE1 envelope, SectionSpec registry, CRC, quiescence (§4.11)
│   │   ├── threads/                #     queues, parks, cpu_relax shim
│   │   └── triage/                 #     [REV3 2026-09-23 D9] play-time capture subsystem (§18; TRIAGE_CAPTURE.md):
│   │       │                       #       one class per file, compiled into ALL builds (unlike core/diag)
│   │       ├── triage_host.h/.cpp · triage_bundler.h/.cpp · fallback_detector.h/.cpp
│   │       ├── perf_monitor.h/.cpp · spike_detector.h/.cpp · savestate_ring.h/.cpp
│   │       └── input_log.h/.cpp · bundle_manifest.h/.cpp · triage_dedup.h/.cpp
│   │                               #       · triage_writer.h/.cpp · frame_annotator.h/.cpp
│   │
│   │  # ── PS2 subsystems: each owns its state/register model + its own hle/ ── [REV D3]
│   ├── ee/                         #   Emotion Engine: recomp glue, kernel HLE, timing domain
│   │   ├── cpu/                    #     CpuContext + PS2-float helpers; flat dispatch; frozen seams
│   │   ├── mem/                    #     address map, fastmem, MMIO router
│   │   ├── timing/                 #     [REV2 D4 representative pairs]
│   │   │   ├── time_domain.h/.cpp  #       TimeDomain family: TickSource → EmuDomain → DerivedClocks (§4.7)
│   │   │   └── boundary_protocol.h/.cpp #  the ONE frame-boundary sequence
│   │   ├── intc/                   #     INTC + EE timers T0-T3
│   │   ├── hle/                    #     kernel/OS surface: syscalls, threads/semas, boot env, libc,
│   │   │                           #       libkernl/libscf/libdev bindings, dyn-decode fallback
│   │   └── mods/                   #     [REV2 D6] ModHost: manifest loader + detour installer over the v2
│   │                               #       update_function_impl seam; safe re-entry; OFF unless --mods (§15)
│   ├── iop/                        #   IOP exec env — OpenGOAL shape [IOPK 2026-09-24]: native code under a
│   │   │                           #     small cooperative kernel; NEVER CPU emulation (see § IOP model)
│   │   ├── iop_kernel.h/.cpp       #     class IopKernel — cooperative scheduler: thread table, priorities,
│   │   │                           #       states Dormant/Ready/Run/Wait; wait kinds Sleep/Delay/Sema/
│   │   │                           #       EventFlag/Mbx/Rpc; dispatch() runs Ready by priority to idle,
│   │   │                           #       fires due events/timers, returns the next deadline
│   │   ├── iop_coroutine.h/.cpp    #     class IopCoroutine — platform fiber wrapper (ucontext / Win32 fibers,
│   │   │                           #       the same backend as the EE scheduler; no third-party switcher)
│   │   ├── iop_host_thread.h/.cpp  #     class IopHostThread — the ONE host thread (REALTIME policy): dispatch,
│   │   │                           #       then condvar-sleep until EE wakeup or next deadline (OpenGOAL loop)
│   │   ├── iop_clock.h             #     class IopClock — the ONLY kernel time source: REALTIME=steady_clock;
│   │   │                           #       FRAME=EE-frame-derived virtual us + forced discrete-event advances
│   │   ├── sif_rpc_bridge.h/.cpp   #     class SifRpcBridge — RPC = memcpy + wakeup: EE→IOP commands run on the
│   │   │                           #       module's own RPC-dispatch coroutine; IOP→EE marshals park Wait::Rpc,
│   │   │                           #       serviced at EE frame/syscall edges (copy-back at defined points)
│   │   ├── iop_module_table.h/.cpp #     class IopModuleTable — module registration, boot sequencing (init runs
│   │   │                           #       to first block before the EE starts), dispatch-policy pumps
│   │   ├── loader.h/.cpp           #     IRX load / reboot handshake (mechanism; module location = title data)
│   │   └── hle/                    #     STOCK Sony module stubs, one dir per module (IOPRP300 set):
│   │       │                       #       sio2man/ padman/ mcman/ mcserv/ libsd/ sdrdrv/ cdvdstm/ …
│   │                               #       — custom modules are title plugins (§14.1): games/<title>/iop/ =
│   │                               #       recompiled STREAM.IRX + its module registration + protocol glue
│   ├── dmac/                       #   channels + chain walkers; hle/ = sce libdma surface
│   ├── gif/                        #   GIFtag parse + PATH arbiter; hle/ reserved [REV-2 D3]
│   ├── vif/                        #   VIF0/1 + UNPACK (+ MTVU submit seam); hle/ reserved [REV-2 D3]
│   ├── sif/                        #   mailbox regs + RPC router; hle/ = sceSif surface + module routing
│   ├── vu/                         #   register state, ONE decode, flag pipeline, XGKICK
│   │   ├── vu0/                    #     macro-mode COP2 helper library + micro
│   │   ├── tiers/                  #     VU1 interp (instrumented reference) + cached; AOT deleted (NOAOT)
│   │   ├── vu_float.h/.cpp         #     [REV2 D5] single-sourced PS2-float helpers (A6 §7 row A — the ~140 ln
│   │   │                           #       duplicated in vu1_hle_programs.cpp collapse here)
│   │   └── hle/                    #     MECHANISM ONLY [REV2 D5]: contract.h (verbatim), registry + dispatch/
│   │                               #       fallback/DIRECT/DrawIR ladder, parameterized by Vu1FamilySignature
│   │                               #       (§14.2). The PROGRAM LIBRARY (6,207 ln, A6 §2.3) lives in games/
│   ├── gs/                         #   Graphics Synthesizer
│   │   ├── format/                 #     THE format layer (§4.2) [REV2 D4 representative pairs]
│   │   │   ├── psm_layout.h/.cpp   #       class PsmLayout — the constexpr per-PSM descriptor table
│   │   │   ├── clut.h/.cpp         #       ONE Csm1Swizzle + ReadClut32/16
│   │   │   ├── texa.h/.cpp · expand.h/.cpp · alpha.h/.cpp
│   │   ├── state/                  #     GsRegisters (dual ctx) + vertex kick + ONE flush → DrawIR;
│   │   │                           #       roundSprite MODE is TitleProfile.timing data [REV2 D5]
│   │   ├── vram/                   #     [REV2 D4 representative pairs]
│   │   │   ├── vram_image.h/.cpp   #       class VramImage — 4 MB mirror + single-writer commit (§4.3)
│   │   │   └── page_record.h/.cpp  #       PageRecord + the commit state machine
│   │   ├── xfer/                   #     host↔local / local↔local transfers (write-side swizzle via format/)
│   │   └── hle/                    #     the NATIVE RENDERER — the GS's HLE [REV D3]
│   │       ├── ir/                 #       DrawIR record defs (§4.6) + G-C1 verifier
│   │       ├── queue/              #       RenderQueue of DrawIR jobs (EE→GFX boundary; pace ledger)
│   │       ├── targets/
│   │       │   └── render_target.h/.cpp #  class RenderTarget [REV2 D4 representative pair] (§4.4)
│   │       ├── texsrc/
│   │       │   └── texture_source.h/.cpp # TextureSource + ResolveRead [REV2 D4 representative pair] (§4.5)
│   │       ├── classrdr/           #       [REV2 D5] framework + PARAMETERIZED idiom lowerings ONLY
│   │       │                       #         (strip/skinned/sprite/particle/blit/stencil-composite);
│   │       │                       #         taxonomy, stamp map, Accept tuples, S9 recipe, VramLayout are
│   │       │                       #         RendererProfile DATA registered by the title (§14.3; A6 §7 B/C)
│   │       ├── interp/             #       optional frame interpolation (identity-stamp matched)
│   │       ├── present/            #       scanout composition, present policy + pacing
│   │       └── sce/                #       libgraph surface (SdkBind rows, HOOK §3.5)
│   ├── cdvd/                       #   ISO9660 walker, async read engine, latency model, FMV; hle/ = libcdvd
│   │                               #     + the completion-visibility model (HOOK §3.6)
│   ├── ipu/                        #   hle/ = sceMpeg + IPU decode HLE (FMV pipeline)
│   ├── pad/                        #   scePad protocol; hle/ = libpad bindings
│   ├── mc/                         #   memory card; hle/ = libmc over host directory
│   ├── sound/                      #   SPU2 core: mixer, voices, VAG, ring — GENERIC [REV2 D5]; the
│   │                               #     STREAM.IRX feed-protocol contract points are title data (A6 §5/§7 G)
│   ├── render/                     #   the backend CONTRACT (§6): backend.h, Caps, surface handles
│   │   └── backends/               #     gl/ · soft/ (oracle) · vulkan/ (rebuilt) · gxm/ (Vita) · gx/
│   │                               #     (Wii/GC research stub) — per-target mapping table in §16
│   └── platform/                   #   [REV2 D7] ONE Platform interface; no #ifdef outside this tree (§16)
│       ├── platform.h              #     window/surface, input, audio out, file IO, threads-or-coroutines,
│       │                           #     timers, memory mapping + PlatformCaps
│       ├── linux/ · macos/ · windows/ · psvita/ · wii/ · gamecube/
│       │                           #     one folder per target, each with its CMake toolchain file; a folder
│       │                           #     may exist only with a feasibility note + CI-buildable stub (§16)
│       └── _template/              #     the copy-me skeleton: every Platform entry point stubbed + checklist
├── games/
│   └── <title>/  # [REV2 D5] the title NTSC-U — ALL title-bound content (A6 §7/§8)
│       ├── title_profile.cpp       #   the TitleProfile literal (§14.1)
│       ├── vu1_library/            #   §14.2 + A6 §2.3: 15 native programs (one family per file, D4),
│       │   │                       #     223 entries / 176 variant hashes (12 names / 15 fns), ~40 game-convention helpers,
│       │   └── vu1_library.cpp     #     family signature {kVariantBase 0x1720, dispatcher op 0x090E0003,
│       │                           #     exit stubs 0x70/0x90, family exit TPC 0xA0}, registration table
│       ├── renderer_profile.cpp    #   §14.3 DATA: stamp→class map, per-class Accept tuples (census rows),
│       │                           #     S9 seven-pass recipe, FMV family decomposition, VramLayout table
│       │                           #     (~220 constants leave engine code — A6 §3.3), raw-classifier spec
│       ├── timing_quirks in title_profile: xgkick_defer=true, round_sprite_mode=1, budget bases (A6 §4)
│       ├── hle/                    #   title HLE (A6 §7 row E): loader_sync.cpp (FUN_0017d6b8 spin model),
│       │                           #     budget.cpp (FUN_00191bd0 emu params), boot argv @0x30a980
│       ├── iop/stream_irx/         #   STREAM.IRX recomp output + lti_ protocol handlers + feed contracts
│       │                           #     (recv buffer 0x3B3000 et al. — A6 §5)
│       ├── savestate/              #   title-tied SectionSpec providers (IOPM/STREAM/MCHL/SDRX semantics)
│       ├── symbols/                #   per-title symbol map: adjudicated names ↔ v2-discovered addresses —
│       │                           #     the ONLY address vocabulary mods may target (§15)
│       ├── mods/
│       │   ├── manifest.toml       #   [REV2 D6] declarative manifest, EMPTY by default (§15)
│       │   └── examples/           #     widescreen/ · fps60_unlock/ · debug_camera/ (hypothetical, documented)
│       ├── gen/                    #   BUILD-DIR generated output namespaced per title: recompiled EE shards,
│       │                           #     VU payloads, sce_hle_bindings, iop_recompiled
│       ├── corpus/                 #   gs_corpus/, vutrace_*/, save_states/ anchors + frozen baselines —
│       │                           #     every corpus asset is this title's (A6 §6)
│       ├── triage/                 #   [REV3 2026-09-23 D9] play-time capture bundles (gitignored except README):
│       │                           #     {fallbacks,perf,spikes,crash,manual}/<UTC>_<id>/ + .ring/ (rolling
│       │                           #     savestate+padlog slots) + index.tsv (dedup) — TRIAGE_CAPTURE.md §7
│       └── verify/                 #   title-specific harness scripts + anchor lists (harness itself: /verify)
├── app/
│   ├── host/                       # composition root ≤500 ln: construct engine, register ONE TitleProfile,
│   │                               #   install manifests, spawn threads; mods loaded ONLY under --mods (§15)
│   └── gui/                        # ImGui panels; third_party/imgui vendored here
├── generated/                      # BUILD DIR ONLY (title outputs redirect to games/<title>/gen/)
├── tests/                          # ctest suites + replay tools (gs_replay, vu1_replay, vif_replay, …)
│   └── lint/                       #   [REV2 D4] lint_one_class.py + lint_engine_purity.py, ctest-registered (§13)
├── verify/                         # the differential harness (EQUIVALENCE_PLAN.md) — title-agnostic
└── docs/                           # SAVESTATE_SCHEMA, INVARIANTS, HOOK_RECLASSIFICATION, decision records;
    └── bugs/                       #   [REV2 D8] TEMPLATE.md + INDEX.md + TRIAGE.md + 30 seeded entries (§17);
                                    #   docs/loop_state/ retained read-only as project history
```

Per-directory ownership is one paragraph in a `README.md` at each directory root;
that file is the only place allowed to narrate history (one link per relevant
loop_state record).

Two structural rules the tree encodes (details: REFERENCE_ARCHITECTURES.md §5.2):

- **Composition root.** Subsystem directories build as libraries with no
  SDL/GL/getenv; `app/host` is the only place that constructs modules and
  REGISTERS the render backend and platform I/O (input poll, audio sink,
  filesystem) into the runtime at startup. Nothing below `app/` reaches out to
  a platform singleton. *Informed by: N64ModernRuntime — ultramodern receives
  platform I/O via app-registered callbacks and the app registers the renderer.*
- **[REV 2026-09-22 D2] The subsystem-HLE rule replaces the engine/game seam.**
  There is no game-code directory: game knowledge enters the program in exactly
  three data forms — recompiled output (from `tools/recompiler`), SDK-library
  signature matches (from `tools/sce_lib_finder`, resolved to addresses at build
  time), and corpus/verification fixtures. Every HLE lives in the `hle/`
  subfolder of the PS2 subsystem it emulates and is bound by what it emulates
  (a library signature, a hardware surface, a timing observable) — never by a
  hand-pinned game address. Former address-bound patches are dispositioned
  one-by-one in `HOOK_RECLASSIFICATION.md`; the port-enabling seam is now the
  subsystem/`render/backends`/`platform` layering itself. *Informed by: OpenGOAL
  (the runtime implements the PS2 OS surface + devices, never per-game address
  patches) and RSDKv5/libultraship (ports touch platform/backends only).*
  **[REV2 2026-09-23 D4-D8] Amended, not reversed, by D5:** the BINDING rule
  stands verbatim (engine HLE never binds a game address). What changes is that
  the game-side *data* those three forms comprise — plus the A6-identified data
  halves of the MIXED clusters (VU1 program library, renderer profile, VRAM
  layout, timing quirks, STREAM.IRX recomp, title HLE models, corpus) — now has
  a directory with a linker-enforced boundary: `games/<title>/`, registered into
  engine interfaces at boot (§14). Title HLE models (loader spin, budget
  parameters) live in the title module but still bind through the same three
  channels; the engine mechanism they parameterize (cdvd completion model,
  DerivedClocks.budget_anchor) stays in `engine/`. "One game = one module" is
  what makes the D2 rule testable: `lint_engine_purity` fails any game token in
  `engine/` (§13), and the [GD1] empty-profile contract guarantees a title with
  NO library and NO renderer profile still renders correctly (slowly) through
  the interpreter + generic DrawBatch path.
- **[REV2 2026-09-23 D4-D8] One class per file (D4).** Every class or
  struct-with-behaviour gets its own `.h`/`.cpp` pair named after it (snake_case
  file = PascalCase type); free-function groups get a concept-named pair; NO
  textual `.inc` includes for hand-written code; headers are declarations only.
  Full rule, exceptions, and the CI lint: §13.

---

## 4. Core data structures (field-level sketches)

Sketches, not headers: field lists convey ownership and intent. `//` notes name the
audit invariant or record each field answers to.

### 4.1 GsRegisters (`gs/state/`)

Pure register file — no caching, no GL, no epochs. PCSX2's `GSState::m_env` analogue.

```
GsRegisters
├── prim: PRIM            // + PRMODE/PRMODECONT selection
├── env:                  // global drawing environment
│   ├── texa   {ta0, aem, ta1}
│   ├── fogcol {r,g,b}
│   ├── dthe, dimx[4][4]              // dither — folded into merge keys [WN4-CT16Q]
│   ├── colclamp, pabe, scanmsk
│   └── texflush_seen                 // observation only
├── ctx[2]:               // dual context, selected by PRIM.CTXT
│   ├── frame {fbp, fbw, psm, fbmsk}
│   ├── zbuf  {zbp, psm, zmsk}
│   ├── xyoffset {ofx, ofy}           // 12.4 fixed
│   ├── scissor  {scax0..scay1}
│   ├── test  {ate, atst, aref, afail, date, datm, zte, ztst}
│   ├── tex0  {tbp0, tbw, psm, tw, th, tcc, tfx, cbp, cpsm, csm, csa, cld}
│   ├── tex1  {lcm, mxl, mmag, mmin, mtba, l, k}
│   ├── miptbp1, miptbp2              // recorded into DrawIR (fixes .gsdl mip gap, A5 §2.4)
│   ├── clamp {wms, wmt, minu, maxu, minv, maxv}
│   ├── alpha {a, b, c, d, fix}
│   └── fba
├── xfer:                 // transfer latch — snapshots at TRXDIR kick [R199-FIX1]
│   ├── bitbltbuf {sbp, sbw, spsm, dbp, dbw, dpsm}
│   ├── trxpos {ssax, ssay, dsax, dsay, dir}
│   ├── trxreg {rrw, rrh}
│   └── stage  {active, dir, cursor_x, cursor_y}
├── clut_state {dirty, loaded_cbp, cached_words[]}   // CSM1 load tracking (PCSX2 GSClut analogue)
└── priv:                 // privileged regs, separate write path
    ├── pmode, smode1, smode2, bgcolor, csr, imr, busdir
    └── dispfb[2], display[2], extbuf, extdata, extwrite
```

Vertex assembly state (`vertex_queue`, `draw_buffer`, kick latches incl. the
first-kick TME snapshot [BATCH-STATE-SNAPSHOT] and roundSprite normalization)
lives beside it in `gs/state/vertex_kick.*` and is savestate-serialized (GSST v3).

### 4.2 PixelFormat / PsmLayout (`gs/format/`)

One descriptor table replaces the four parallel families (A3 §2: decoder tables,
gs_state write-side, backend encode-side, census PagedGeom) and the scattered bpp
switches. Modeled on PCSX2 `GSLocalMemory::m_psm[]`.

```
enum class Psm : u8 { CT32, CT24, CT16, CT16S, T8, T4, T8H, T4HL, T4HH,
                      Z32, Z24, Z16, Z16S };

PsmLayout                                  // constexpr table, one row per Psm
├── bits_per_pixel: u8                     // 32/24/16/8/4
├── page_w, page_h: u16                    // CT32-class 64×32, CT16-class 64×64, T8 128×64, T4 128×128
├── block_w, block_h: u16
├── column_h: u8
├── block_table: const u8 (*)[..]          // CT32/Z32/CT16/Z16/Z16S/T8/T4 layouts (texture_decoder canonical)
├── pixel_addr:  u32 (*)(x, y, bp, bw)     // GetAddressPSM* family — the ONLY address math
├── pixels_per_write: u8                   // replaces gs_state.cpp:5066/5332 switches
├── is_z: bool                             // Z reads/writes MUST use Z rows — co-page corruption contract
├── is_indexed: bool; index_shift: u8      // T8/T4/T8H/T4HL/T4HH
└── alias_class: u8                        // 32-class / 16-class / 24 — drives shuffle legality
```

Companions in the same directory: `clut.h` (ONE `Csm1Swizzle`, `ReadClut32/16`),
`texa.h` (applied to **both** CT24 and CT16 — the CT16 TEXA gap is a documented
accepted-difference candidate, EQUIVALENCE_PLAN §6), `expand.h` (bit-replicate
5551→8888, the settled convention), `alpha.h` (0–128→0–255 domain).

### 4.3 VramImage + residency (`gs/vram/`)

The single-writer generalization of `gsovl` (A3 §4): one page record replaces five
trackers.

```
VramImage
├── bytes[4 MiB]                           // the CPU mirror — LAZY for GPU-authored pages (by design)
├── pages[512]: PageRecord
│   ├── author: Cold | Cpu | GpuTarget(fbp)     // gsresident's author, unified
│   ├── upload_epoch: u64                       // EE-stream writes; re-stamped write-exact [M1A-EPOCH]
│   ├── deposit_epoch: u64                      // GPU deposit generation [SH1-DEPEPOCH] — separate counter family
│   ├── state: Clean | Encoding | Pending | Applying   // gsovl commit machine; bracket [M1A-DEPBRACKET]
│   ├── write_bits: u64                          // precise sub-page write bits [CX8]
│   ├── dirty_band: {y0, y1}                     // partial-page deposit band
│   └── reader_flag: bool                        // census-seeded read-hot marker
├── shadow[4 MiB lazy]                     // GFX-thread deposit staging (gsovl SHADOW)
└── epoch_counters {upload, deposit}
API (the only doors):
├── WriteStream(span, bytes)               // EE thread ONLY — uploads, local↔local, HLE feeds, savestate restore
├── StageDeposit(span, bytes, ee_epoch)    // GFX thread — into shadow, tagged with the batch's EE epoch
├── CommitDeposits(boundary)               // EE thread — per-page last-writer arbitration:
│                                          //   upload_epoch ≤ deposit_epoch ⇒ commit, else drop [TEXPOP-FIX][VRAM-EPOCH]
├── ReadView(span) → composed bytes        // mirror + pending shadow (ComposeRead) — CPU readers NEVER touch bytes[] raw
├── MaterializePages(span, reason)         // demand flush; reason ∈ {Decode, Serve, Refresh, PreDraw, Scanout, Destroy, Save}
└── AssertNoStaleAtSave()                  // [GB4]: save-time zero-stale invariant, kept as a runtime check + test
```

### 4.4 RenderTarget (`gs/hle/targets/`) [REV 2026-09-22 D3]

The 75-field `TargetFBO` split into four structs with distinct lifetimes (A3 §10.8).
PCSX2 `GSTextureCache::Target` analogue, backend-agnostic (surfaces are handles).

```
RenderTarget
├── identity: TargetIdentity
│   ├── fbp; fbw; render_psm                    // last FRAME PSM — retag keeps content [R102]
│   ├── logical_w, logical_h                    // grow-only; scissor flips never destroy [R102]
│   ├── scale: u8                               // fixed per-fbp at creation [GE1]; native pins (0x230000, alias dests [S83])
│   └── class_bits {was_32, was_16, has_32bit_content}   // sticky; survive grow rebuild [R113 carry]
├── views: SurfaceViews                          // backend surface HANDLES, not GL ids
│   ├── color: SurfaceId; depth: SurfaceId      // depth is a texture (sampleable) [R162-ZTEX]
│   ├── alt16: SurfaceId?                       // 16-bit companion [R105-ALT16]
│   ├── alt16_stale: Rect                       // main→companion coherence rect [R113]
│   ├── resolve: {color, depth} SurfaceId?      // native-res pair, scale>1 only [GE1]; freshness seqs
│   └── persistence: SurfaceId?                 // sw-blend rt_copy source
├── readback: ReadbackEngine
│   ├── staging rings + fences (color, z)       // PBO analogue behind backend caps
│   ├── drawn_since_sync: Rect                  // color writebacks clamp to it [R104]
│   ├── z_drawn_since_sync: Rect                // [R111]
│   └── keep_mask16: u16                        // masked 5551 writeback preservation [MASKED-SYNC]
└── coherence: CoherenceState
    ├── vram_coherent_epoch, vram_coherent_class     // voucher for idx serves [R173-IDXFRESH]
    ├── lane_author_seq, surface_author_seq         // fmt=5 lane serve predicate [S107-FOGLANE-LIVE]
    ├── last_draw_gfx_epoch                          // serve-keep predicate input [S32-RTKEEP]
    └── needs_frame_clear, ever_composited           // per-game-frame clear keyed on composite consumption (R93)
```

### 4.5 TextureSource (`gs/hle/texsrc/`) [REV 2026-09-22 D3]

The output of the ONE resolver. The A3 §3 rule — *a read returns stream-ordered
bytes in the read's view geometry* — becomes one function with an exhaustive match;
today's 11 serve routes + 3 rescues + 2 caches are its cases.

```
ResolveRead(view: TexReadView, targets, vram) → TextureSource
TexReadView  {tbp, tbw, psm, tw, th, region?, clut{cbp,cpsm,csm,csa}, texa, clamp, mip chain}
TextureSource
├── owner: one of
│   ├── Surface {target_fbp, which: Color|Resolve|Alt16|Depth|Persistence, fmt_case: 0..6}
│   │        // fmt cases keep the SH5-BIND route names: COLORTEX/RESOLVE/ALT16/DEPTH(1,2,3)/IDX(4,5,6)
│   └── Decoded {content_key, cache_tier: L1|L2, bytes}       // address-free key [R125-L2FIX]
├── transform:                                    // backends never re-derive aliasing math
│   ├── shuffle: None|16to32|32to16|DepthToColor|ChannelShuffle (+ uv_scale_u)
│   ├── uv_prescale {w, h}                        // read-view prescale [S111-READVIEW]
│   ├── z_row_offset: u16                         // fmt=3 zoff (0 or 224) — set EVERY draw [W3]
│   ├── uv_native: bool                           // requantize to native centers [S103-UVNATIVE]
│   └── idx_lane: Base|Partner?                   // fmt=5 pair partner at +h·fbw·2
├── freshness: {upload_epoch, deposit_epoch, author}   // the proof the serve is legal [S109][CX8]
├── clut: ClutSourceId?                            // prefolded 256×1, content-hash keyed, kick-snapshot sourced
└── declined: Reason?                              // decline ⇒ rescue deposit happened before Decoded fallback [S109(i,ii,iii)]
Per-call outputs are fields of this struct — the m_ftt_* loose-member idiom is
abolished; reset-per-entry [R163-ZTEX2 W2] holds by construction.
```

### 4.6 DrawIR record (`gs/hle/ir/`) [REV 2026-09-22 D3]

The `.gsdl` DrawRec v2 promoted from recording format to the in-flight IR
(A3 §6/§10.7). Backends, the recorder, the replayer and interp all consume the same
record. NO graphics-API types anywhere; per-backend staging lives in sidecars keyed
by `seq`.

```
DrawIR
├── seq: u64; src_frame: u32
├── identity {hle_handler, image_hash, frame_inst, kick_pc, kick_ord, class}   // [GSDL-ID] verbatim
├── geometry
│   ├── topology: Point|Line|LineStrip|Tri|TriStrip|TriFan|Sprite
│   ├── verts: span into flat pool of VertexRec {x,y (12.4), z, s,t,q, u,v, rgba8, fog8, kick}
│   ├── xyoffset {ofx, ofy}; ctx: 0|1
├── raster                                         // pure GS values
│   ├── frame {fbp,fbw,psm,fbmsk}; zbuf {zbp,psm,zmsk}
│   ├── test {...}; alpha {a,b,c,d,fix}; scissor
│   ├── colclamp, pabe, fba, scanmsk, fogcol
│   └── dthe, dimx                                 // NOW recorded (closes .gsdl dither gap)
├── tex_view: TexReadView?                         // §4.5 input — a READ VIEW, not a decode
│   └── + miptbp1/2, tex1 mip fields               // NOW recorded (closes .gsdl mip gap)
├── payload_refs {vram_window_hash → side pool, clut_hash → side pool}
│   // side pools carry BYTES for CLUT + read windows in capture mode (closes the [SH5] T8H CLUT replay gap)
└── resolved: TextureSource?                       // filled by resolver before backend dispatch
Non-draw jobs (JobRec): Upload, StateChange, Dispfb/Display, VSync, Invalidate,
SyncAllRTs — unchanged from .gsdl v2.
Invariant: applying DrawIR ≡ replaying the register ops store-for-store (G-C1
verifier kept as test + optional tripwire).
```

### 4.7 TimeDomain (`ee/timing/`)

A4 §5.4's consolidation, verbatim in structure:

```
TickSource                       // the ONE generator (thread or timer callback)
├── period_us (16667 / K)        // turbo = scalar K policy; warp/freerun = release-trigger policies
├── mode: Realtime | Freerun | Turbo(K) | Warp(to_frame)
└── tick classification census (consumed/vsync/timeout/cdvd) — diagnostic only
EmuDomain                        // today's timedom, semantics unchanged [S94-TIMEDOM]
├── emu_count: u64               // THE game-visible counter; DomainTick() is the sole read API
├── regime: Detector{...} | Pinned(n)      // deterministic profile pins it
├── lead_governor {max_ahead=1}; ReAnchor(boot/restore)
DerivedClocks                    // pure functions of EmuDomain
├── timers[4] {count, mode, comp, hold; counts_per_tick; subtick interpolation as presentation}
├── budget_anchor (FUN_00191bd0 emulation base)   // [S96-BUDGETEMU]
├── heartbeat (STREAM.IRX 60 Hz), cdvd_refill_epoch, cop0_count (free-run model added)
BoundaryProtocol                 // the single frame-boundary sequence, written ONCE:
└── NoteArrival → pace park (depth bound) → tick wait (quantum) → OnBoundary settle
    → timer pump → ISR dispatch
PresentPolicy (gs/hle/present)   // decimation + FMV bypass + injected CRTC fallback;
                                 // consumes EmuDomain stamps; can never influence loop admission
```

Surviving knobs: `time.regime_pin`, `turbo`, `load_turbo`, `fps_cap`, `ee_runahead`.
Everything else (TIME_EMU/T0_SUBTICK/HB_TICKCLOCK/BUDGET_EMU/QUANTUM) becomes the
one shipped mode.

### 4.8 Config (`core/config/`)

One schema, one loader, no getenv anywhere else.

Field-count reconciliation (canonical across A2/MIGRATION/this doc): the base
number is **27** — A2's 29 keep-as-setting registry rows minus the two S30 probes
(PS2_HEADWATCH, PS2_EULERTRACE) that reclassify to the diagnostics build. A2 §6's
enumerated ini schema also totals 27 fields (9+2+2+2+5+4+3 across its seven
sections) — family merges (PS2_VU1_HLE+PS2_VU_MODE → `vu1_mode`) offset the
non-env surfaces it absorbs (display.ini's 5 keys, GUI volume/device/kbm_pad).
The target schema below = those 27, **minus** `recomp.vu_aot` (AOT tier deleted,
A1 §5.4), **minus** `recomp.overrides_file` ([REV 2026-09-22 D1] the
recomp_overrides.ini pin channel retires with the legacy engine — MIGRATION
Phase 2), **plus** 7 refactor-introduced fields — `video.backend`,
`dev.log_categories`, `engine.{time_regime_pin, turbo, ee_runahead}` (§4.7 /
A4 §5.4's surviving knobs), `meta.{profile, source_fingerprint}` — ⇒ **32
fields**. MIGRATION_PLAN §3 step 5's "27 effective" is this same base number.
[REV3 2026-09-23 D9] The `triage` section adds 7 fields and absorbs
`dev.slowspot_capture` (the slowspot watchdog becomes core/triage/PerfMonitor,
TRIAGE_CAPTURE.md §3) ⇒ the working total is **38 fields**.

```
Config                              // immutable after Load(); passed by const&
├── video  {scale:1..4, interp:bool, vsync:bool, fps_cap:int, fit_mode, aspect,
│           custom_scale_pct, custom_offset_x/y, backend: gl|vulkan|soft|gxm}
├── audio  {enabled, volume}
├── input  {device, kbm_pad}                       // + bindings file
├── loads  {instant_cdvd, load_turbo}
├── engine {vu1_mode: hle|cached|interp, mtvu:bool, mtvu_depth, pacing: realtime|freerun,
│           time_regime_pin:int?, turbo:int, ee_runahead:int, shutdown_wait_s}
├── dev    {gui_open, perf_hud, log_level, log_categories[], slowspot_capture}
│                                   // [REV3 2026-09-23 D9] slowspot_capture retires INTO triage.* below
├── triage {enabled, fallbacks, perf, spikes, fps_target, disk_budget_mb, ring_period_s}
│                                   // [REV3 D9] play-time capture (TRIAGE_CAPTURE.md §3); 7 fields, −1 for
│                                   //   dev.slowspot_capture ⇒ schema total 38
├── recomp {warp_to}                               // [REV 2026-09-22 D1] overrides_file deleted with the pin channel
└── meta   {profile: default|deterministic, source_fingerprint}
Sources & precedence: defaults < ps2r.ini < CLI --set key=value < (dev builds only)
PS2R_SET env blob for harness use. Validation encodes the real dependency DAG
explicitly (freerun ⇒ instant_cdvd default, mtvu_depth needs mtvu) — replacing the
NOVSYNC→FREERUN→CDVD_INSTANT hidden cascade (A2 finding 3).
`profile=deterministic` = the EQUIVALENCE_PLAN §3 pin set, as one named preset.
```

### 4.9 SdkBindingManifest (`tools/sce_lib_finder` output → per-subsystem `hle/`) [REV 2026-09-22 D2]

Replaces the former HleHookTable design. There is **no address-keyed hook
registry**: the ~200 main.cpp dual-hooks (grep 2026-09-22: 200 live
`install_dual_hook(` call sites + definition at main.cpp:464 — the
authoritative count; A4 counted 191, A2's 208 includes non-call mentions, and A1
§7's 172 counts direct `recompiled_functions[` map mutations in main.cpp — a
different grep pattern over the same population, re-verified 2026-09-22
[COVERAGE-FIX]) + 235 manual installs + sce bindings walk + libc/sif/memory
scatter (A1 §7.7, A4 §0.1) are dispositioned in
`docs/design/HOOK_RECLASSIFICATION.md` ([REV-2 2026-09-23 recount]): 299 rows
become signature-bound SDK/OS HLE, 2 become hardware/timing HLE models, 250 are
emulation-bug workarounds
retired at their root fixes, 1 is retail behavior left unpatched, and 195 are
diagnostics that delete.

What remains is ONE declarative manifest for the SDK/OS HLE class:

```
SdkBinding
├── library: LibId                                 // libpad, libmc, libgraph, libcdvd, libmpeg, libsdr,
│                                                  //   libkernl, libdma, libc, sceSif, …
├── signature: SigId                               // sce_lib_finder signature identity in the SDK 3.0.1 drop
│                                                  //   (crash_matches_full lineage) — THE KEY. Never an authored address
├── addr: u32                                      // DERIVED at build time from the signature match;
│                                                  //   out-of-band matches (game code) are rejected, as the
│                                                  //   generated bindings file already does today
├── kind: SdkBind | SdkWrap                        // Wrap receives the original (the guarded-libc shape [R138])
├── fn: void(*)(CpuContext&)                       // handler, lives in the owning subsystem's hle/
├── name: const char*                              // "sceCdReadClock", "memcpy"
├── provenance: RecordId                           // one loop_state/doc link; no prose in code
├── phase: Boot | PostElf | PostIopReboot          // install ordering, explicit
└── diag_only: bool                                // compiled out of Release with core/diag
SdkBindingManifest = span<SdkBinding>, assembled from per-subsystem contribution
tables (pad/hle, mc/hle, gs/hle/sce, cdvd/hle, ipu/hle, sound/hle, dmac/hle,
sif/hle, ee/hle); ONE installer walks it through the frozen update_function_impl
seam (pointer/ref stability per the R137 contract, A4 §1.3) and calls
recomp_mark_hooked. Content ([REV-2 2026-09-23]): the 291 sce registry rows +
4 libc + the 3 seam-only sce addresses (sceDmaSync 0x2c1030, sceGsResetPath
0x2c1ab0, sceGsSyncPath 0x2c1b50 — the generator's 5 other SDK-band seam
addresses are already registry rows and collapse into them; the former "2 sif"
entries were miscounted: sceSif handler bodies ride the 291, and the 0x181490
piracy bypass is workaround-class, never a binding) = **298 address-keyed
rows** + the boot-environment writer (S8-ARGV, not address-keyed) —
HOOK_RECLASSIFICATION §2/§3.5.

The former Replace/Wrap/GameFix/ManualBody kinds are gone: GameFix rows were
reclassified (workarounds retire at their root fixes; the budget hook is an
ee/timing model, not a binding); ManualBody rows retire with the v2-only engine
(D1, MIGRATION Phase 2). The equivalence harness adjudicates each former
behavioral patch during retirement (HOOK_RECLASSIFICATION §4). The ~185+ DIAG
probes do not migrate; the probe MECHANISM lives in core/diag.

kind-adjacent: GenProbe (Informed by: XenonRecomp mid-asm hooks) survives as a
GENERATION-TIME probe channel in a separate diagnostics manifest — entries name
a probe function at an exact guest instruction; tools/recompiler inserts the
call during emission and the probe body links from core/diag. Exists ONLY in
PS2R_DIAGNOSTICS builds; the Release generator emits nothing. This replaces the
ad-hoc PS2_SHADOW_DIAG / PS2_EE_RAWDISP injections (A2 §7.4) with declared,
reproducible instrumentation — generated code is never hand-edited and never
env-gated. Guest-address keys are permitted HERE and only here, because a probe
is not behavior: it observes, it never replaces.
```

### 4.10 VuProgramLibrary entry (`vu/hle/`)

Contract from `hle/vu1_hle.h` kept verbatim; registration becomes data.

```
VuHleProgram
├── name; family: ttx|pcs|lit|crys|skn|idx|zk|spr
├── image_hash: u64            // whole-image content hash (exact-image match tier)
├── variant_hash: u64          // MPG-patched per-material subroutine family tier
├── start_pc: u16              // 0 = dispatcher entry, 0x1748 = direct entry, ...
├── fn: Outcome(*)(const VuRunInput&, VuRunOutput&)   // may DECLINE → fallback tier
└── proven_by: RecordId + corpus coverage note        // vu1_replay --hle FAIL=0 evidence
VuRunInput  {micro[16K], data[16K], start_pc, top/tops/itop/itops, VUregs, image_hash}
VuRunOutput {ordered GIF kicks (or typed DrawIR runs), data_writes[], regs? (has_regs)}
Registry: constexpr table in registry.cpp (replaces static-init s_reg_* booleans);
lookup exact-image → variant, start_pc pinned; --catalog reads the same table.
```

[REV2 2026-09-23 D4-D8] Ownership and the family signature move per D5 — the
registry/dispatch MECHANISM stays `engine/vu/hle/`; the CONTENT (programs,
entries, hashes, helpers) becomes `games/<title>/vu1_library/`, and the five
game constants A6 §2.2 found inside the mechanism are extracted into a
per-title signature struct. The full interface (engine dispatcher looks up
image hash → per-game entry {hash, name, native fn, oracle case ids}; engine
ships only mechanism + cached interpreter): §14.2.

### 4.11 Savestate envelope (`core/savestate/`)

Format PS2SAVE1 v2 preserved bit-for-bit (reader compatibility with the ~40 anchor
states is a hard constraint, A5 §7.3.8).

```
Envelope   {magic "PS2SAVE1", version, section_count, vsync_frame, wall_ms,
            SectionHeader{fourcc, version, flags(REQUIRED), crc32, len} × N, trailer CRC}
SectionSpec (the NEW part — a declared registry instead of ~30 free functions)
├── fourcc; version; required: bool
├── serialize(Writer&), restore(Reader&), validate() 
└── owner_module                                   // declared BESIDE the module state it saves
Quiescence protocol unchanged: save only at a boundary with MTVU + render queue
drained, RTs materialized (AssertNoStaleAtSave), DMA idle, FMV idle.
Cross-engine contract unchanged: R5 (host stack carries no PS2 state), shadow call
chain push/pop, recomp_fn_id_for containment of resume addresses (A4 §6).
```

---

## 5. IR design — MIPS → IR → targets

[REV 2026-09-22 D1] **The refactor targets ONLY the v2 recompiler engine.** The
v2 pipeline is the in-repo, no-Ghidra static discovery + emission engine: own
discovery of 8,819 functions and 177 jump-table sites (Ghidra-comparison
verified, every miss classified absorbed-or-true-gap — RECOMP_P2_IMPL.md /
RECOMP_P2_COMPARISON.md), general emission through the proven translation core,
oracle-verified (fixtures + 50-function samples + all 18 legacy-divergence
findings, RECOMP_P3A_IMPL.md), corpus-as-audit GAPS=0 over 1,642 runtime
jr/jalr pairs with the standing RecompCorpusAudit ctest gate, and a pure-
discovery no-Ghidra build that boots (REFACTOR_COMPLETE.md, RECOMP_P4_IMPL.md).
Retired with it (MIGRATION Phase 2, proof-gated): the legacy CLI and its
emission special-case chain, the Ghidra extracted_functions.json input, the
recomp_overrides.ini pin/pin-closure channel, and switch_manual_enumeration.json.

**What v2 still needs by hand — precisely:** nothing at emission time for game
code. All 143 manual-dispatcher-enumeration functions and FUN_00177168's former
1,490-line hand emitter are general-emitted by v2 and oracle-verified
(REFACTOR_COMPLETE.md: "no longer required by the v2 path"). Two hand surfaces
remain in the TREE today and are retired by the Phase 2 gate, not kept: (a) the
engine-independent runtime manual-body layer — 235 installs
(222 fun_*_body.inc + 3 hand dispatchers + 10 sibling installs, all 143
enumeration addresses ⊂ the 235) still replaces map-dispatched bodies even in
v2 builds (main.cpp:5177 installs unconditionally) — each address is
harness-adjudicated v2-body-vs-manual-body and deleted
(HOOK_RECLASSIFICATION §3.2); (b) the two v2-correlated open defects (P4-F1
stream-restart spin; P4FIX-F1 cold-boot [FATAL-NOJUMP] pc=0) are emulation bugs
of the v2-loses-a-race timing family (REFACTOR_COMPLETE.md limitations 2-3) —
they are root-caused and fixed in Phase 2, never papered over with pins or hand
bodies. What v2 keeps needing as INPUT: the ELF and the corpus pair fixtures;
per-game knowledge stays data.

Today the recompiler translates MIPS directly to C++ text inside a 12,784-line
function (A1 §3.1). The target design inserts a small typed IR between decode and
emission — not to optimize, but to make the semantics contract (recompiler_tool/
CONVENTIONS.md, A4 §1.3) machine-checkable and to open a second emission target
later (denser code for Vita-class, per-shard incremental codegen).

### 5.1 What stays SEMANTIC (IR node properties, from the proven contract)

- **Delay slots** are explicit: a terminator node carries its slot node, sequenced
  before the transfer (DS1–DS7); branch-likely lowers to a guarded slot on the taken
  path; branch compares are 64-bit; slot words come from the .text image when block
  metadata splits them.
- **Register model**: 128-bit GPRs; 32-bit results sign-extend into the low double;
  $zero never written; HI/LO vs HI1/LO1 pipes distinct; **no cross-site register
  caching** — an IR may cache within a block only if it provably spills at every
  call/dispatch/label boundary (default: it doesn't).
- **PS2 float** ops are typed: clamp is an explicit node (`Ps2Clamp`), div/sqrt/rsqrt
  carry a rounding-mode tag (FE_TONEAREST inside FE_TOWARDZERO ambient) and are
  single-precision by construction — so an ARM backend can batch FPCR writes (B8).
- **COP2 / VU0 macro** ops are first-class typed nodes (dest-mask, broadcast, Q/ACC,
  interlocks, VCALLMS) sharing the clamp semantics.
- **Syscall** lowers to one node → `runtime_syscall_dispatcher($v1, ctx)`; mtc0/ei/di/
  sync/eret are nodes with named runtime handlers.
- **Frozen hook surfaces** are emission OBLIGATIONS attached to function nodes:
  `update_function_impl` value-replacement stability, shadow call-stack push/pop
  wrappers, `recomp_fn_id_for` dense ids, mid-entry dispatch, and the
  `find_containing_function` fallback. Savestates depend on these (A4 §6) — the IR
  verifier checks each obligation is emitted.
- **Determinism**: identical inputs ⇒ byte-identical output (regen-determinism gates
  kept).

### 5.2 Emission changes (behavior-neutral, portability-driven)

- **Bounded host stack**: guest calls emit as tail-dispatch through a trampoline loop
  (return-to-dispatcher) instead of native C++ recursion, capping host stack depth at
  park sites — this deletes the 1 GB-stack requirement (blocker B1) without changing
  the R5 state contract (host stack carries no PS2 state *already*).
- **Explicit entry tables** replace the `uint32_t addr = 0` default argument on 9,327
  functions; mid-entry stays as the switch header.
- **Flat dispatch**: fn-pointer arrays + binary-searched ranges replace
  `std::map<u32, std::function>` (B10; also a desktop win).
- **Sharding is a first-class output parameter** (compile cost history: 2,068 s /
  2.6 GB for one TU — A4 §1.1); shard size budget §11.
- **No diagnostics in generated code**: the PS2_SHADOW_DIAG / PS2_EE_RAWDISP
  injections are removed at the generator (A2 §7.4); probes belong to core/diag via
  hooks — in diagnostics builds, via the declared `GenProbe` manifest channel
  (§4.9), never via env-gated emission.
- **[REV 2026-09-22 D1/D2] No override lanes.** The previously sketched
  link-time weak-alias override lane (N64Recomp patch mode / XenonRecomp weak
  aliases) is dropped: its two consumers — hand-written manual bodies and
  behavioral GameFix hooks — no longer exist (D1 retires the bodies,
  HOOK_RECLASSIFICATION retires the hooks). The runtime
  `update_function_impl` seam remains (savestates and SdkBinding installs need
  it) and is the ONLY function-replacement mechanism.
- **Optimizations are staged, never bundled** (Informed by: XenonRecomp — its
  register-localization flags are documented as "enable only after a working
  recompilation", with quantified wins): any future IR optimization (register
  localization within provably-spilling regions, dead-flag elision) ships as an
  off-by-default emitter flag that must pass the full oracle family and the
  differential harness before it may become default. Behavior changes and
  optimization changes never share a commit.

### 5.3 How the two "other IRs" fit

Three IRs, three layers, deliberately separate:

| IR | Producer | Consumer | Identity | Proof |
|---|---|---|---|---|
| **EE code IR** (§5.1) | tools/recompiler lifter | C++ emitter (today); future direct emitters | function/leader addresses | recompiler-v2 oracle + enforcers |
| **VU1 HLE library** (§4.10) | hand-lifted programs | vu/hle runtime → emits DrawIR or raw kicks | whole-image content hash + variant hash | vu1_replay --hle over 3,481 cases |
| **GS DrawIR** (§4.6) | gs/state flush (ONE path) | all backends + recorder + replayer + interp | [GSDL-ID] stamps | G-C1 verifier + corpus replay |

VU1 HLE is the bridge: a registered program skips micro execution and emits typed
DrawIR directly (today's [GS-DRAWIR] tier, default ON); unregistered images fall to
the cached interpreter whose XGKICKs are parsed into the same DrawIR by the GIF
layer. Either way the backend sees ONE input type.

The tier ladder is the permanent shape, not scaffolding (Informed by: OpenGOAL's
tfrag1→tfrag3 bring-up — an exact, slow reference consumer of the VU program's
XGKICK output came first, the native per-family renderer second, and the
reference tier stayed alive for debugging). Consequences we encode: (a) the
instrumented VU interpreter is never deleted — it is the reference every HLE
program was proven against and the fallback for unregistered images; (b) every
native class renderer keeps a DECLINE path to the generic DrawIR consumer;
(c) OpenGOAL's stage-3 move (offline preprocessing of level geometry into
GPU-friendly buffers) is noted as a possible future enhancement layer above
DrawIR, out of scope for the refactor.

Considered and rejected — static recompilation of VU1 microcode: the N64Recomp
family recompiles RSP microcode offline (RSPRecomp; Zelda64Recomp ships a
recompiled DSP microcode config), the closest analogue to compiling our VU1
programs to native code. We built exactly this (the VU-AOT tier) and measured it:
233 MB generated + 351 MB lib, 2,068 s single-TU compiles, no correctness win
over the cached interpreter + HLE library (A1 §5.4, A4 §3.2, NOAOT record). The
HLE-program + reference-interpreter ladder replaces it; this note exists so the
alternative is not re-litigated without new evidence.

## 6. Backend abstraction (`render/backend.h`)

Dolphin's unification lesson applied: every coherence decision (targets, texture
source, VRAM, scanout policy) lives ABOVE the interface; a backend is an API
adapter. PCSX2's GSDevice and RPCS3's GL/VK GSRender pairs confirm the shape.

```
IRenderBackend (implemented by render/backends/{gl, soft, vulkan, gxm})
├── Caps QueryCaps()            // {max_tex, compute:bool, geometry_tier, staging:bool,
│                               //  reverse_subtract:bool, texture_barrier:bool,
│                               //  depth_sample:bool, ...}   // depth-as-texture reads
├── SurfaceId CreateSurface(desc {w, h, format: RGBA8|D32F|R8, renderable, scale})
├── void      DestroySurface(id)
├── void      Upload(id, rect, bytes, stride)
├── void      Draw(const DrawIR&, const TextureSource&, SurfaceId color, SurfaceId depth)
│             // includes blend map (A−B)×C≫7+D incl. REVERSE_SUBTRACT [R178], TEST/DATE,
│             // scissor, fbmsk emulation strategy chosen by caps
├── void      CopyRect(src, dst, transform)        // blits, alt16 pack/unpack, resolve pairs
├── void      Readback(id, rect, → cpu span)       // staging-ring async when caps.staging
├── void      Present(const ScanoutDesc&)          // composited by gs/hle/present, displayed here
├── FenceId   InsertFence(); bool Poll(FenceId)
└── void      BeginFrame()/EndFrame()
```

Per-backend notes:

- **GL 4.1 core** (first target; current code decomposed into it): no compute
  required — the compute-raster experiment does not survive; PBO staging behind
  caps.staging.
- **Software** (scalar reference): implements Draw per the GS pixel pipeline exactly;
  serves as (a) the oracle for backend-independent pixel tests, (b) the
  guaranteed-everywhere fallback. Byte-deterministic by construction.
  *Informed by: DuckStation and Dolphin, whose software renderers are
  first-class backends beside the hardware ones, not afterthoughts.*
- **Vulkan**: rebuilt later against this interface; the stale 2026-03 backend is
  deleted (A1 §5.4), not ported.
- **GXM (Vita)**: GLES2-class caps — no compute, limited formats, 128 MB VRAM
  budget, per-scene begin/end; feasible precisely because nothing above the
  interface assumes desktop GL (vitaGL demonstrates the GL→GXM translation exists
  if we ever want a shortcut).

**Pipeline/shader cache contract** (Informed by: XenosRecomp + Unleashed
Recompiled; forced by the Vita floor). The DrawIR raster-state space is finite:
blend func (A,B,C,D,FIX classes), TEST (ATE/ATST/DATE/Z), fbmsk strategy, texture
function, format case (§4.5 fmt 0..6), dither. Backends therefore treat pipeline
construction as a cacheable pure function of a DrawIR-derived key:

- Every backend keys pipelines/programs by a content hash of that state vector;
  the permutation list is ENUMERABLE offline by sweeping the gs_corpus (19
  captures, ~1.06 M draws cover the game's real state space).
- GL: link-time program cache warmed at boot from the enumerated list. Vulkan:
  VkPipelineCache persisted to disk + boot precompile of the corpus-derived set;
  compilation of a novel key happens on a worker during LOAD, never on the draw
  path (Unleashed Recompiled integrates pipeline compilation into asset
  streaming for exactly this reason). GXM: shaders precompiled at BUILD time
  where the toolchain allows — on-device compilation is documented as expensive
  enough that Vita ports ship compiler speedhacks (DaedalusX64-vitaGL).
- A cache miss during gameplay is a diagnostic event (counted, logged), because
  it means the corpus missed a state combination — feed it back into the
  enumeration list.

`caps.depth_sample` and its fallback: SurfaceViews' depth-as-texture reads
([R162-ZTEX], §4.4) assume the bound depth surface can be sampled — true on GL 4.1
/ Vulkan / the software backend, NOT assumable on GLES2/GXM-class hardware. When
`depth_sample` is false, gs/hle/texsrc resolves a Z-source read by `CopyRect`-ing
the depth surface into a color-format shadow surface (R32/packed16 per PSMZ) — or,
last resort, materializing the pages CPU-side through the VramImage mirror — before
issuing the sampling draw. The decision lives in ResolveRead (above the interface);
backends only report the capability.

## 7. Threading model — single writer per domain, no hidden globals

Thread census (target): main/UI, EE guest threads (bounded stacks, §5.2), GFX
consumer, TickSource, optional MTVU worker, ONE IOP host thread (the
cooperative kernel dispatcher — see § 7.1; [IOPK 2026-09-24] replaces the
per-IOP-guest-thread std::thread pool), audio callback, savestate writer.

Ownership table (the whole point — every mutable domain has exactly one writer):

| Domain | Single writer | Readers | Crossing mechanism |
|---|---|---|---|
| GsRegisters + vertex state | EE thread | — | DrawIR snapshot at flush |
| VramImage bytes + PageRecords | EE thread (CommitDeposits) | GFX via StageDeposit only | shadow + epoch arbitration (§4.3) |
| RenderQueue | EE pushes, GFX pops | — | mutex+condvar (as today) |
| RenderTarget cache | GFX thread | — | deposits back via StageDeposit |
| EmuDomain / clocks | TickSource advances, EE settles at boundary | all (read-only DomainTick) | atomics, one header |
| IOP kernel state (threads/semas/events/timers + IOP RAM during a pass) | the current dispatcher (IOP host thread under REALTIME; EE thread under FRAME) | EE glue via the dispatch guard | injection queue (iWakeup / sema signals / events) + dispatch lock |
| Config | Load() before threads | everyone | immutable const& |
| SdkBindingManifest | installer at boot phases | dispatch | frozen seam |
| Audio ring | mixer writes | device callback reads | SPSC ring |

Rules: **zero mutable globals in headers** (A1 found 184 across 23 headers — the
kernel thread map moves into ee/hle state owned by one TU); cross-thread state
is declared in one `threading.md` table per module; `_mm_pause`-class spin hints go
through a `platform/cpu_relax()` shim (B7).

### 7.1 IOP model (informed by OpenGOAL) [IOPK 2026-09-24]

The rule: **the IOP is native code under a cooperative kernel; never
emulated; the dispatch policy is the only host-dependent choice.**

- The Sony kernel surface (thbase/thsemap/thevent/thmsgbx/sifrpc/sifcmd) is
  a small cooperative scheduler (`engine/iop/iop_kernel.*`) over coroutines
  (`iop_coroutine.*`, the platform fiber backend the EE scheduler already
  uses). Guest IOP threads are coroutines run by ONE dispatcher at a time;
  they park at the same points the real single-core kernel switches
  (SleepThread / DelayThread / WaitSema / RpcLoop / the EE marshal).
- The game's custom IRX is native code — OpenGOAL hand-ports theirs
  (`game/overlord`), ours is statically recompiled (games/<title>/iop/
  STREAM.IRX) — bound to the kernel through the same stub surface as the
  stock HLE modules (`iop/hle/<module>/`).
- RPC is a memcpy plus a wakeup, in both directions: EE→IOP commands run on
  the module's OWN RPC-dispatch coroutine (the thread parked in
  sceSifRpcLoop — no thread-id impersonation); IOP→EE marshals park in
  Wait::Rpc with a deadline and are serviced at the EE's frame/syscall
  edges (`sif_rpc_bridge.*`).
- Deferred completions (CD, SPU voice-transfer) and the module heartbeat
  are kernel events/timers fired inside dispatch — the counted-queue
  contract (N schedules fire N callbacks, never inside the issuing pass).
- Dispatch policy (`iop.dispatch`): **realtime** (play default) = the ONE
  IOP host thread dispatches and condvar-sleeps until an EE wakeup or the
  next deadline (the OpenGOAL iop_runner loop; low audio latency, wall
  clock). **frame** (deterministic verification profile) = the EE thread
  dispatches at each frame boundary with a frame-derived virtual clock
  (discrete-event forced advances when every thread is time-parked) —
  guest-visible IOP scheduling becomes a pure function of the EE frame
  stream. Proven: paired profiled legs' frame+audio lanes bit-identical
  (PHASE0_IMPL.md), where the legacy free-running-worker hosting diverged
  at the first content present.
- Savestate: kernel thread/sema PODs serialize through the same IOPM
  section layout as the legacy hosting (cross-mode compatible); coroutine
  stacks are boot-symmetric (threads re-park at the same guest wait points
  on every boot) exactly as the legacy host threads were — the same
  documented gap, unchanged.

### 7.2 EE model — run token, EE-synchronous ticks, lockstep present [P0-EETOK / TICKSYNC 2026-09-25]

The rule: **one EE, one instruction stream — every guest-visible event
lands at a defined point of the EE's own execution; foreign host threads
never mutate guest state.**

- **Run token** (`ee/sched/ee_run_token.*`, hatch `PS2_EE_RUNTOKEN`
  default ON): guest EE threads keep their host threads (1 GB stacks,
  exception unwinding, resume machinery) but exactly one executes guest
  code at a time; the token changes hands only at kernel scheduling points
  (blocking syscalls, readying-syscall dispatch edges, hardware parks,
  park-loop pumps) under the thbase 1.02 election rule. Foreign producers
  (graphics thread, IOP dispatcher, SDL loop, audio) route wakes through
  `EeEventQueue`, drained only by the token holder (BUG-035). A savestate
  LOAD hands the loader's own identity off before relaunching and holds
  elections until every resumed thread is READY (`BeginRelaunch` /
  `EndRelaunch`, BUG-036) so the first post-load runner is the thbase
  winner, not the first pthread to start.
- **EE-synchronous ticks** (`ee/timing/tick_sync.*`, `PS2_TICK_SYNC`,
  deterministic profile): no free-running vblank ticker. A tick body
  (count increment, emu-domain carry, FIX-C hold-to-edge delivery, CRTC
  injector, sema 1/3 signals, INTC VBON/TIM2/VBOF, GS_CSR.VSINT) executes
  ONLY on a guest EE thread at a fixed point of its stream: exactly one per
  frame boundary (the "60 Hz PS2" quantum), one per delivering deferred-ISO
  pass, one per FMV pace demand; demands from non-guest host threads are
  deferred to the next boundary. The graphics thread raises no INTC.
- **Lockstep present** (`ee/timing/frame_rendezvous.*`): every EE-pushed
  VSync carries a process-local ticket acked by the graphics thread at
  every exit of its VSync case; the boundary parks (token released) until
  acked — no wall valve. Replaces the MAX_FRAME_LAG / 100 ms park. Play
  keeps the legacy park (the rendezvous costs the one-frame EE/gfx overlap:
  ~10-20 % fps_mean, PHASE0_IMPL §11.8); `time.tick_sync` is the Config
  field.
- **The frame boundary is a fixed sequence** (`ee/timing/
  boundary_sequencer.*`), identical at both boundary sites: 1 present
  rendezvous → 2 tick advance (+deferred ticks, EE_TIM0 pump) → 3 event
  queue drain → 4 maintenance duties + pad stamp → 5 IOP FRAME dispatch →
  6 EE stream RPC service → 7 savestate probe/save/load (a load stops the
  sequence) → 8 verification hooks. The rendezvous is first so every
  frame-keyed consumer in the sequence reads a settled present count; the
  IOP dispatch follows the maintenance deliveries so completions delivered
  at this edge are visible to this edge's dispatch. Result (PHASE0_IMPL
  §11): paired profiled legs' EE and IOP scheduler event streams are
  bit-identical and all 24 non-defect battery legs are ADMISSIBLE-STABLE
  across 3 reps on both binaries; the two known v2 defects became
  deterministic crashes.
- Ownership-table additions: `g_vblank_count` / tick bodies — writer: the
  EE boundary thread under `time.tick_sync` (TickSource thread otherwise);
  `RenderJob::lockstep_ticket` / `FrameRendezvous` — EE issues, GFX acks
  (mutex+condvar); `GS_CSR.VSINT` — EE thread in sync mode (was a gfx-thread
  write racing the EE's field toggle).


## 8. Configuration system

One schema (§4.8), one loader, one file. `getenv` is banned outside
`core/config/env_override.cpp` (dev builds) — enforced by a lint test (the same
enforcer pattern as the recompiler's probe-silent gate). The A2 disposition table
(496 vars → 29 settings) is the migration input; the schema is the destination.
Dependent settings are validated, not silently inherited. `--config`, `--set`,
`--profile deterministic` are the only override channels.

## 9. Logging and diagnostics as a module

`core/log`: `LOG(cat, level, ...)` with **compile-time categories** (~30, harvested
from the TagFilterBuf registry — VIF, GIF, GS, VU, DMAC, MEM, SIF, SND, PAD, MPEG,
KERNEL, LOADER, HEAP, PERF, ...), levels error/warn/info/narrate, three sinks
(file, stderr, crash ring). Disabled paths compile to nothing — no iostream
formatting ever runs for a dead category (the M4A lesson, structural this time).
Budget primitives (`first_n`, `every_nth`, `frame_window`) are library calls, not
hand-rolled statics. Replaces all four retrofit layers and the 6,869 raw
`g_logFile<<` sites (A2 §3).

`core/diag`: the durable tooling (A2 §4 keep-list): perf profiler, bench+report,
slowspot, bug_report+ring, drawlist recorder, vutrace collector, call census,
artdet, vram census, generic address-hook probe kit. Entire directory compiles out
of Release (`PS2R_DIAGNOSTICS=OFF`); the harness build turns it on. One-off
forensics (SEQ54 class) do not migrate at all. [REV3 2026-09-23 D9] Amendment:
slowspot, bug_report, and the artdet FIRE path graduate out of this keep-list
into `core/triage/` (§18) — they are product capture, present in ALL builds;
what stays diag-build here is the perf profiler internals, censuses, probe kit,
drawlist recorder and the bulk vutrace collector (TRIAGE_CAPTURE.md §12).

## 10. Testing strategy hooks

- Every module directory owns its unit/property/equivalence tests; the in-process
  A/B equivalence idiom (A5 §1.2) is the standing pattern for any fast-path.
- **Golden-emission snapshots** (Informed by: OpenGOAL's `_REF.gc` reference
  files, kept in-repo and diffed in CI): the emitted C++ for a fixed, curated
  function set (delay-slot shapes, jump tables, COP2 clusters, mid-entry cases)
  is frozen as reference files; any recompiler change that alters them shows up
  as a reviewable diff, not a behavior surprise. Complements the existing
  regen-determinism gates (those prove stability, snapshots prove *content*).
- The **invariant list (A3 §9, 38 rules)** becomes `docs/INVARIANTS.md`, each rule
  implemented as a named test in the owning module (tracked as a checklist).
- Replay tools (gs_replay, vu1_replay, vif_replay, drawlist_tool) build against
  the subsystem libraries only — they are the proof the layering holds (no
  SDL/GL for CPU-only modes).
- `verify/` hosts the differential harness (EQUIVALENCE_PLAN.md) as a first-class
  target; suite runtime is recorded per run (A5 §1.3 gap).
- Seams: the recorder tap stays at the DrawIR producer boundary; VUTRACE at
  VU1 run entry; frame-hash at present; audio hash at the mixer output ring;
  memory checkpoints at savestate quiescence points.

## 11. Naming conventions and size budgets

**Naming**

- Files name the CONCEPT: `texture_source.cpp`, `psm_layout.h`, `page_record.h` —
  never sessions (`s9_composite.h` → `gs/hle/classes/shadow_composite.*`).
- Classes/structs are nouns (`TextureSourceResolver`, `VramImage`, `TickSource`);
  functions are verbs (`ResolveRead`, `MaterializePages`, `CommitDeposits`).
- **No session tags** in identifiers, namespaces, or comments: `r120gl::` → `glstate::`,
  `r118::` → `texcache::`, `zk::` → its family name (mechanical rename — r120gl::
  appears on 691 .cpp/.h lines, 1,110 incl. .inc, and is load-bearing, A2 §5).
- Comment rules: comments state hardware invariants and cross-thread contracts
  ("why"), one line each, optionally ending with a RecordId link. Narrative,
  A/B numbers, "used to be", and `[TAG]` prose are banned; history lives in
  docs/loop_state (283 records) and per-directory READMEs. Enforced by a lint that
  rejects `\[[A-Z0-9-]+\]`-style tags in new code.
- **Divergence comments in OS-surface code** (Informed by: OpenGOAL, whose
  C-kernel/OVERLORD rule is "as close as possible to the game, and all
  differences should be noted with a comment"): in the per-subsystem `hle/`
  directories (`ee/hle`, `iop/hle`, `pad/hle`, `mc/hle`, `cdvd/hle`, …), any
  knowing departure from documented SDK 3.0.1 / hardware behavior
  carries a one-line `// diverges: <what> — <why>` comment with a RecordId.
  These are the layers where silent drift is most dangerous and hardest to
  bisect; the divergence census (grep `// diverges:`) becomes a review artifact.

**Size budgets** (CI-enforced; warn at soft, fail at hard)

| Thing | Soft | Hard |
|---|---|---|
| File | 1,000 lines | 1,500 (2,000 fail) |
| Function | 150 lines | 300 (400 fail) |
| Header mutable globals | 0 | 0 |
| `getenv` sites | core/config only | anything else fails |
| Public API per module | one `<module>.h` façade | — |
| Generated shard | 3 MB | 4 MB |
| Include depth into subsystems from render/backends/ | gs/hle/ir + render/backend.h only | — |
| Session-tag comments / namespaces | 0 in new tree | 0 |
| [REV2 D4] Primary types per `.h`/`.cpp` pair | 1 | >1 fails (`LintOneClass`, §13) |
| [REV2 D4] Textual `.inc` includes of hand-written code | 0 | 0 (generated exempt, §13) |
| [REV2 D4] Function definitions in headers (beyond trivial accessors/`.inl`) | 0 | 0 |
| [REV2 D5] Game tokens in `engine/` (A6 §1.2 regex set) | 0 | 0 (`LintEnginePurity`, §13) |

**Status (2026-10-06, P8.36-L):** the table is enforced by ctests under `tests/lint/`:
`LintSizeBudget` (file / function / generated-shard rows; WARN at soft, FAIL at the fail column —
2 pre-existing offenders are allowlisted by name in `tests/lint/budget_allowlist.txt` with a dated
reason and sit in the orchestrator's refactor queue: generated shard 028 at 4.30 MB, `verify/diff_legs.py`'s
521-line `selftest` (the composition root's former 2,118-line `EeBootstrap::Run` / 2,427-line
`ee_bootstrap.cpp` entries closed on 2026-10-08: the leg is fourteen phase functions in construction
order across `app/host/ee_bootstrap_*.cpp`, each under the 300-line hard budget, handing each other
`EeBootstrap::LegState` in `leg_state.h`); the shard row's MB is decimal — 1 MB = 1,000,000 bytes, soft 3,000,000 / hard 4,000,000 —
not MiB), `LintHeaderGlobals` (header mutable globals: 0 found across `.h .hpp .inl .inc`), `LintSessionTags`
(session tags in comments / namespaces / session-numbered string literals across the whole hand-written
tree incl. `tools/` and `verify/`: 0 new allowed; 39 pre-existing occurrences in 11 files are allowlisted
by literal — frozen report vocabulary, log-line prefixes pinned by tests, OLD tool output grammar,
FLAG_LEDGER flag names, GS-corpus capture names, plan case labels), plus the earlier `LintOneClass`,
`LintEnginePurity`, `CoreSingleGetenvSite`. An allowlisted offender that grows fails again; the
thresholds are never read from the allowlist. Not yet enforced: "Public API per module" and "Include depth
from render/backends/" (no lint; review items).

## 12. "Where is X" — the 12 audit concepts (A1 §7) in the new tree

[REV 2026-09-22 D2/D3]

| # | Concept | New home |
|---|---|---|
| 1 | Swizzle / PSM address math | `gs/format/psm_layout.*` |
| 2 | CLUT decode + CSM1 | `gs/format/clut.*` |
| 3 | Texture cache key | `gs/hle/texsrc/content_key.*` |
| 4 | Render-target tracking | `gs/hle/targets/` |
| 5 | VRAM mirror sync (GPU↔CPU) | `gs/vram/` (PageRecord + ReadView/Materialize) |
| 6 | VU1 program dispatch | `vu/tiers/dispatch.*` (+ `vu/hle/registry.cpp`) |
| 7 | HLE hook table | gone as a concept — `SdkBindingManifest` rows in each subsystem's `hle/` (§4.9); dispositions in `HOOK_RECLASSIFICATION.md` |
| 8 | Syscall table | `ee/hle/syscalls.*` |
| 9 | Timers / vblank | `ee/timing/` + `ee/intc/` |
| 10 | Savestate serialization | `core/savestate/` + SectionSpec beside each module |
| 11 | Input | `platform/*/input` + `pad/hle/` |
| 12 | Audio | `sound/` + `sound/hle/` |
| 13 | [REV3 2026-09-23 D9] Play-time capture (bug_report / slowspot / artdet fire / vutrace-fallback / PERF evidence bundling) | `core/triage/` (runtime) + `tools/triage/` (consumer CLI) + `games/<title>/triage/` (bundles) — §18, TRIAGE_CAPTURE.md |

## 13. One class per file + CI lint [REV2 2026-09-23 D4-D8]

**The rule (D4, binding).** Every class or struct-with-behaviour gets its own
`.h`/`.cpp` pair named after it — snake_case file = PascalCase type
(`psm_layout.h/.cpp` = `PsmLayout`, `vram_image` = `VramImage`, `texture_source`
= `TextureSource` + its `ResolveRead` entry, `render_target` = `RenderTarget`,
`time_domain` = the `TimeDomain` family). Free-function groups get a
concept-named pair (`clut.h/.cpp`, `expand.h/.cpp`). **No textual `.inc`
includes for code** — the TU-tail pattern (six `*_native.inc` at
opengl_backend.cpp's tail, A1 §1.1) and the 222 `fun_*_body.inc` are the
counterexamples this rule buries; both populations already retire under
D1/D3/D5, and nothing new may recreate the pattern. **Headers are declarations
only**: trivial accessors (single-expression getters/setters) may stay inline;
templates that need visible definitions go in a sibling `.inl` included at the
header's end — by rule, `.inl` is the ONLY sanctioned textual include, and only
for templates.

**Allowed without counting as a second primary type**: nested types; TU-private
helpers in anonymous namespaces; plain-data PODs ≤ ~30 lines declared adjacent
to their single owner (e.g. `TexReadView` beside `TextureSource`); the enum a
class's API consumes.

**Explicit exemptions** (listed in the lint config, never implicit):

1. Generated files — `games/<title>/gen/`, `recompiled*`, bindings, build_id.
2. `ee/cpu/cpu_state.h` register PODs (the frozen CpuContext family).
3. Vendored third-party code (`app/gui/third_party/`).
4. `.inl` template-definition siblings (paired to their header by name).

**The CI lint — lives in `tests/` (D4 wording is binding):**
`tests/lint/lint_one_class.py`, registered as ctest `LintOneClass` beside the
existing gate families. Implementation: libclang where available, falling back
to a regex-AST hybrid; per non-exempt file it counts top-level record
declarations with ≥1 method plus top-level namespaces containing ≥2 unrelated
free-function groups; >1 primary ⇒ FAIL with the offending declaration list;
any `#include "*.inc"` in hand-written code ⇒ FAIL; non-trivial function
definition in a `.h` outside the accessor/`.inl` allowance ⇒ FAIL. Its sibling
`tests/lint/lint_engine_purity.py` (ctest `LintEnginePurity`) greps `engine/`
for the A6 §1.2 game-token regex set (EE text-range address literals,
`FUN_`/`func_00xxxxxx`, the game VRAM base constants, game strings, VU1 program
stamps), seeded with the A6 §3.3/§4 constants as the denylist; 0 tolerated.
Both lints are applied **as clusters land** in the new tree during migration
(MIGRATION §1.6 adds a "lint clean" column), not as a big bang; a moved file
must pass before its phase gate.

## 14. Engine/game interfaces — TitleProfile, VU1 library, RendererProfile [REV2 2026-09-23 D4-D8]

The D5 split is only as real as its interfaces. Three registration surfaces,
all data-shaped, all installed by `app/host` at boot from ONE `TitleProfile`.
`engine/` compiles and links with none of them populated — the [GD1] contract
(empty profile ⇒ interpreter + generic DrawBatch renders correctly, slowly) is
the bring-up default for any new title and a standing engine test.

### 14.1 TitleProfile (`games/<title>/title_profile.cpp`)

Field set per the binding decision — {serial, crc, sdk_version, iop_modules[],
quirks{}, vu1_library*, renderer_profile*, symbol_map*, mods_manifest*} — plus
the A6-sourced fields the runtime already needs (elf, boot, hle, patches, save):

```cpp
struct TitleProfile {
  const char*        serial;         // "SLUS-12345"
  uint32_t           crc;            // 0x<elf-crc>
  struct { const char* path; uint32_t entry, gp, load_lo, load_hi; } elf;
  struct { const char* version; const char* ioprp; int ee_imports; } sdk;   // "3.0.1", "IOPRP300", 22
  struct IopModules {                // stock modules by name/version → engine iop/hle
    std::span<const IopModuleRef> stock;      // IOPRP300 set + disc IRX (sio2man 2.03 … cdvdstm 1.01)
    std::span<const IopPlugin>    custom;     // {"STREAM.IRX", IopPlugin_StreamIrx} — recomp plugins
  } iop_modules;
  struct Quirks {                    // title-tuned behavior switches of GENERIC mechanisms (A6 §4)
    bool     xgkick_defer;           // true — TT VU1 microcode XGKICK timing (GameDB gamefix parity)
    uint8_t  round_sprite_mode;      // 1 — sprite half-pixel rounding convention
    BudgetModel budget;              // FUN_00191bd0 emulation params: bases (8400..9700 tuned),
                                     //   boot-phase classifier thresholds ([S96]/[S112])
  } quirks;
  const Vu1ProgramLibrary* vu1_library;       // §14.2 — may be null (interpreter-only title)
  const RendererProfile*   renderer_profile;  // §14.3 — may be null (generic DrawBatch only)
  const SymbolMap*         symbol_map;        // adjudicated names ↔ v2-discovered fn addresses;
                                              //   the ONLY address vocabulary mods may target (§15)
  const char*              mods_manifest;     // "games/<title>/mods/manifest.toml" (§15)
  struct { const char* argv; uint32_t args_base; } boot;    // "cdrom0:\\SLUS_123.45;1", 0x0030a980
  std::span<const TitleHleModule> hle;        // loader_sync, budget — models registered into engine seams
  std::span<const TitlePatch>     patches;    // e.g. {0x00181490, NopCall, "anti-piracy", HOOK ledger} —
                                              //   ONLY rows surviving HOOK_RECLASSIFICATION adjudication
  struct { const char* mc_dir; uint32_t stream_recv; } save; // "BASLUS-12345", 0x003B3000
  std::span<const SectionProvider> savestate_sections;       // IOPM/STREAM/MCHL/SDRX providers
  struct { const char* elf_sections; const char* corpus_pairs; } recomp;    // per-title recompiler inputs
};
// games/<title>/title_profile.cpp defines
// const TitleProfile kExampleTitleUS = { …every field sourced from A6 / runtime-environment ground truth… };
```

`app/host` registers exactly one profile; every consumer (recompiler config,
IOP loader, VU1 dispatcher, class-renderer registry, mods host, savestate
registry, memory ELF loader — which deletes memory.cpp's hardcoded path +
section table, A6 §7 row H) reads it through const&.

### 14.2 Vu1ProgramLibrary — registration API (engine mechanism ⇄ game library)

```cpp
struct Vu1FamilySignature {          // the five constants A6 §2.2 extracts from vu1_hle.{h,cpp}
  uint32_t variant_base;             // 0x1720 — first byte of the per-material variable region
  uint32_t dispatcher_op0;           // 0x090E0003 — ILW.x vi14,3(vi00), the family dispatcher's first op
  uint16_t exit_stubs[2];            // 0x70, 0x90 — the dispatch chain's E-bit terminators
  uint32_t family_exit_tpc;          // 0xA0 — the round-1 TPC exit contract
};
struct Vu1LibraryEntry {             // per-game entry the engine dispatcher looks up by hash
  uint64_t     image_hash;           // exact-image tier (0 = variant-keyed, as all 137 current entries are)
  uint64_t     variant_hash;         // MPG-patched per-material family tier — THE key for this title
  const char*  name;                 // "vs_family_tristrip" … (also the RendererProfile stamp vocabulary)
  uint16_t     start_pc;             // 0 dispatcher, 0x1748 sprite direct, …
  Vu1Outcome (*fn)(const VuRunInput&, VuRunOutput&);   // native program; may DECLINE → fallback tier
  std::span<const uint32_t> oracle_case_ids;   // vutrace corpus cases proving THIS entry (FAIL=0 gate)
};
struct Vu1ProgramLibrary {
  Vu1FamilySignature family;                   // drives Analyze()'s chain walk + prefix check
  std::span<const Vu1LibraryEntry> entries;    // 223 entries / 176 hashes for SLUS-12345 (12 program names, 15 program functions; IDOC-A)
};
```

Engine behavior (unchanged mechanism, new ownership): `vu1hle::Analyze()` takes
the signature as a parameter; `Lookup()` resolves exact-image → variant →
wildcard; a miss or a DECLINE falls to the cached interpreter — **the engine
ships ONLY the mechanism + the instrumented/cached interpreter tiers**; with a
null library every image interprets. `vu1_replay --hle` over each entry's
`oracle_case_ids` is the per-entry proof gate; `--catalog` reads the same
table. A second title registers a different library — or none.

### 14.3 RendererProfile — generic idiom detectors + per-game trigger data

```cpp
struct DrawShapeSpec {               // one census row, DATA not code: every axis Accept() pins today —
  // prim, iip, fst, fog, tex on/off+psm, abe+ABCD+FIX, cc, pabe, fba, ate,
  // DATE/datm, zte/ztst, zmsk, frame psm/fbmsk, scanmsk … each {require, forbid, any}
};
struct SequenceRecipe;               // declarative multi-pass FSM: the S9 seven-pass composite
                                     //   (CLEAR 0x78787878 / SEED / ZCOPY / INVERT / STRIPS / QUANT /
                                     //   COMPOSITE), FMV family decomposition (family F of six), …
struct VramLayout {                  // the ~220 constants leaving gs/render code (A6 §3.3):
  // display 0x0/0x70000 · scene_rt 0x230000 · fog 0x280000+0x2F0000 · s9_zcopy 0x268000 ·
  // fmv_stage 0x150000 · tile_atlas base 0x380000 stride 0x10000 cluts +0xC00 / 0x370000+0x5400 ·
  // fonts 0x3F0000 · ocean 0x3C0000/0x3D0000 · z 0x150000 — queried as layout.is(scene_rt, fbp)
};
struct RendererProfile {
  std::span<const std::pair<const char* /*vu1 program stamp*/, classrdr::Class>> stamps;
  std::span<const std::pair<classrdr::Class, DrawShapeSpec>> accepts;   // per-class census tuples
  std::span<const SequenceRecipe> sequences;
  VramLayout vram;
  RawClassifierSpec raw;             // unstamped-draw taxonomy (S9 signature, ≥256×128 blit threshold, …)
};
```

The six lowerings (strip / skinned / sprite / particle / blit /
stencil-composite) stay in `engine/gs/hle/classrdr/` as **generic idiom
detectors parameterized by the spec**; `ClassifyFields()`'s strstr-on-program-
names chain (A6 §3.1) becomes a read of `stamps`; every hard-coded
`fbp==0x230000`-style compare becomes a `layout` query; the gs_state.cpp:2909
cutscene latch becomes a title scene-hook. Every decline still falls back to
the generic DrawBatch path — [GD1] guarantees correctness with an empty
profile. Class renderers keep their per-class hatches and counters; the
byte-identity harness gates each class as its profile row lands.

**Adding a second game** — the authored-by-hand vs tool-generated boundary and
the nine-step checklist live in MIGRATION_PLAN Appendix A ("ADD A SECOND
GAME"), kept beside the phase plan because it is an operational procedure.


## 15. Intentional modding hooks [REV2 2026-09-23 D4-D8]

**Principle (binding): hooks exist ONLY for modding/enhancement — never to
make the game work.** Anything a mod needs that isn't a declared surface is a
feature request for a new hook, not a patch. Two hard rules, stated here and
mirrored in the companion docs:

1. **Engine correctness tests and the equivalence harness run mods-off.**
   Mods-off is the only configuration the harness certifies (EQUIVALENCE §1);
   CI asserts the mods-off build is byte-identical with the mod host compiled
   in but empty. Loading any mod stamps the window title and logs
   "MODDED — equivalence void".
2. **HOOK_RECLASSIFICATION's 250 EMULATION-BUG-WORKAROUND rows are NOT mods.**
   They are masks over our own defects, retired at their root fixes under the
   §4 adjudication ladder (also stated in HOOK_RECLASSIFICATION §6). The mod
   surface may never host a workaround, and a workaround may never be
   grandfathered as a mod.

**Per-title declarative manifest** — `games/<title>/mods/manifest.toml`, empty
by default. Required per-mod metadata per the binding decision: target
(address or symbol, resolved against the per-title symbol map), kind
(pre/post/replace), author, provenance, enabled, savestate-compat flag:

```toml
[mod]                                # one table per mod, load-ordered
id = "debug-camera"
version = "0.1"
author = "example"
provenance = "https://example.org/crash-mods/debug-camera"   # where it came from / who maintains it
enabled = true
savestate_compat = false             # false ⇒ loading states saved with this mod active warns/blocks
requires_engine = ">=2.0"

[[mod.detour]]                       # code hook via the title symbol map
target = { symbol = "title::UpdateCamera" }   # OR { address = 0x0019xxxx } — an address is legal
                                     #   ONLY if it resolves to a function in games/<title>/symbols/
                                     #   (the v2-discovered set + adjudicated names); anything else
                                     #   hard-fails the load
kind = "pre"                         # pre | post | replace (replace receives the original as callable)
handler = "plugins/freecam.dll:on_update_camera"

[[mod.texture]]                      # asset replacement — keyed by content, never frame timing
match = { tbp = 0x370000, psm = "PSMT8", hash = "93cb32d7" }
replace = "textures/logo_hd.png"     # decoded via the ONE texture-source resolver

[[mod.vu1_program]]                  # render override — keyed by variant hash (the §14.2 key)
variant_hash = "0xd4264095dc680c85"
handler = "plugins/hd_strips.dll:vs_family_tristrip_hd"

[[mod.audio]]                        # bank/voice replacement — keyed by STREAM.IRX bank id
bank = 12
replace = "audio/theme.ogg"

[[mod.config]]                       # config overlay (whitelisted keys of the 32-field Config only)
key = "render.scale"
value = 2
```

**Engine side — `engine/ee/mods/` (the ModHost):** (a) loads manifests ONLY
when `--mods` is passed (`app/host` is the sole call site); (b) resolves every
entry against the declared registries — the title symbol map for detours, the
texture resolver, the VU1 library, sound banks, the config whitelist; (c)
hard-fails on ANY unresolved entry (no silent partial loads); (d) installs
detours through the v2 emission's `update_function_impl` seam — the same
frozen function-replacement mechanism SdkBinding rows use (ARCH §4.9/§5.2),
with **safe re-entry**: pre/post wrappers preserve the shadow-call-stack
push/pop contract and the R5 rule (host stack carries no PS2 state), a
`replace` handler receives the original as a callable, and re-entrant
installation (savestate load, engine-HLE reinstall) is idempotent — install
order is manifest order, last-wins per target, and a detour never survives a
`recomp_mark_hooked` conflict with an SdkBinding row (bindings win; the load
fails loudly instead). Savestates record the active mod set; loading a state
whose mod set differs warns, and blocks if any active mod has
`savestate_compat = false`.

**Worked examples** (hypothetical, shipped as documentation under
`games/<title>/mods/examples/`): *widescreen* — a `pre` detour on the
projection-setup symbol adjusting the aspect constant + a `config` overlay for
`video.fit_mode`; *60 fps unlock* — a `pre` detour on the frame-rate governor
symbol (paired with `quirks.budget` awareness; documented as gameplay-affecting,
`savestate_compat=false`); *debug camera* — `pre` detour on
`title::UpdateCamera` redirecting to a plugin free-cam. Each example
documents which declared surfaces it uses and why it needs no others.

## 16. Platform targets and honest feasibility [REV2 2026-09-23 D4-D8]

`engine/platform/{linux, macos, windows, psvita, wii, gamecube, _template}/`,
each implementing ONE `Platform` interface — no `#ifdef` outside this tree —
plus a per-target CMake toolchain file in its folder. `_template/` is the
copy-me skeleton: every entry point stubbed with `unimplemented()`, a README
checklist, and the CI stub-build wiring.

```cpp
struct Platform {                     // capability-honest, like the GPU caps contract
  // window/surface
  Surface  (*create_surface)(const VideoConfig&);
  // input
  void     (*poll_input)(InputState&);
  // audio out
  AudioSink(*audio_open)(const AudioSpec&);
  // file IO (ISO + config + savestates; Vita: app0:/ux0:; consoles: DVD/SD)
  File     (*open)(const char* path, Mode);
  // threads OR coroutines: CPU thread, graphics thread, PS2 cooperative threads
  Thread   (*thread_create)(void (*fn)(void*), void*, const ThreadDesc&);
  Fiber    (*fiber_create)(void (*fn)(void*), void*, size_t stack);   // win32 fibers / ucontext /
  void     (*fiber_switch)(Fiber);                                    //   custom asm / coroutine shim
  // timers: the single TimeDomain source (A4's 9-clock consolidation)
  uint64_t (*now_ns)();
  Timer    (*timer_create)(uint64_t period_ns, void (*cb)(void*), void*);
  // memory mapping: fastmem arena reservation, page protection, (no-MMU targets
  //   report caps.has_mmap=false and the fastmem tier degrades to checked access)
  void*    (*map_reserve)(size_t bytes, MapFlags);
  bool     (*map_protect)(void* p, size_t bytes, Prot);
  size_t   page_size;
  const PlatformCaps caps;            // memory budget, gles_level, has_fibers, has_mmap, …
};
```

**Render backend mapping per target** (backend lives in `render/backends/`,
selected by caps + config; the platform folder only provides the surface):

| Target | Backend | Note |
|---|---|---|
| linux | gl (vulkan optional) | SDL2 surface; CI-able headless |
| macos | gl 4.1 today; **metal via MoltenVK** as the Vulkan backend's transport when rebuilt | Apple-silicon FP contract already handled ([FP-CONTRACT]) |
| windows | vulkan (rebuilt) or gl | Win32 fibers native |
| psvita | **gxm** (GLES2-feature-class; pipeline precompile REQUIRED — §6 cache contract) | vitaGL exists as a GL1/2 shortcut, not the plan of record |
| wii | **gx** (fixed-function-era; research stub only) | see feasibility |
| gamecube | **gx** (tombstone) | see feasibility |
| all | **soft** (scalar oracle) | runs everywhere the CPU does; the pixel-pipeline oracle |

**Honest feasibility table — this is the commitment, not marketing.** The
memory baseline any PS2 recomp target must hold: 32 MB guest RAM + 4 MB GS
VRAM + the CPU mirror/shadow copies (~8-12 MB across VramImage bytes+shadow)
+ recompiled code — **~100-190 MB today** as measured (RSS 743-1,075 MB on
desktop including caches/GL; the 100-190 MB figure is the recompiled-text +
guest + mirrors floor after the D1/D2 deletions) — plus caches, pools and the
host runtime.

| Target | Verdict | Grounds — and what must shrink |
|---|---|---|
| windows | **Supported** (original platform) | Win32 fibers native; Vulkan backend currently STALE (A1 §5.4) — GL or rebuilt Vulkan per plan |
| macos | **Supported** (current dev platform) | GL 4.1-on-Metal today; MoltenVK transport when the Vulkan backend is rebuilt; FP contract handled |
| linux | **Feasible, low risk** | SDL2/GL ports directly; fibers → ucontext/boost.context; becomes the CI reference target |
| psvita | **Feasible with major effort — spike-gated** | 512 MB device / ~256-330 MB app budget CAN hold guest 32 MB + 4 MB VRAM + mirrors — IF the recompiled code shrinks: today's ~100-190 MB of x86-64-shaped emitted text must come down via the §5.2 density work (flat dispatch, denser IR emission target, per-shard dead-strip) toward a ~60-90 MB ARM32 text budget, and the VramImage shadow must be optional-lazy. Needs: GXM backend (caps contract anticipates it), ARM32 codegen of recompiled output, custom fibers, explicit newlib-heap plan (d3es-vita 300 MB / rvgl 160 MB precedents), pipeline precompile at build time. Perf headroom unproven on 4×444 MHz A9 vs the EE's 300 MHz workload — U2_VITA_FEASIBILITY.md is the standing study; A4 lists 12 concrete blockers (B1-B12). Ship criterion: the §14.3 empty-profile leg at full speed BEFORE any native classes |
| wii | **Not feasible for the full game — stated plainly** | 88 MB total (24 MB MEM1 + 64 MB MEM2) cannot hold 32 MB guest + mirrors + recompiled text + host runtime; 729 MHz single-core PPC750 cannot host EE(300 MHz superscalar)+VU work even fully HLE'd — projected >10× over budget on the mods-off software path. GX has no shaders for the blend/TEST emulation the DrawIR needs. The folder exists ONLY as a research stub if someone funds a spike (menu-scene demo at best); no roadmap claim |
| gamecube | **Not feasible** | 24 MB main + 16 MB slow A-RAM < the guest's 32 MB alone — physically excluded before any code runs. Folder is a tombstone whose README contains this sentence |

**What a target feasibility spike measures BEFORE any port work is committed**
(the gate for psvita, and for wii if ever funded): (1) **code size** — compile
the recompiled shards + subsystem libs with the target toolchain; report text/
rodata vs the target's budget; (2) **RAM** — the explicit memory plan table
(guest 32 MB + VRAM 4 MB + mirrors + caches + heap) vs the app budget, with
the custom-allocator plan; (3) **GPU features** — run the DrawIR corpus
through the caps checker: which blend/TEST/fbmsk/shuffle cases lack a mapping
on the target API (gxm/gx), and what the fallback costs; (4) **CPU
throughput** — the empty-profile interpreter leg + the recompiled-EE bench
legs on target hardware vs the EE's 300 MHz workload (fps on the anchor set;
the [GD1] leg at full speed is the pass bar). *Informed by
REFERENCE_ARCHITECTURES: N64Recomp's portability comes from single-file
plain-C output plus a runtime whose platform I/O is app-registered callbacks
(§1.8 — "compiles for any platform", the renderer sets the floor); Unleashed
Recompiled ships x86-64-with-AVX + D3D12/Vulkan-1.2 floors and gets ARM64 via
SIMDe (§3.6) — both prove the recomp CORE ports cheaply while the
renderer/hardware floor is the real gate; the Vita ports study (§4.6) proves a
MIPS-console emulator runs on Vita when heaps are explicit, stacks bounded,
dispatch flat, and shaders precompiled.*

Rule (unchanged from A6, now binding): a platform folder may exist only with a
written feasibility note (this table) and a CI-buildable stub; no aspirational
empty dirs.

## 17. Bug knowledge base [REV2 2026-09-23 D4-D8]

`docs/bugs/` is the project's distilled defect memory: `TEMPLATE.md` (the
entry contract: ID, title, user-visible symptom, scene/savestate, subsystem,
root cause, **the PS2 rule violated**, fix + hatch, regression test, source
record path, triage tips), `INDEX.md` (by subsystem AND by symptom),
`TRIAGE.md` (symptom → likely subsystem → first checks → tools), and 30
seeded entries (BUG-001…BUG-030) distilled from this year's
`docs/loop_state/` records — each entry cites its record path. The KB is the
onboarding artifact the 283-record history cannot be; the "PS2 rule violated"
field is what makes it portable to a second title. Maintenance rules live in
TEMPLATE.md; every future root-cause session appends an entry in the same
commit as its fix.

## 18. Triage capture — play-time bundles for later sessions [REV3 2026-09-23 D9]

**Principle (binding, D9): the runtime captures, at play time, three classes of
event — HLE fallbacks (VU1 / EE / IOP-RPC / GS-class / syscall), sustained poor
performance, and frame-time spikes — into self-contained bundles under
`games/<title>/triage/` that a LATER session (AI or human) can triage and act on
without the original machine state.** Every bundle carries a savestate (for
spikes: a slot from the rolling pre-event `SavestateRing`), the input log since
that state (`InputLog`, the same recorder EQUIVALENCE §2 requires), the title
profile id, build id, effective Config, and an AI-contract `MANIFEST.md` (what
happened / what to do / the repro command / the acceptance test). Fallback
bundles carry the code and the oracle I/O a lift needs: `Vu1Case` (the existing
33,456-byte capsule) for VU1, the new `EeFunctionCase` (function bytes +
disassembly + entry/return contexts + memory windows) for EE, the RPC payload
pair for IOP, a `.gsdl` window for GS declines. Dedup is content-keyed (same
variant hash / hot function / rpc id ⇒ one bundle + a durable counter);
retention is per-class caps + a disk budget; ALL capture I/O rides one throttled
background writer thread so capture can never cause the spikes it reports —
each bundle records its own capture cost. `tools/triage/` is the consumer:
`list`, `repro` (headless savestate+padlog replay), `lift` (scaffolds the HLE
case + oracle test), `perf` (attribution report). A fixed bundle becomes a
regression test at its layer plus a `docs/bugs/` entry, and its index row is
marked fixed so a re-fire reads as a regression. Full design — as-is audit with
file:line cites, component contracts, per-class content specs, layout decision,
thresholds, cost budget, workflow: `docs/design/TRIAGE_CAPTURE.md`. Absorbs:
U5 bug_report (crash/manual triggers live on), slowspot (→ PerfMonitor), the
artdet fire path, the fallback-triggered arm of vutrace capture.

---
*Companion documents: `MIGRATION_PLAN.md` (how current code reaches this tree,
incl. the A6 MIXED split rows §1.6 and the ADD-A-SECOND-GAME appendix),
`EQUIVALENCE_PLAN.md` (how identity is proven at every step; mods-off),
`HOOK_RECLASSIFICATION.md` (every former address-bound override, dispositioned;
workarounds are not mods), `TRIAGE_CAPTURE.md` ([REV3 D9] the play-time capture
subsystem), `../bugs/` (the bug knowledge base).*
