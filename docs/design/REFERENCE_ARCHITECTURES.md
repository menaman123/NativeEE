# REFERENCE_ARCHITECTURES — what the successful recomp/decomp/port projects teach us

Companion to `ARCHITECTURE.md` (which applies these lessons), `MIGRATION_PLAN.md`,
and `EQUIVALENCE_PLAN.md`. Researched online 2026-09-22 (session "ref"); every
claim is paraphrased from the cited public source — no license-encumbered text is
reproduced. Where a lesson changed our design, the change is listed in §5 and the
edited section in ARCHITECTURE.md / MIGRATION_PLAN.md carries an
`Informed by: <project>` note.

Reading order: §1–§3 are the three deep studies (N64Recomp family, OpenGOAL,
Unleashed Recompiled family). §4 is the brief survey (libultraship/SoH, RSDK,
PCSX2 GS, Dolphin, DuckStation/Panda3DS, Vita ports). §5 is the comparison matrix
and the lessons-to-changes table.

---

## 1. N64Recomp + N64ModernRuntime + Zelda64Recomp (+ RT64)

Sources:
- https://github.com/N64Recomp/N64Recomp (tool; MIT)
- https://github.com/N64Recomp/N64ModernRuntime (librecomp + ultramodern; GPL-3.0)
- https://github.com/Zelda64Recomp/Zelda64Recomp (game shell; GPL-3.0)
- https://github.com/rt64/rt64 (renderer)

### 1.1 Repo layout — a three-repo product line

The ecosystem is deliberately split into three repos with a strict dependency
direction:

| Repo | Contents | Depends on |
|---|---|---|
| **N64Recomp** (tool) | `src/`, `include/`, `lib/`, plus sibling tools: `RSPRecomp/` (RSP microcode recompiler), `LiveRecomp/` (runtime recompilation for mods), `OfflineModRecomp/`, `RecompModTool/`, `RecompModMerger/` | rabbitizer, ELFIO, toml11, fmt |
| **N64ModernRuntime** | `librecomp/` + `ultramodern/` libraries, `thirdparty/`; consumed by games via CMake `add_subdirectory` | N64Recomp (submodule, for shared types) |
| **Zelda64Recomp** (game) | `src/` (thin shell), `patches/` (+ Makefile), `us.rev1.toml`, `patches.toml`, `njpgdspMain.us.rev1.toml` (RSP microcode config), `overlays.us.rev1.txt`, `Zelda64RecompSyms` (symbol data as a **submodule**), `mods/`, `rsp/`, `shaders/`, `shadercache/`, `assets/` | N64ModernRuntime, RT64 |

The game repo contains *no* recompiler code and *no* OS-reimplementation code;
per-game knowledge lives in **data files** (TOMLs, symbol submodule, overlay
lists), not in forks of the tool.

### 1.2 Generated vs hand-written split

- The tool emits C from the N64 binary; the output "is expected to be used with a
  runtime" — generated code never contains platform or OS logic, only calls to
  runtime-provided macros/functions.
- **librecomp** is the bridge: it implements the lookups and leftovers the
  generated code needs (overlay section tables, PI DMA / ROM reads, save types).
- **ultramodern** is the OS reimplementation: threads, message queues, timers,
  controllers, audio, RSP task handling, VI timing. Platform I/O (controller
  reads, audio submission) is supplied **via callbacks registered by the
  embedding app**, and the app also **registers the renderer** (RT64 is the
  recommended one, not a hard dependency).
- The game shell is thin: menus (RmlUi), config plumbing, asset extraction.

### 1.3 IR / emission strategy

- No mid-level IR: instruction-at-a-time, deliberately literal translation
  ("very literal in order to keep complexity low") — e.g. an `addiu` becomes a
  macro-wrapped assignment on a context struct; `jal` becomes a direct C call;
  identified tail-calls become calls; `jr $reg` with a recognized jump table
  becomes a `switch`.
- Branch delay slots are handled by duplicating the slot instruction where
  needed — correctness first, no cleverness.
- Indirect calls (`jalr`) emit `LOOKUP_FUNC(ctx->rN)(rdram, ctx)` — the **runtime**
  owns the address→function mapping, so overlays and mods can rebind at runtime.
- Relocatable overlays: relocation-bearing instructions emit `RELOC_HI16/LO16`
  macros with a section index; the runtime patches immediates at overlay load.
- One function per output file (grouping planned) — compile-time is managed by
  file granularity, an explicit output-shape concern like our sharding.

