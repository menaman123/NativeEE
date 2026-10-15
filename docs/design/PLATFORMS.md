# Platforms — the host matrix, the shim header, and the recompiled-code build facts

Standing Charter 2, Track B. The one place that states what each host target has, what it
lacks, and the measured facts the build decisions rest on. Owners: B1 Linux (Herschel),
B2 Windows (Socrates), B3 CI (Socrates, wave 2; section 6).

## 1. Matrix

| Target | Compiler | Presets | Platform arm | GL | Status (2026-10-12) |
|---|---|---|---|---|---|
| macOS arm64 / x86-64 | Apple clang | `macos-dev`, `macos-diag`, `macos-release` | `engine/platform/macos/` + `host/host_posix.cpp` | OpenGL.framework (4.1 core, `<OpenGL/gl3.h>`) | the dev and acceptance platform; Release 0/0/0, suite green |
| Linux x86-64 / aarch64 | GCC 12+ / Clang 15+ | `linux-*` (B1) | `host/host_posix.cpp` (+ B1's posix move of the macos folder) | Mesa (`libGL`) | B1 in progress |
| Windows x64 | clang-cl (release); cl.exe compile check | `windows-dev`, `windows-release`, `windows-msvc-dev` (+ the flag shim launcher `tools/build/clang_cl_shim.py`) | `host/host_windows.cpp` | opengl32.dll + a loader (gap, section 4) | groundwork: shims, presets, flag shim + contract; the build itself is ci.yml's first run (section 6) |

## 2. The ONE shim header — `engine/platform/host/platform.h`

ARCHITECTURE section 16: no `#if` on the host outside `engine/platform/`. The header has
four sections with two owners; the two arms are `.cpp` files (the one-class lint keeps
header bodies trivial):

| Section | Content | Owner |
|---|---|---|
| 1 compiler shims | `PS2R_PRINTF_FORMAT`, `PS2R_FORCE_INLINE/NOINLINE/UNREACHABLE/LIKELY/UNLIKELY`, `Ctz64/Clz64/Popcount64` (defined at 0), `Mul64x64` (the one 64x64->128 product), `FrameAddress`, `AlignedAlloc/AlignedFree` | shared (B2 wrote it) |
| 2 host API | fd I/O (`FdOpen/Read/Write/Pread/Sync/Close/OfStream`, `DirectorySync`), paths (`MakeDirectory`, `IsDirectory`, 64-bit seek/tell), process (`ProcessId`, `ExecReplaceProcess`, `ProcessFootprintBytes/Peak`), threads (`ThreadCreate` with a stack size, `ThreadJoin`, `SetCurrentThreadName`, `PinCurrentThreadInteractive`), `MonotonicNowNs`, `Backtrace/BacktraceSymbolsFd/SymbolForAddress`, coroutines (`CoroutineCreate/Current/Switch/Destroy`), `HostArmName` | shared |
| 3 Windows arm | `host_windows.cpp` (UCRT + Win32: `_sopen_s`, `ReadFile`+`OVERLAPPED`, `_beginthreadex` with `STACK_SIZE_PARAM_IS_A_RESERVATION`, `SetThreadDescription`, QPC, `CaptureStackBackTrace` + DbgHelp, native fibers, `GetProcessMemoryInfo`) | B2 |
| 4 POSIX arm | `host_posix.cpp` (the direct calls; Darwin QoS / `phys_footprint` under the one OS `#if`; fibers delegate to `FiberMacos`) | B1 |

The contract test `tests/platform/test_host_platform.cpp` (ctest `PlatformHostShims`) runs
unchanged on every arm; it is green on the POSIX arm (`host platform arm=posix
failures=0`) and is the first test the Windows CI job must pass.

128-bit: the EE register file (`engine/core/types.h` `u128`/`s128`, `engine/ee/cpu/cpu_state.h`)
is two 64-bit halves on every compiler; the generated shards use no `__int128` and no
`__builtin_*` (grep over `recompiled*.cpp`: 0), and the generator already guards
`<execinfo.h>` on `__APPLE__ || __GLIBC__`. Alignment is `alignas(16)` throughout
(standard). The engine's own non-portable spellings to migrate onto the header (B1/B2
caller moves, 14 files): `__builtin_ctzll` (gs/xfer/transfer_engine.cpp),
`__builtin_frame_address` (core/diag/word_watch.cpp), `__attribute__((format))`
(core/log/logger.h, core/diag/bench_report.cpp), `<unistd.h>/<fcntl.h>` fd calls (core/log/
ring_log.cpp, core/diag/bench.cpp, core/triage/triage_bundle.cpp, games/.../saves/
save_storage.cpp), `<execinfo.h>`/`<dlfcn.h>` (triage_bundle.cpp, word_watch.cpp,
platform/macos/crash_handler.cpp), `<pthread.h>` (ee/hle/thread_manager.h,
vu/tiers/mtvu_worker.h, app/host/session_runtime.cpp), `<mach/mach.h>` (app/host/
leg_state.h), `execv` (app/host/main.cpp).