### 1.4 Hooks / patches vs generic runtime

- The recompiler TOML can stub functions, skip functions, and **patch single
  instructions** in the target binary.
- **Single-file-output mode**: all functions of a hand-patched ELF are emitted
  into one file, and standard linker symbol-resolution order (static libraries
  resolve only unfound symbols) makes patched functions override the original
  recompiler output — described as saving "a tremendous amount of time" while
  iterating, because neither the recompiler nor the bulk output rebuilds.
  Zelda64Recomp's `patches/` + `patches.toml` + Makefile implement exactly this.
- `[[patches.func]]` / `[[patches.hook]]` sections reserve a declarative
  function-hook channel in the same config file.
- Mod support is a productized extension of the same machinery (RecompModTool,
  Thunderstore distribution, runtime mod loading).

### 1.5 Renderer / platform abstraction

- RT64 is a separate project; ultramodern only defines the registration seam.
  Zelda64Recomp advertises that N64 framebuffer/depth effects run **unmodified**
  through RT64 — accuracy of the interface, not hacks in the game shell.
- Platform floor is explicit in the README: D3D12 / Vulkan 1.2 / Metal-class GPU
  required; SSE4.1 floor on x86-64; ARM64 builds supported.

### 1.6 Config

- Per-game recompiler config is TOML (`us.rev1.toml`); runtime user config is the
  in-game menu. Symbol metadata is a versioned submodule. Everything per-game is
  data.

### 1.7 Testing / verification

- RSP microcode recompilation credits the Ares emulator's RSP vector instruction
  implementations as reference — an external oracle for the vector unit.
- No public whole-game differential harness; correctness is anchored on literal
  translation + reference implementations + community soak. (We keep our
  stronger harness; this is a place we exceed the references.)

### 1.8 Portability story

- Output C compiles with msvc/gcc/clang "for any platform"; runtime floor is set
  by the renderer (RT64 needs modern APIs). Windows/Linux/macOS + ARM64 shipped;
  Steam Deck supported.

---

## 2. OpenGOAL (jak-project)

Sources:
- https://github.com/open-goal/jak-project (ISC)
- https://github.com/open-goal/jak-project/blob/master/docs/project-overview.md
- https://opengoal.dev/docs/porting-info/drawable_and_tfrag/porting_tfrag/

### 2.1 Repo layout

Four top-level components plus support dirs:

| Dir | Role |
|---|---|
| `goalc/` | Their own GOAL (Lisp) compiler for x86-64, with REPL + live patching |
| `decompiler/` | GOAL decompiler; per-game config (type casts, var names, stack structures) as JSON |
| `goal_src/` | The decompiled **game source** — the analogue of our recompiled EE code, but human-maintained |
| `game/` | The C++ runtime: `kernel/` (GOAL linker, low-level language runtime), `sce/` (Sony library impls), `system/` (PC-specific), `overlord/` (IOP driver reimplementation), `sound/` (989SND), `graphics/` (renderers), `assets/` |
| `common/`, `test/`, `tools/`, `third-party/` | shared code, tests, tooling |

### 2.2 Generated vs hand-written split

- ~98% of the game is GOAL; it is decompiled to `goal_src/` and **recompiled by
  their own compiler** — the game logic layer is regenerated source, the runtime
  is small hand-written C++.
- `game/kernel` follows an explicit discipline: "as close as possible to the
  game, and all differences should be noted with a comment." The same rule
  governs `game/overlord` (IOP). This is a *documented divergence* policy for the
  OS-surface code — the exact layer where silent drift is most dangerous.
- `game/sce` vs `game/system`: Sony-API implementations are separated from
  PC-port-specific services — the same cut as our per-subsystem `hle/` vs
  `platform/` [REV 2026-09-22 D3: formerly written `hle/sdk`].

### 2.3 IR / emission strategy

- The "IR" is a real language: decompile → typed GOAL source → compile with
  goalc. Hard hand-optimized assembly functions fall back to **MIPS2C** (their
  literal MIPS→C escape hatch) and can later be replaced with clean C++ or GOAL —
  a per-function accuracy/effort dial. (Their tfrag time-of-day interpolation
  shipped as MIPS2C first, rewritten as ~15 lines of C++ later.)

### 2.4 The renderer methodology — the most relevant lesson in this study

The tfrag porting doc describes a **three-stage bring-up** used per renderer
family (tfrag, tie, merc, shrub, sprite, ocean, sky, ...):