What the Windows arm does not provide, and how a caller degrades (header section 3):
private working set instead of `phys_footprint` (the peak_mb pins are platform-keyed);
spawn + exit instead of `execv` (the relaunch gate must not pin the pid); DbgHelp symbols
need the `.pdb` beside the exe; no directory fsync on NTFS (`DirectorySync` reports success
after checking the directory exists).

## 3. The recompiled code on each compiler

`games/<title>/recomp/CMakeLists.txt` maps the semantic flags per compiler front end:

| Front end | Shards | Common TU | Notes |
|---|---|---|---|
| GNU (clang, gcc, Apple clang) | `-O2 -w -fno-strict-aliasing -fwrapv -ffp-contract=off` | `-O0` + the same | the installed configuration |
| clang-cl | `/O2 /w /clang:-fno-strict-aliasing /clang:-fwrapv /clang:-ffp-contract=off /bigobj` | `/Od` | the Windows release compiler |
| cl.exe | `/O2 /w /fp:precise /bigobj` | `/Od` | configure WARNING: no `-fwrapv` equivalent — compile check only, never a release |

The shards build as one OBJECT library with a precompiled prologue
(`engine/ee/recomp_abi/anchor/recomp_pch.h` after the generated header;
`PS2R_RECOMP_PCH=OFF` disables it). The PCH changes no object byte: all 32 Release shard
objects are identical with and without it (`cmp`, 2026-10-12), so the linked binary and
the deterministic lanes are unchanged by construction.

### Per-TU compile time (Apple M4, Apple clang 21, Release flags, 2026-10-12)

| TU | Bytes | Isolated, machine lightly loaded | Inside `-j4` build (ninja log) | Isolated, machine at load ~11 (other owners building) |
|---|---|---|---|---|
| `recompiled_shard_028.cpp` (largest of 32) | 4,296,958 | 7.8 s | 11.1 s | 12.4 s |
| typical shard (of 32) | 2.3–3.5 MB | — | 6–10 s | — |
| `recompiled.cpp` (common TU, `-O0`) | 3,135,370 | 34.7 s | 46.4 s | — |
| `recompiled_shard_056.cpp` (largest of a 64-way split) | 2,623,540 | — | — | 14.2 s |
| includes-only prologue (what the PCH removes) | — | 0.55 s | — | — |

Every TU is under the 60 s bound at the installed 32-way split, with the common TU the
slowest at `-O0`. A 64-way split halves the largest file but not the largest compile:
the heavy functions (`FUN_002a4b30`, 5,711 lines; `FUN_002a0bb0`, 3,072) land in one TU
whatever the count, and the measurement above puts the 64-way maximum at the same order
as the 32-way one. The split therefore stays at 32; the knob exists (`PS2R_RECOMP_SHARDS`
→ `--shards`) and a re-split is PROVABLY body-identical:

```
tools/recomp_shard_bodies_sha.py games/<title>/recomp --against /tmp/r64
00bc662b374b5ea15b6e81c073aeca35f76c2f12a4832e9e620b6a876de99117  bodies(32 shards, 92387399 B)
00bc662b374b5ea15b6e81c073aeca35f76c2f12a4832e9e620b6a876de99117  bodies(64 shards, 92387399 B)
IDENTICAL bodies across 32 vs 64 shards
```