1. **Stage 1 — slow, exact "emulation-like" port**: decompile the EE-side
   drawing code; execute the original VU program semantics; hand the XGKICK
   output (GS packets) to a generic `DirectRenderer` that draws whatever the
   packets say. Slow, "not super fast, but it's nice for debugging."
2. **Stage 2 — understand and cut**: with the reference port working, identify
   what the VU program *means*; discover that large chunks (e.g. `tfrag-near`,
   most of the `tfrag` VU program) are never needed; move clipping/transform to
   the GPU where the API does it for free.
3. **Stage 3 — native renderer (tfrag3)**: an **offline preprocessing step**
   converts level data into GPU-friendly formats; the runtime renderer draws it
   with few draw calls, doing culling and time-of-day palette blending in C++.

Their stated conclusion: understanding the renderer was only possible because
they started with the slow exact port. This validates our tier ladder —
VU1 interp (reference) → cached interp → HLE program library emitting DrawIR —
and our rule that every native class renderer must keep a decline path to the
generic consumer. The `DirectRenderer` (generic GS-packet consumer) is precisely
our "GIF-parse → DrawIR → backend" fallback lane; their per-family native
renderers are our `render/classes/*`.

### 2.5 The IOP kernel — the second-most-relevant lesson [IOPK 2026-09-24]

OpenGOAL never emulates the IOP CPU. `game/system/IOP_Kernel.{h,cpp}` is a
~500-line cooperative scheduler over libco coroutines: a thread table with
priorities and states (Run/Ready/Wait/Dormant), wait kinds (Sleep, Delay,
Semaphore, Messagebox, EventFlag), banked WakeupThread tokens, and a
`dispatch()` that runs Ready threads by priority until none remains, then
returns the earliest DelayThread deadline. ONE host thread
(`game/system/iop_thread.*` + the `iop_runner` loop in `game/runtime.cpp`)
alternates `kernel.dispatch()` with a condvar `wait_until(next_deadline)` —
woken early by EE-side RPC posts. `game/sce/iop.cpp` binds the Sony kernel
API 1:1 onto that kernel; the game's custom IRX (OVERLORD) is hand-ported
native C++ (`game/overlord/`), the Sony sound library is a native
reimplementation (`game/sound/989snd/`). SIF RPC is a memcpy into the
server's buffer plus `iWakeupThread` through a locked queue; the RPC thread
loops `handle command → SleepThread`.

**Adopted** (implemented in the OLD tree behind `PS2_IOP_KERNEL`, target
shape `engine/iop/` — ARCHITECTURE §3 + §7.1): our recompiled STREAM.IRX
replaces their hand-port as the native module; the kernel/coroutine/host
thread/bridge split is the same, written fresh on our platform fiber layer.
**Extended beyond OpenGOAL**: a second dispatch policy (`iop.dispatch=frame`)
where the EE thread dispatches at frame boundaries on a frame-derived
virtual clock — OpenGOAL has no determinism requirement; our verification
harness does, and the policy switch is the entire host-dependence surface
(EQUIVALENCE §3). Their `wait_run_iop(next_wakeup)` loop is the REALTIME
policy verbatim in shape.

### 2.6 Hooks / game-specific vs generic

- Game-specific fixes live in the decompiled source itself (it's editable
  source), with PC-specific services isolated in `game/system`. Their scratchpad
  and DMA-register accesses are patched at decompile/port time to fake-scratchpad
  helpers and instant-DMA calls (documented pattern in the tfrag doc).

### 2.7 Config / testing / portability

- Per-game decompiler configs are JSON under `decompiler/config/<game>/`.
- Testing: decompiled output that compiles and round-trips is "continually
  tested as part of our unit tests"; the repo carries offline **reference files**
  (`test/decompiler/reference/**/*_REF.gc`) that pin the expected decompilation
  of each file — golden-file regression tests over the whole corpus. CI runs on
  Linux + Windows.
- Portability: x86-64 only (Windows/Linux/macOS via Rosetta) — the compiler
  emits x86-64, so the portability envelope is bounded by goalc, not the runtime.
  (Our C++-emitting recompiler is more portable than their approach by design.)

---

## 3. Unleashed Recompiled + XenonRecomp + XenosRecomp (+ plume)

Sources:
- https://github.com/hedge-dev/XenonRecomp (PPC→C++ tool)
- https://github.com/hedge-dev/XenosRecomp (Xenos shader→HLSL tool)
- https://github.com/hedge-dev/UnleashedRecomp (the game port; GPL-3.0)
- https://github.com/renderbag/plume (RHI; MIT)

### 3.1 Repo layout

| Dir | Role |
|---|---|
| `UnleashedRecompLib/` | The **generated layer**: recompiler config (`config/SWA.toml`, `config/SWA_switch_tables.toml`) + the emitted PPC C++ |
| `UnleashedRecomp/` | The **hand-written game runtime**: Xbox-360 kernel translation layer, GPU backend (over plume), APU, `user/` (config, save), UI/menus (ImGui), installer, patches |
| `UnleashedRecompResources` | Art/resources as a submodule |
| `tools/`, `thirdparty/`, `flatpak/`, `docs/` | support |

XenonRecomp itself is a separate repo, explicitly runtime-less: "It only converts
the game code to C++ … Making the game work is your responsibility." The tool /
runtime boundary is absolute.

### 3.2 IR / emission strategy (XenonRecomp)

- Literal instruction-at-a-time PPC→C++; CPU state struct passed to every
  function; second arg is the memory base pointer (32-bit guest pointers).
- Endianness handled at the load/store boundary (byte-swap on access, `volatile`
  to stop unsafe reordering); VMX registers stored whole-vector-reversed with
  instruction semantics adjusted — a documented, uniform convention rather than
  per-site fixes.
- Per-instruction FPU/VMX denormal-state switching — float-mode state is part of
  the emitted contract (analogue of our PS2-float clamp/rounding nodes).
- Indirect calls: a "perfect hash table" — function pointers stored at
  (guest_address × 2) past the valid XEX range of the base memory allocation, so
  an indirect call is one load, no map lookup. (Flat-table dispatch, same
  direction as our fn-pointer-array + range-bisect plan.)
- Jump tables detected by XenonAnalyse into a **TOML sidecar** consumed by the
  recompiler (→ real `switch` statements); function boundaries from `.pdata`
  plus analysis, with **manual TOML overrides** for the analyzer's blind spots;
  `invalid_instructions` skip lists for non-code bytes. All discovery corrections
  are config, not code.
- **Optimizations are opt-in flags, staged after correctness**: the README
  explicitly recommends enabling register-as-local / skip-LR optimizations only
  once the recompilation already runs. Payoff quantified: ~20 MB smaller binary,
  several ms/frame. This is the same discipline as our "behavior first, oracle
  gates, then optimize."

### 3.3 Hooks / patches

Two mechanisms, both declarative:

1. **Function override by weak linking**: every generated function is emitted
   weakly aliased to an `__imp__`-prefixed implementation; hand-written code can
   redefine the function and still call the original — wrapper-with-original
   semantics identical to our dual-hook contract, but resolved by the **linker**
   instead of a runtime table.
2. **Mid-asm hooks**: TOML entries (`name`, `address`, `registers`, plus
   `return`/`jump_address`/conditional variants) make the recompiler insert a
   call to a named function at an exact guest instruction; the game project
   implements the function and the linker binds it. Probe/patch injection is
   config-driven and reproducible — never hand-edited generated code.

### 3.4 Shader/renderer strategy (XenosRecomp + plume)

- Xenos shader binaries → HLSL → DXIL + SPIR-V offline; the entire shader set is
  scanned from game files, recompiled, compressed (smol-v + zstd), and
  **embedded as a cache keyed by XXH3 hash** — at runtime a shader lookup is a
  hash probe, never a compile.
- Renderer is "a translation layer … rather than emulating the Xbox 360 GPU":
  draw calls are translated to modern APIs over **plume**, a small RHI
  (single interface header + one file per backend: D3D12, Vulkan, Metal-WIP,
  "bring your own shader compiler", grown out of RT64).
- **Pipeline compilation is woven into asset streaming**: the renderer walks the
  game's own structures at load time to determine which pipelines an asset needs,
  compiles them on the streaming workers, and only then marks the asset loaded —
  "no stutters during gameplay the first time new objects or effects appear."
  The small residue (post-processing, 2D) is a fixed boot-time precompile list.
- Specialization constants bridge per-draw variation without permutation
  explosion (DXIL linking trick / SPIR-V spec constants).

### 3.5 Config / preservation stance / testing

- One user config file (`config.toml`) + in-game menus; mods via an external
  mod manager with file redirection; mods may not replace the executable or
  shaders.