(`recompiled.h` and `recompiled.cpp` byte-identical between the two; `continuations.h`
differs only in its per-shard sha comment list.) What a re-split costs: 121 registrations
under `tests/`, `app/gui/`, `tools/` name shard files by number
(`recompiled_shard_NNN.cpp` — the oracle generators and the per-shard test links), so the
installed count moves only together with a function-keyed lookup for those references
(refactor queue; the shard-028 size-budget allowlist entry retires with it).

Windows expectation (unmeasured here — CI measures): clang-cl is clang, so the per-TU
times scale with the runner's core speed (a GitHub `windows-2022` runner is roughly 2–3×
slower per core than the M4: expect 20–40 s per shard, 90–120 s for the `-O0` common TU).
If the common TU is the problem on Windows, the lever is the generator (the init body's
8,647 registrations), not the shard count.

## 4. What the Windows job's FIRST RUN will show, in order (ci.yml `windows-x64`)

No Windows machine or VM is available on the dev host and the repository has no git remote,
so the workflow has never executed. Since 2026-10-12 (evening) the Windows-only compile errors
a macOS host CAN catch are caught before CI by a **cross-compile smoke**: `tools/build/
win_cross_smoke.py` (ctest `PlatformWindowsCrossSmoke`, SKIP 77 without the prerequisites)
re-issues the dev host's compile commands with Homebrew LLVM 23's clang for
`--target=x86_64-pc-windows-msvc` against an `xwin splat` of the MSVC 14.44 CRT and the Windows
11 SDK 10.0.26100 headers (the same headers clang-cl sees on the runner; `cargo install xwin`,
`xwin --accept-license splat --output <dir>`, 629 MB) and writes COFF objects. With `--lld-link`
(zig's bundled lld-link here; Homebrew has no `lld` under the sandbox) it also links the
`PlatformHostShims` contract test against the Windows arm into a PE32+.

What is OBSERVED on the dev host (not run — compiled and linked):

| TU set | Result |
|---|---|
| `engine/platform/host/platform.h` as a standalone Windows TU, `-Wall -Wextra -Werror` | compiles |
| every `engine/core/*.cpp` (30 TUs, the Release set) | compiles |
| `host/host_windows.cpp`, `host/platform.cpp`, `platform.cpp`, `windows/platform_windows.cpp` | compile; link into `test_host_platform.exe` (PE32+, 45 KB) with every import resolved: KERNEL32 (fibers, QPC, module/file attributes), the UCRT api-sets (`_sopen_s`, `_beginthreadex`, `_aligned_malloc`, `_stricmp`, `_gmtime64_s`, `_spawnv`, `_mkdir`), VCRUNTIME140, dbghelp (`SymInitialize/SymSetOptions/SymFromAddr`) |
| `recompiled_shard_000.cpp` (`-O2 -fwrapv -fno-strict-aliasing -ffp-contract=off`) | compiles, 3.3 s, 1.5 MB COFF |
| `recompiled_shard_028.cpp` (the largest) | compiles, 13.0 s, 2.9 MB COFF, 967 sections |
| `recompiled.cpp` (the common TU, `-O0`) | compiles, 5.0 s, 11.6 MB COFF, 3,998 sections (no `/bigobj` needed) |
| sweep: every `.cpp` under `engine/ app/ games/ tools/ third_party/` with `-fsyntax-only` | **660 / 692** compile; the 32 that do not are listed below |

Errors the smoke caught and FIXED (all on 2026-10-12, commits on `port-windows`):
`host/platform.cpp` had an `#error` for the Windows forwarders and an unconditional
`<execinfo.h>`; `windows/platform_windows.cpp` wrote `host::` inside `ps2r::platform`, which
names the target-folder API, not the arm (19 errors), lacked the three statics the forwarders
call, and hit the UCRT's `fopen` deprecation under `-Werror`; the UCRT/`<windows.h>` compile
definitions every TU needs were missing (`_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS
_USE_MATH_DEFINES NOMINMAX WIN32_LEAN_AND_MEAN`, root CMakeLists `if(WIN32)`); the core
callers on `<strings.h>` (logger, ring_log, config), `<unistd.h>`/`<fcntl.h>`/`<execinfo.h>`/
`gmtime_r` (ring_log, triage_bundle) and `<pthread.h>` (`ee/hle/thread_manager.h`,
`vu/tiers/mtvu_worker.h` — the first error of 14 app/engine TUs) now go through the host
header, whose section 2 gained `StrCaseCmp/StrNCaseCmp`, `GmTimeUtc`, `ExecutableImagePath`
(both arms). To let `ps2r_core` depend on the arm without a cycle, the arm (+ the Platform
registry and the POSIX fibers) is its own library `ps2r_host` below `ps2r_core`.

What the FIRST RUN will still show, in order:

0. **Flag spellings — PRE-EMPTED** (`tools/build/clang_cl_shim.py`, ctest
   `PlatformClangClShimContract`): `-Wall` IS cl's `/Wall` under clang-cl; the launcher maps
   the 17 per-target flag sets (0 failures). The cross-compile smoke uses the GNU driver with
   `--target=x86_64-pc-windows-msvc`, i.e. the same front end and the same MSVC ABI/STL as
   clang-cl, so its diagnostics are clang-cl's.
1. **vcpkg manifest install** (`sdl2`, `ffmpeg[avcodec,swscale]`): FFmpeg from source on a
   cold cache, 30–60 min on `windows-2022`; ci.yml restores/saves the binary cache around
   configure. **Reveals:** whether the two ports build on the runner and the cold time.
2. **`PS2R_GL_HEADER` is undefined on `_WIN32`** (`engine/platform/host/platform.h`, by
   design) — **the first compile error CI will print**: the 7 `engine/render/backends/gl/*.cpp`
   TUs (+ `engine/platform/sdl/gl_context.cpp`, `app/host/{session_window,video_stack}.cpp`
   and the GL tests behind SDL). Windows' opengl32.dll exports GL 1.1 only — every core 4.1
   entry point needs `wglGetProcAddress`: `<GL/glcorearb.h>` (vcpkg `opengl-registry`) +
   function pointers for the ~87 entry points the backend uses, loaded after
   `SDL_GL_CreateContext`. Owner decision pending (Zeno owns the backend).
3. **Six callers still on POSIX headers** (the sweep's remaining list; each a "file not found"
   or an undeclared name): `app/host/main.cpp` (`<unistd.h>`), `app/host/savestate_slots.cpp`
   (`<unistd.h>`), `app/host/first_run.cpp` (`::mkdir` → `host::MakeDirectory`),
   `app/host/launcher_settings.cpp` (`<dirent.h>` → `std::filesystem::directory_iterator`),
   `app/host/clock_census.cpp` (`CLOCK_THREAD_CPUTIME_ID` → a `host::ThreadCpuTimeNs`
   to add with both arms: `clock_gettime` / `GetThreadTimes`), `tools/vu1_replay/vu1_replay.cpp`
   (`<dirent.h>`). The `app/host` files are Track A/C's active files (Bernoulli: launcher,
   first run, save slots) and were left to their owner; the fix for each is one include +
   one call through section 2. `engine/core/diag/{bench,word_watch}.cpp` (`PS2R_DIAGNOSTICS`,
   OFF in the windows presets) keep `<unistd.h>` / `<dlfcn.h>` / `__builtin_frame_address`.