- Preservation: original game bugs are intentionally preserved; a bug report is
  rejected unless accompanied by original-hardware footage proving the behavior
  differs — an operationalized "bug-compatibility unless adjudicated" policy,
  the same shape as our accepted-difference protocol.
- Testing: XenonRecomp recompiles the Xenia emulator project's PPC instruction
  test corpus and executes it, comparing results against expected values — an
  **external instruction-level oracle corpus** feeding the recompiler's CI.

### 3.6 Portability

- Floor: AVX-class x86-64, D3D12/Vulkan 1.2 GPU; Windows + Linux (Flatpak,
  Steam Deck); macOS pending plume-Metal. ARM64 viability comes from SIMDe in
  the emitted VMX code.

---

## 4. Brief studies

### 4.1 Ship of Harkinian / 2Ship (libultraship)

Sources: https://github.com/Kenix3/libultraship • https://github.com/HarbourMasters/Shipwright

libultraship (LUS) is the shared engine under the Zelda decomp ports: libultra
reimplementations + the **Fast3D display-list renderer** with multiple graphics
backends + SDL3 window/input/audio + resource manager. Two structural ideas:
(1) **assets are separated from the executable** into `.otr`/`.o2r` archives with
an override search order, making texture/data mods a data-layer concern the
engine never special-cases; (2) the platform/renderer layer is a **reusable
library across many games** (SoH, 2Ship, Starship...), proven on Windows, Linux,
macOS, iOS, Android (and console forks). Lesson: a game-agnostic runtime library
with a display-list-level renderer interface is what lets one team's platform
work fan out across ports.

### 4.2 Sonic Mania / RSDKv5 decompilation

Source: https://github.com/RSDKModding/RSDKv5-Decompilation

The **engine** (`RSDKv5/`) and the **game logic** (separate Sonic-Mania repo /
`Game`) are distinct artifacts; platform code sits under `platforms/`+
`dependencies/`, and a built-in mod loader + API is part of the engine. The
engine/game cut is what enabled the community port fan-out — Switch, 3DS, Wii,
Wii U, WebASM, and a **PS Vita port** (SonicMastr/Sonic-Mania-Vita) — mostly
without touching game logic. Lesson: our `runtime/` vs `hle/game` boundary is
the port-enabling seam; keep game-specific code out of the engine layers.
*[REV 2026-09-22 D2/D3]: the design no longer has an `hle/game` directory — the
lesson's seam is now carried by the subsystem/`render/backends`/`platform`
layering itself, with game knowledge entering only as data (ARCH §3 rule 2).*

### 4.3 PCSX2 GS layering

Sources: https://deepwiki.com/PCSX2/pcsx2/3.1-graphics-synthesizer-core •
https://github.com/PCSX2/pcsx2/blob/master/pcsx2/GS/Renderers/HW/GSRendererHW.cpp

Already the backbone of our GS design (ARCHITECTURE §2/§4): one format layer
(`GSLocalMemory` per-PSM descriptor table + `GSBlock`/`GSTables` + `GSClut`),
one register/vertex-kick state machine (`GSState`), draw-level policy above the
API (`GSRendererHW`), coherence in `GSTextureCache` (`Source`/`Target`,
dirty rects, hash cache), API adapters below (`GSDevice` GL/VK/Metal/D3D/SW).
The 20-year-old lesson our A3 audit re-derived independently: **exactly one
implementation of swizzle/CLUT/format math**, and draw order is sacred.

### 4.4 Dolphin VideoCommon unification

Source: https://dolphin-emu.org/blog/2019/04/01/the-new-era-of-video-backends/
(archived: https://web.archive.org/web/2024/https://dolphin-emu.org/blog/2019/04/01/the-new-era-of-video-backends/)

Moving all emulation logic into VideoCommon deleted >7,000 lines, erased
whole classes of backend-specific crashes, gave every backend every feature
automatically, and let one developer write a brand-new D3D12 backend that was
~55% smaller than the previous attempt *and* feature-complete. EFB peek/poke
emulation became one shared implementation with fast/slow paths chosen by
**capability flags** (e.g. point-rendering support), with all backends landing
within ~1% of each other. This is the direct blueprint for our `render/` (all
policy) over `backends/` (API adapters + caps) split.

### 4.5 DuckStation and Panda3DS — the lean-codebase ethos

Sources: https://github.com/stenzek/duckstation • https://github.com/wheremyfoodat/Panda3DS

DuckStation's stated focus: "playability, speed, and long-term maintainability",
with hack options discouraged and one default configuration expected to support
everything — the philosophical precedent for our "one shipped mode / escape
hatches become tests" rule. Its renderer stack pairs multi-API hardware backends
with a **vectorized, multithreaded software renderer** as a first-class citizen —
precedent for our `render/backends/soft` oracle. Panda3DS shows an HLE-first handheld
emulator kept deliberately small and readable (single optional system
dependency, SDL-first frontend), demonstrating that subsystem clarity — not
line count — is what keeps a hardware-emulation codebase contributable.

### 4.6 PS Vita ports — the real-world floor

Sources: https://vitasdk.org/ • https://github.com/Rinnegatamante/vitaGL •
https://github.com/Rinnegatamante/d3es-vita (Doom 3) •
https://github.com/Rinnegatamante/rvgl-vita (Re-Volt) •
https://github.com/Rinnegatamante/DaedalusX64-vitaGL (N64 emu) •
https://gl33ntwine.com/posts/develop-for-vita/

What large C++ ports on Vita actually contend with:

- **Heap budgeting is explicit and manual**: ports document raising the newlib
  heap (rvgl: 64→160 MB; Doom 3: 300 MB) and vitaGL offers a custom-heap build
  flag (`HAVE_CUSTOM_HEAP`) — the app owns a fixed memory plan, not a growing
  host heap. Total app budget is ~256–330 MB RAM + 128 MB VRAM on a 512 MB
  device.
- **GPU access is sceGxm** (GLES2-feature-class); vitaGL wraps a GL1/2 subset
  over it. The DaedalusX64 port ships with a **shader-compiler speedhack** flag
  because on-device shader compilation is expensive — pipeline/shader
  precompilation is not an optimization there, it is a requirement.
- **A MIPS-console emulator (N64) runs on Vita** via this stack — evidence that
  a recompiled-PS2-game workload (with our HLE reductions) is not absurd on
  quad Cortex-A9, provided host stacks are bounded, dispatch is flat, and the
  render path fits GXM caps.

---

## 5. Comparison matrix and lessons

### 5.1 Comparison matrix

Columns: **N64R** = N64Recomp/Zelda64Recomp stack; **OG** = OpenGOAL; **UR** =
XenonRecomp/XenosRecomp/UnleashedRecomp; **LUS** = libultraship/SoH; **P/D** =
PCSX2-GS + Dolphin-VideoCommon (renderer-split lessons); **Ours-today** = the
audited tree; **Ours-proposed** = ARCHITECTURE.md target.

| Concern | N64R | OG | UR | LUS | P/D | Ours today | Ours proposed |
|---|---|---|---|---|---|---|---|
| **Generated ⁄ runtime split** | 3 repos: tool / librecomp+ultramodern / thin game shell; per-game data files | decompiled source (`goal_src`) vs small C++ runtime (`game/`); sce vs system split | `UnleashedRecompLib` (config+generated) vs `UnleashedRecomp` (kernel/GPU/APU/UI); tool is runtime-less | engine library reused across games; game repos thin | n/a (emulators) | generated shards + 222 hand `.inc` bodies inside host_app; hooks scattered over 7 TUs | `tools/` ⁄ per-subsystem dirs (each owning its state + `hle/`) ⁄ `render/backends/` ⁄ `platform/` ⁄ `app/`; `generated/` build-dir only; game knowledge enters as data only [REV 2026-09-22 D2/D3] |
| **IR / emission** | literal per-instruction C; macros; runtime `LOOKUP_FUNC` dispatch; delay slots by duplication | decompile→typed GOAL→own compiler; MIPS2C escape hatch per function | literal per-instruction C++; TOML-driven jump tables/boundaries; perfect-hash indirect dispatch; staged opt flags | display-list interpreter (not recomp) | n/a | direct MIPS→C++ text in one 12.8k-line function | typed IR + verifier between decode and emission (§5); flat dispatch; bounded stacks |
| **Renderer abstraction** | renderer registered by app; RT64 behind a seam; modern-API floor | per-family native renderers above a generic packet consumer (DirectRenderer) | new renderer over plume RHI (1 file/backend); shader recompile offline; pipelines precompiled during streaming | Fast3D DL renderer, multi-backend | ALL policy in common layer; backends = API adapters + caps; SW renderer first-class | 25.6k-line GL TU owns policy + API + coherence | `gs/hle` policy over `render/backends/` caps-based adapters [REV 2026-09-22 D3]; DrawIR is the interface; soft backend as oracle |
| **Hooks / patches** | TOML stubs/patches; single-file patch mode overriding via link order; mod tools | edits in decompiled source; PC services isolated | weak-alias function overrides (linker); TOML mid-asm hooks with register args | data-archive override order (assets) | GameDB per-game hacks in HW renderer (PCSX2) | 191–208 main.cpp dual-hooks + 235 manual installs + sce walk, grep-discoverable only | one declarative signature-keyed `SdkBindingManifest` [REV 2026-09-22 D2: replaces the address-keyed `HleHookTable`; former patches dispositioned in HOOK_RECLASSIFICATION.md] + generation-time probe channel for diag builds |
| **Config** | per-game TOML + symbol submodule; in-game menus | per-game JSON decomp configs | `config.toml` user config; TOML sidecars for tool | JSON + archives | ini/GameDB | 496 env vars / 625 getenv sites | one 32-field ini schema [REV 2026-09-22 D1: overrides_file dropped]; getenv banned outside loader |
| **Tests / verification** | Ares RSP reference impls; community soak | golden `_REF.gc` reference outputs in CI; compile-round-trip continually tested | Xenia PPC test corpus recompiled + executed as oracle; original-HW footage rule for bug reports | community soak | Dolphin: unified logic ⇒ backend bugs vanish | 407 ctest gates, corpora, but pixels/audio not gated | two-binary deterministic harness (frames/audio/mem) + corpora + golden-emission snapshots |
| **Portability** | any-platform C; ARM64 shipped; GPU floor = renderer's | x86-64 only (compiler-bound) | Windows/Linux; AVX floor; SIMDe for ARM64 | Win/Linux/macOS/iOS/Android/consoles | Dolphin: new backend cheap post-unification | Windows+macOS desktop GL; 1 GB stacks block handhelds | Vita-class floor checked per decision (B1–B12); GXM backend feasible via caps |
| **Code size / readability practices** | small tool; runtime split into 2 libs by concern | kernel "as close as possible + comment divergences"; renderers per-family files | game code organized by subsystem (kernel/gpu/apu/ui/user); tool kept lean and separate | library-per-concern | Dolphin: −7k lines by unification; D3D12 −55% | 5 megafiles = 84k lines; 184 header globals; session tags in identifiers | ≤1.5k-line files, ≤300-line functions, no header globals, concept-named files, history in docs |

### 5.2 Lessons → concrete changes applied

| # | Lesson (source) | Change applied |
|---|---|---|
| L1 | Per-game recompiler knowledge is **data** (TOMLs, symbol submodule, jump-table sidecars), never tool forks (N64Recomp, XenonRecomp) | ARCHITECTURE §3: `tools/recompiler/config/` added as the single home for game metadata inputs. [REV 2026-09-22 D1: the surviving input is the corpus pair fixtures; Ghidra JSON, `recomp_overrides.ini` and `switch_manual_enumeration.json` retire in MIGRATION Phase 2] |
| L2 | **Link-time patch lane**: compile hand-patched functions separately; linker symbol priority overrides generated output; no regeneration during iteration (N64Recomp single-file mode; XenonRecomp weak aliases) | [REV 2026-09-22 D1/D2: **superseded** — the lane's consumers (manual bodies, GameFix hooks) were retired by decisions D1/D2, so ARCHITECTURE §5.2 now drops the lane; the lesson stands recorded for any future need] |
| L3 | **Mid-asm/probe hooks belong in tool config**, inserted by the generator, resolved by the linker — never hand-edited generated code and never runtime scatter (XenonRecomp) | ARCHITECTURE §4.9: the `GenProbe` generation-time declarative probe channel, used ONLY by diagnostics builds; replaces the ad-hoc PS2_SHADOW_DIAG-style injections [REV 2026-09-22 D2: it lives in a diagnostics manifest beside the SdkBindingManifest, not in a hook table] |
| L4 | The runtime is a **library**; the app is a composition root that registers platform callbacks and the renderer (N64ModernRuntime/ultramodern) | ARCHITECTURE §3 (post-tree rules) adds the composition-root rule: `app/host` constructs the subsystem modules and registers `IRenderBackend` + platform I/O callbacks at startup; the subsystem libraries never link SDL/GL |
| L5 | **Three-stage renderer bring-up** — exact reference consumer first, native class renderers second, offline preprocessing last; keep the reference tier alive for debugging (OpenGOAL tfrag1→tfrag3) | ARCHITECTURE §5.3 documents the tier ladder as the permanent shape (decline paths mandatory); records "static VU1 micro recompilation (RSPRecomp analogue)" as considered-and-rejected (NOAOT); offline mesh preprocessing noted as a future stage-3 option, not scope |
| L6 | OS-surface code carries a **divergence-comment discipline**: as close to original as possible, every difference noted (OpenGOAL game/kernel, game/overlord) | ARCHITECTURE §11 comment rules extended: the per-subsystem `hle/` directories (`ee/hle`, `iop/hle`, …) must annotate any knowing divergence from SDK 3.0.1 semantics with a one-line `// diverges:` comment + RecordId [REV 2026-09-22 D3: paths updated] |
| L7 | **Pipelines/shaders precompiled, keyed by content hash, never compiled on the draw path**; on weak GPUs this is a requirement, not a luxury (XenosRecomp cache, UnleashedRecomp async pipeline compilation, vitaGL shader-compiler cost) | ARCHITECTURE §6 adds the pipeline-cache contract: the DrawIR raster-state space is finite and enumerable from corpus; backends precompile at boot/load via a hash-keyed cache; MIGRATION Phase 10 [REV: renumbered] makes the cache a Vulkan/GXM entry criterion + risk row R13 |
| L8 | **External instruction-test corpus as recompiler oracle** (XenonRecomp runs Xenia's PPC tests) | MIGRATION Phase 8 [REV: renumbered] proof gate extended with an optional external EE instruction-corpus arm (e.g. ps2autotests-class suites) alongside our differential sweeps |
| L9 | **Golden reference outputs in CI** catch emission drift cheaply (OpenGOAL `_REF.gc` files) | ARCHITECTURE §10 + MIGRATION Phase 8 [REV: renumbered]: golden-emission snapshot tests — frozen emitted-C++ for a fixed function set, diffed on every recompiler change (regen-determinism already exists; this adds reviewable content pins) |
| L10 | **Bug-compatibility is a policy with an evidence bar** (UnleashedRecomp: original bugs preserved; reports need original-HW footage) | ARCHITECTURE §1 non-goals cites the precedent; our accepted-difference protocol (EQUIVALENCE §7) is the same policy — PCSX2/ps2tek/hardware evidence required to change OLD behavior |
| L11 | Engine/game split is the **port-enabling seam** (RSDK engine vs game repos; LUS across SoH/2Ship) | [REV 2026-09-22 D2/D3: the seam is no longer a directory pair (`runtime/` vs `hle/game`) — ARCHITECTURE §3 rule 2 now carries it as the subsystem-HLE rule: game knowledge enters only as data (recompiled output, signature matches, fixtures), so ports still touch platform/backends only] |
| L12 | **Optimization flags staged after correctness**, quantified wins, off by default until oracle-green (XenonRecomp) | ARCHITECTURE §5.2 notes the staging policy for future IR optimizations (register localization etc.) — always behind the oracle gates, never bundled with behavior changes |
| L13 | Vita ports demand an **explicit memory plan** (newlib heap sizing, custom allocators, fixed budgets) (d3es-vita, rvgl-vita, vitaGL HAVE_CUSTOM_HEAP) | MIGRATION Phase 10 [REV: renumbered] Vita spike expanded: memory budget table (32 MB guest RAM + mirrors + caches vs ~300 MB budget), custom-heap plan, and the pipeline-precompile requirement |
| L14 | A software renderer as a **first-class backend** doubles as oracle and floor (DuckStation; Dolphin SW) | Already in ARCHITECTURE §6 (`render/backends/soft`); DuckStation precedent cited to protect its priority |

### 5.3 What we deliberately do differently

- **Stronger equivalence machinery than any reference.** None of the studied
  projects run a two-binary deterministic frame/audio/memory-hash harness; they
  rely on literal translation + community soak. We keep ours (EQUIVALENCE_PLAN)
  because our refactor must prove behavior-identity, not just "works".
- **No mod/asset-archive layer now.** LUS/Zelda64Recomp/UR invest heavily in
  modding surfaces; our §1 non-goals defer this. The DrawIR + hook-manifest
  seams are where such a layer would attach later without redesign.
- **HLE'd VU1 library instead of microcode recompilation.** The N64 family
  statically recompiles RSP microcode; our AOT tier was measured (233 MB inc /
  351 MB lib, NOAOT record) and deleted in favor of the OpenGOAL-shaped
  HLE-program + reference-interpreter ladder.