4. **SDL TUs** (16): the sweep sees Homebrew's macOS `SDL_config.h` and stops there — no
   signal either way; on the runner they compile against vcpkg's `SDL2`. `third_party/imgui`'s
   `imgui_impl_opengl3_loader.h` carries its own loader and compiles.
5. **Per-TU compile time** (section 3): the smoke's x86-64 codegen on the M4 is ~1.3–1.7× the
   arm64 build's (shard 028: 13.0 s vs 7.8 s); on a `windows-2022` runner expect 20–40 s per
   shard, 15–30 s for the common TU (it is 5.0 s here at `-O0` since the compact init table);
   `-j3`.
6. **Link inputs**: `find_package(SDL2 CONFIG)`, `find_package(OpenGL)` (opengl32.lib),
   `dbghelp psapi` for the Windows arm (observed resolving here against the SDK import
   libraries) — through the vcpkg toolchain / the SDK.
7. **`build.ps1`**: parsed by PowerShell 7.4.6 (0 errors) and its staging path run end to end
   against a stub build dir on macOS, incl. both refusal paths (section 5). Windows PowerShell
   5.1's native-stderr termination is handled (`Invoke-Native`).
8. **Paths**: `/`-joined paths work on Win32; the user data dir (`%APPDATA%`) is C4's.
9. **Running anything** (`PlatformHostShims` on the Windows arm, the component suite, the
   title): only the runner can; the arm's `.exe` was linked, not executed.

How B2 closes: item 2 (the loader: one decision + one header + one call site) and item 3 (six
callers, mechanical) are the only code; items 1/5/9 are measured by the run; then the lane is a
gate like the others and `build.ps1 -SkipBuild -Dist` + the asset guard stage
`dist/windows-x86_64/`.

## 5. The developer path (Track C3)

`./build.sh` (macOS/Linux) and `.\build.ps1` (Windows): configure the host's Release
preset, build, run the component suite (the `Root*`/`Og*` game-file groups excluded),
stage `dist/<platform>/` — binary, bundled SDL2 + FFmpeg (macOS: the non-system dylib
closure with install names rewritten to `@rpath` and the executable's rpath replaced by
`@executable_path/lib`, ad-hoc re-signed; Linux: the `ldd` closure with `$ORIGIN/lib` via
patchelf; Windows: vcpkg's applocal DLLs), `BUILD-ID`, the user half of `README.md`,
`LICENSE`, `THIRD_PARTY_NOTICES.md`. The staging step refuses a folder carrying anything
that looks like game data (C1's asset scan is the authority). Verified on macOS
2026-10-12: `dist/macos/nativeee --version` loads SDL2/avcodec/avutil/swscale from
`dist/macos/lib` (`DYLD_PRINT_LIBRARIES`), 14 bundled dylibs, 163 MB (binary 137 MB + lib/ 32 MB;
the release zip of it is 33 MB).

`build.ps1` reviewed line by line against `build.sh` (2026-10-12 evening) and exercised with
PowerShell 7.4.6 on macOS: 0 parse errors; `-Help`; the prerequisite check throws; the staging
path (`-SkipBuild -Dist`) against a stub build dir produced `dist/windows-x86_64/` (exe, DLLs,
`title/`, `BUILD-ID` via the exe's `--version`, README user half, LICENSE placeholder,
`THIRD_PARTY_NOTICES.md` from the DLL list, vcpkg `copyright` files under `licenses/`) and the
zip, both asset-scanned CLEAN; a planted game-data blob → `asset_scan` 125 findings → REFUSING,
exit 1; a missing `SDL2*.dll` → "NOT self-contained", exit 1. Parity fixes: `-NoTests` builds
only `nativeee` (as `--no-tests`); the warning count reads `warning:` and `lld-link: warning`;
`binaryDir` follows `inherits`; `PS2R_TITLE` honoured; `Invoke-Native` keeps Windows PowerShell
5.1 from terminating on a native command's first stderr line under `2>&1` (every compiler
warning would have killed the build step); clang-cl from `VC\Tools\Llvm` is put on PATH when
`VsDevCmd` did not. **Not verified here:** the configure/build/test half (needs clang-cl, vcpkg,
the SDK) and `Compress-Archive` under Windows PowerShell 5.1 (backslash entry names; CI uses
pwsh 7).

## 6. CI (Track B3) — `.github/workflows/{ci,release,linux-gcc}.yml`

No GitHub remote exists on the dev host; the workflows are authored and validated locally with
`actionlint` (+ shellcheck) and by running their bash steps by hand on macOS. The first real run
is the first push to a GitHub remote.

`.github/actionlint.yaml` declares the macOS runner labels (`macos-15-intel`, `macos-14`) under
`self-hosted-runner.labels`. actionlint's `[runner-label]` rule checks `runs-on` against a table
of GitHub-hosted labels baked into the `rhysd/actionlint` image, so when GitHub retires a runner
image the label leaves that table and the `guard` job fails on the lint error alone -- and because
`build` is `needs: guard`, that takes the whole matrix down (the retired `macos-13` image did
exactly this). Listed labels are accepted regardless of the image's table, so an upstream
retirement becomes a real runner error on the one lane that uses it. A listed label is no longer
spell-checked, so the list stays short: the stable labels (`ubuntu-24.04`, `ubuntu-24.04-arm`,
`windows-2022`) are left to the rule to police.

**ci.yml** (push `main` / PR / dispatch / `workflow_call`): `guard` → `build` (5 targets) →
`identity`.

| Target | Runner | Preset | Suite | Artifact |
|---|---|---|---|---|
| macos-arm64 | `macos-14` | `macos-release` | `ctest -E '^Root'` | `dist/macos/` |
| macos-x86_64 | `macos-15-intel` (the x86-64 label that succeeded the retired `macos-13`) | `macos-release` | same | `dist/macos/` |
| windows-x64 | `windows-2022` | `windows-release` (clang-cl + shim, vcpkg) | same | `dist/windows-x86_64/` (`build.ps1 -SkipBuild -Dist`) |
| linux-x86_64 | `ubuntu-24.04` | `linux-release-clang` | same under `xvfb-run` (llvmpipe) | `dist/linux-x86_64/` from the `linux-dist` job (C1: `./build.sh` in the `tools/ci/linux.Dockerfile` toolchain image = the static `linux-dist` preset) |
| linux-aarch64 | `ubuntu-24.04-arm` | `linux-release-clang` | same | `dist/linux-aarch64/` from the `linux-dist` job |

Every job: a partial + sparse checkout (`filter: blob:none`; negative patterns for
`**/probe/*.ps2st`, `**/gsdumps/*.gs.zst`, the console shots, `**/title_screen/*.gsf1` — the
game-derived oracle images the local Root legs use; the tests that need them SKIP 77), then
`tools/ci/asset_guard.py --root . --mode ci` which FAILS on any class A (disc / extracted game
file, by name and by magic at fixed offsets) or class B path. The Release build is gated at
0 FAILED / 0 `warning:` / 0 `ld: warning` like the dev host. `dist/<platform>/` is scanned with
`--mode artifact` before upload. Game files are never in CI by construction AND by check.

**The identity CI can check without the disc.** The padlog-replay lanes (frames/audio/drawir/
gsops/mem, `verify/platform_identity.py`) need the extracted game files and a game process:
local only (`games/<t>/verify/platform_ref/README.md`). CI's identity claim is the component
suite's deterministic oracles: the six math library oracles that need no game file
(Generated, Collision, Trig, Animation, Bounds, Clipping) run with `--digest` (configure
`-DPS2R_MATH_ORACLE_DIGEST_DIR`): the exact byte streams their DT-12 receipts would hold —
synthesized inputs, the model (generated NTSC bodies) / selected (native MathLibrary) / control
(captured originals) arms' outputs, the counter streams, the fixtures, the completion record —
folded into one SHA-256 per channel (49 keys). `verify/component_identity.py --collect` writes
the `oracles` lane; the `identity` job compares every target with macos-arm64 through
`platform_identity.py` and fails on the first divergent `<Oracle>/<channel>`. Two hosts with
identical lanes ran the same arithmetic (`-ffp-contract=off`, `-fwrapv`, the rounding-mode
sections, the generated code) on the same inputs. Measured on the dev host: two reps IDENTICAL
(49 keys); a one-digit mutation DIVERGENT naming `MathLibraryTrigOracle/selected.outputs.bin`;
the digests equal `shasum -a 256` of the `--artifacts` files.

**release.yml** (tags `v*`): `uses: ./.github/workflows/ci.yml` (the matrix, the suite and the
identity job gate the release), then one zip per target (`nativeee-<tag>-<target>.zip` = the
scanned `dist/<platform>/`), `SHA256SUMS`, notes with the per-target BUILD-ID, a GitHub release.

**linux-gcc.yml**: the GCC lanes (manual + weekly, `continue-on-error`), red by construction on
main f8cb78cd (GCC 13 exceeds 9.7 GiB on `recompiled.cpp`'s one-function init table; CHARTER2_B1.md).
Herschel's B1 fix is the generator's `--init-parts` knob with `&FUN_` registrations (port-linux
`c836a8bd`; splitting into part functions alone still peaked at 13.3 GiB — the 17,600 closure types
are the cost, not the function length; the compact form: GCC 13.3 in 23 s / 887 MB, observed). Once
it integrates, this lane joins the `ci.yml` matrix; not gating before.

Expected first-run state per lane: macOS arm64 green (the dev host's own build and suite);
macOS x86-64 green or "no runner" if the label is retired; Linux clang lanes red on
`Render_Present` check 163 (llvmpipe's bilinear weights; B1 follow-up, Herschel) and otherwise
green per the colima run (716/717); Windows red at section 4 items 1–3 in that order.
