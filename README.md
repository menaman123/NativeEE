NativeEE — run PS2 games natively: the game's code, a native engine underneath. First title: the title (NTSC-U), first level playable. You must own the game and supply your own disc image.

# NativeEE

NativeEE statically recompiles a PlayStation 2 game's own executable into native code and runs it
on a native engine that replaces the PS2 hardware and operating system: graphics, sound, input,
disc, memory cards and the clock are native services, the game logic is the retail game's. It is a
port, not an emulator. The first and so far only title is *the title* (SLUS-12345, the US
release) on Windows / macOS / Linux.

**You must own the game.** This repository and every build of it ship no game data of any kind —
no textures, audio, models, movies or disc bytes, and no code derived from the game: the
recompiled program is generated at build time from *your* disc image and never committed. Only the
US release (SLUS-12345) is supported; PAL and other regions are refused with an exact message.

Licence: MIT (`LICENSE`). Third-party components: `THIRD_PARTY_NOTICES.md`. Contributing:
`CONTRIBUTING.md`. Security reports: `SECURITY.md`.

---

## Users

### 1. Download

Take the folder for your platform from the release (one zip per platform) and unpack it
anywhere:

| Platform | Folder | Contents |
|---|---|---|
| Windows x64 | `nativeee-windows-x86_64/` | `nativeee.exe` + the runtime DLLs (SDL2, FFmpeg), `title/`, `README.md`, `LICENSE`, `THIRD_PARTY_NOTICES.md`, `licenses/`, `BUILD-ID` |
| macOS (Apple silicon / Intel) | `nativeee-macos/` | `nativeee` (SDL2 and FFmpeg compiled in), `title/`, `README.md`, `LICENSE`, `THIRD_PARTY_NOTICES.md`, `licenses/`, `BUILD-ID` |
| Linux x86-64 / aarch64 | `nativeee-linux-<arch>/` (`.tar.gz`) | `nativeee` (SDL2, FFmpeg, libstdc++ compiled in), `title/`, `README.md`, `LICENSE`, `THIRD_PARTY_NOTICES.md`, `licenses/`, `BUILD-ID` |

Nothing else needs installing. The folder is self-contained; the OpenGL driver is your
system's own.

**macOS, first start only (Gatekeeper):** the build is signed ad hoc, not notarised, so a
zip downloaded by a browser carries the quarantine mark and Finder refuses to open
`nativeee` ("cannot be opened because the developer cannot be verified"). Either
right-click `nativeee` → *Open* → *Open* once, or clear the mark from a terminal:

```sh
xattr -dr com.apple.quarantine nativeee-macos/
```

Starting it from a terminal (`./nativeee`) works without either step. (This is the one
friction point of the first start that is not ours to remove; the measured first-run drill
is `docs/design/FIRST_RUN_DRILL.md`.)

### 2. Drop in your disc image

Make a disc image (`.iso` or `.bin`) of your own *the title* (NTSC-U, SLUS-12345)
disc with any disc-imaging tool. Then start `nativeee`: the **first-run launcher** asks
for the image — drop it on the window or type its path — verifies it (an ISO 9660 volume,
`SLUS_123.45` present, the game's ELF SHA-256 equal to the one this build was made from;
PAL, NTSC-J, other titles and damaged images are refused with the exact reason), extracts
the game's files into your user data folder with a progress bar (the disc image is copied
there by default — a `.bin` becomes a plain `.iso` — or referenced in place if you untick
the copy), creates the memory cards and default settings, lists your controllers, and
offers *Play*. Your own image is only ever read.

Afterwards the same window is the **settings screen** (Display, Controls, Save states,
Fixes, Data pages, then *Play*). It is skipped on later starts once the files exist; the
Data page's "Show this screen on every start" brings it back, and so does
`./nativeee --launcher`. A disc can also be extracted without a window:
`./nativeee --extract-disc "/path/to/the title.iso"`.

Your data lives in your user data folder, never beside the binary:

| Platform | Folder |
|---|---|
| Windows | `%APPDATA%\NativeEE\` |
| macOS | `~/Library/Application Support/NativeEE/` |
| Linux | `$XDG_DATA_HOME/NativeEE/` (default `~/.local/share/NativeEE/`) |

It holds the extracted game files (`game_files/`), your settings (`ps2r.ini`,
`bindings.ini`), memory cards, save states and texture packs. Each extracted set records
the `BUILD-ID` it came from; a newer build warns when the files are from another build and
offers to re-extract.

A fresh machine with only the zip and a disc image reaches the beach of the first level in
about three minutes (unzip 0.1 s, verify + extract ~2 s for a 2 GB image on an SSD, then
the game's own intro movies and first-level load); the measured drill is in
`docs/design/FIRST_RUN_DRILL.md`.

### 3. Play

Start `nativeee` (double-click or from a terminal). Keyboard and mouse work out of the
box (W/A/S/D move, mouse camera, Space jump, LShift spin, E triangle, Y start, Tab select,
arrow keys dpad); any
SDL2-recognised game controller (DualShock, Xbox, Switch Pro) is picked up with the
standard layout — A cross, B circle, X square, Y triangle, triggers L2/R2. Every pad
button and every key can be rebound on the launcher's **Controls** page: click the PS2
button, press the pad button, trigger or key you want for it (a control already on
another row moves; Escape cancels, Backspace unbinds a key). Stick deadzone and
sensitivity, rumble and the keyboard-as-pad toggle live there too; everything is written
to `bindings.ini` in your user data folder. In game:

| Key | What |
|---|---|
| Esc | the game's pause menu; its *Options* carry the Display and Controls pages |
| F5 / F8 | save state / restore state (the slot is a setting; the launcher's Save states page lists them) |
| START or CROSS during a movie | skip it (the attract movie included) |
| R2 held during a cutscene | fast-forward (the factor is a setting; off by default) |
| F1 | the dev overlay (census, frame rate / game rate, save-state tools) |

Game speed is a fixed 60 Hz regardless of your display's frame rate; the renderer draws
as fast as it can and drops frames rather than slowing the game (the clock contract,
DT-14).


### Input devices (automatic)

The input device is automatic: a connected gamepad is used when present, keyboard and mouse are
ALWAYS live as a pad, and the ACTIVE source is whichever produced the last event (typing or
unplugging a pad mid-game takes over instantly; hot-plug both ways). Default keyboard layout:

| PS2 control | Key | PS2 control | Key |
|---|---|---|---|
| left stick | W / A / S / D | right stick (camera) | mouse |
| cross | Space | circle | right mouse button |
| square | LShift | triangle | E |
| L1 / L2 | 1 / 2 | R1 / R2 | 3 / 4 |
| L3 / R3 | 5 / 6 | start | Y |
| select | Tab | dpad up / down / left / right | arrow keys |

(For the harness: `run.meta`'s `input_sources=` row is the census of that contract.)

### Something went wrong?

A crash writes a *triage bundle* into your user data folder under `logs/triage/<time>/`
(the exit census, the last frames of draw records, a native backtrace, the error text).
Attach that folder to a report together with `BUILD-ID`. Every replacement the engine
makes for a PS2 subsystem can be turned back off for one run with
`--set hle.<key>=off` (see `nativeee --help`); if that cures a bug, say which key.

### Licence

NativeEE's own code is MIT licensed (`LICENSE`). Bundled libraries — each named with its licence
and how it is linked in `THIRD_PARTY_NOTICES.md` (the artifact carries its own generated copy and
the texts in `licenses/`): SDL2 (zlib); FFmpeg libavcodec/libavutil/libswscale (LGPL v2.1 or later,
built by `tools/ci/ffmpeg_static.sh` with only the MPEG-1/2 video decoders and no GPL or non-free
component — `licenses/FFmpeg-BUILD-CONFIGURATION.txt` quotes the configure line as embedded in the
shipped binary by libavcodec, i.e. what was actually linked, checked by the build against the
recipe; statically linked, relinkable from this repository); Dear ImGui (MIT) and its embedded gl3w
OpenGL loader (public domain); on Linux the static libstdc++/libgcc (GPLv3 with the GCC Runtime
Library Exception). *the title* is the property of its rights holders; this project contains
none of its assets and asks you to own the game.

---

## Developers

### 1. Build without a disc — the engine and its tests

No game file is needed for the engine, the recompilers, the tools and the component test suites:

```sh
cmake --preset macos-release                   # linux-release / windows-release: the same shape
cmake --build --preset macos-release -j8
ctest --preset macos-release --timeout 900 -j8 -E '^Root|^Og'
```

Without a disc image or title ELF, cmake prints one `SKIPPED` status line per title target: the
recompiled program, the title module, the play binary (`nativeee`), the oracle binary
(`nativeee_oracle`) and every test that needs them are simply not registered. Everything else —
`engine/` (the title-neutral PS2 semantics and host services), `tools/recompiler_v2`,
`tools/iop_recompiler`, `tools/disc_extract`, the lints and the per-subsystem component suites —
builds and runs. `./build.sh --no-dist` (`.\build.ps1 -NoDist`) does the same in one command.
Tests that need the extracted game files (`Root*`) or the frozen original tree (`Og*`, env
`PS2R_OLD_ROOT`) skip with ctest's SKIP code (77) rather than fail.

Prerequisites:

- **macOS:** Xcode command line tools (Apple clang), CMake 3.21+, Ninja, Python 3.8+,
  `brew install sdl2 ffmpeg`.
- **Linux:** GCC 12+ or Clang 15+, CMake, Ninja, Python 3, SDL2 and FFmpeg development
  packages (`libsdl2-dev libavcodec-dev libavutil-dev libswscale-dev`), Mesa GL; `patchelf`
  for the staging step.
- **Windows:** Visual Studio 2022 Build Tools with the *C++ Clang tools for Windows*
  component (clang-cl + Ninja), CMake 3.21+, Python 3, and a [vcpkg](https://github.com/microsoft/vcpkg)
  checkout with `VCPKG_ROOT` set — `vcpkg.json` names the two dependencies (SDL2,
  FFmpeg with avcodec + swscale) and the `windows-*` presets pull them through the vcpkg
  toolchain. clang-cl is the release compiler: the recompiled code relies on `-fwrapv`,
  `-fno-strict-aliasing` and `-ffp-contract=off`, which reach it through `/clang:`; the
  `windows-msvc-dev` preset (cl.exe) is a compile check only. The first configure builds SDL2
  and FFmpeg from source through vcpkg (tens of minutes; cached afterwards).

### 2. Build with your disc — the game

The recompiled EE program and the recompiled IOP module are **generated into the build directory**
from the ELF (`SLUS_123.45`) and `DATA/SYS/STREAM.IRX` of your own disc image; neither is in the
tree. Give the build the image, or the loose files:

```sh
cmake --preset macos-release -DNATIVEEE_DISC="/path/to/the title.iso"
#   or   -DNATIVEEE_TITLE_ELF=/path/to/SLUS_123.45 [-DNATIVEEE_STREAM_IRX=/path/to/STREAM.IRX]
#   or the environment variables NATIVEEE_DISC / NATIVEEE_TITLE_ELF / NATIVEEE_STREAM_IRX
cmake --build --preset macos-release -j8          # extracts, recompiles, then builds the title
ctest --preset macos-release --timeout 900 -j8     # the full suite (Root* runs one game process at a time)
./build-release/app/host/nativeee --version
```

What the build does (`cmake/TitleInputs.cmake`, `games/<title>/recomp/CMakeLists.txt`,
`games/<title>/iop/stream_irx/CMakeLists.txt`):

1. **Extract** — `tools/disc_extract` reads `SLUS_123.45` and `DATA/SYS/STREAM.IRX` out of the
   ISO 9660 image (a `.bin` raw image works too). A loose ELF / IRX is used as given.
2. **The SHA-256 gate** — both files must hash to `games/<title>/TITLE_INPUTS.sha256`
   (`SLUS_123.45` `8d114879…038f`, `STREAM.IRX` `96ef95…6760`). Another revision, region or
   file is refused at configure time with the reason.
3. **Generate** — `ps2r_recompiler_v2 --shards 32 --init-parts 32 --direct-calls` over the ELF
   (with the committed `games/<title>/recomp/inputs/` stub lists in its cwd), then
   `continuations.py`; `ps2r_iop_recompiler` over the IRX. Output:
   `<build>/generated/games/<title>/recomp/` (`recompiled.h`, `recompiled.cpp`, 32 body shards,
   `continuations.h`) and `<build>/generated/games/<title>/iop/stream_irx/`.
4. **Verify** — every generated file must hash to the committed `MANIFEST.sha256` of its
   directory, so the binary you build is the binary this tree was developed and tested against
   (`-DPS2R_RECOMP_CHECK_MANIFEST=OFF` turns that into a warning while you change the generator).
5. **Compile** — the shards at `-O2` with the semantic flags (`-fwrapv -fno-strict-aliasing
   -ffp-contract=off`), the common TU at `-O0`, through a precompiled prologue
   (`engine/ee/recomp_abi/anchor/recomp_pch.h`; `PS2R_RECOMP_PCH=OFF` to disable; the object code
   is byte-identical either way). On an Apple M4 the largest shard takes about 8 s, the common
   TU about 35 s.

What IS committed for the title: the manifests, `INVOCATION.md`, `inputs/`, `continuations.h`
(regenerated and checked), the symbol map, the hand-written HLE and glue. Tests that read bodies
out of the shards use the generated directory (`PS2R_RECOMP_DIR`, `tests/recomp/recomp_paths.py`)
and exist only when the title is built. Regenerating the manifests after a generator change:

```sh
cmake --build --preset macos-dev --target ps2r_recompiler_v2 ps2r_iop_recompiler
NATIVEEE_TITLE_ELF=/path/to/SLUS_123.45 tools/regen_from_frozen.sh          # two runs, diff empty, MANIFEST
NATIVEEE_STREAM_IRX=/path/to/STREAM.IRX tools/regen_from_frozen.sh --iop    # the IOP module's
tools/regen_from_frozen.sh --check <build>/generated/games/<title>/recomp
```

The shard count is a generator setting (`PS2R_RECOMP_SHARDS=64 tools/regen_from_frozen.sh --out
/tmp/r64`; `tools/recomp_shard_bodies_sha.py` proves the bodies hash the same for every split).

### Per-title HLE — why every game needs its own work

NativeEE is a static recompiler, not an emulator, and that distinction is the whole reason this
section exists. Recompiling a title's EE code is the part that generalises: point the build at
another game's ELF and you get native code out. What does *not* generalise is everything the
game asks the console to do around that code.

The engine ships a title-neutral model of each PS2 subsystem — EE core, IOP, DMAC, GIF, VIF,
VU0/VU1, GS, CDVD, IPU, SPU2, pads, memory cards. Those models are correct, and they are the
**fallback** path: when nothing native is registered for what the game is doing, the engine
reproduces the hardware's behaviour instead. That is what makes an unported title run at all.
It is also slow, because reproducing a subsystem's behaviour costs far more than doing the
equivalent work natively — a VU1 microprogram interpreted register-by-register instead of a
native mesh transform, a GS register stream replayed into a software rasteriser instead of a
native draw call.

**So a new title boots on the fallbacks, and a native, performant title is the result of
replacing them subsystem by subsystem.** For each part of each game, somebody has to write the
HLE: recognise what the game is doing, then register a native implementation for it. Until that
happens for a given subsystem, that subsystem's work stays on the hardware model, and
performance reflects that. Per-title HLE is the bulk of the porting effort, and there is no way
to skip it — the fallbacks are a correctness floor, not a finish line.

Where the work lands, per title, under `games/<serial>_<slug>/`:

| Subsystem | What the fallback does | What the native HLE replaces it with | Lives in |
|---|---|---|---|
| **VU1** | Interprets the microprogram | A native routine per program, keyed by microcode hash | `vu1_library/` |
| **GS** | Replays the register stream into a rasteriser | Classified draws dispatched to native renderers | `renderer_profile/` |
| **EE functions** | Runs the recompiled body as-is | Hand-written native replacements for hot or hardware-bound functions | `hle/` |
| **IOP modules** | Models the IOP and its SDK calls | Native services for the title's custom `.IRX` modules | `iop/` |
| **SDK calls** | Generic SDK semantics | Title-specific bindings via `hle/sdk_bindings.tsv` | `hle/` |
| **Audio / FMV / streaming** | SPU2 and IPU models | Native mixing, decode and stream feeding | `hle/audio`, `hle/fmv`, `hle/stream` |

`docs/design/HLE_COVERAGE.md` tracks, per subsystem, what currently runs native and what is
still on a hardware model. Expect that document to start out almost entirely "fallback" for a
newly added title; closing those rows is the port.

### 3. `build.sh` / `build.ps1` — the one command, and `dist/`

| Platform | Command | Result |
|---|---|---|
| macOS | `./build.sh --disc "/path/to/the title.iso"` | `build-macos-dist/app/host/nativeee` + the tests + `dist/macos/` |
| Linux | `./build.sh --disc "/path/to/the title.iso"` | `build-linux-dist/app/host/nativeee` (the `linux-dist` preset: static SDL2 + FFmpeg, nothing to install at run time) + the tests + `dist/linux-<arch>/` + `dist/nativeee-linux-<arch>.tar.gz` |
| Windows | `.\build.ps1 -Disc "X:\path\the title.iso"` | `build-windows-release\app\host\nativeee.exe` + the tests + `dist\windows-x86_64\` + `dist\nativeee-windows-x86_64.zip` |

Both scripts configure the host's distribution preset, build it, run the test suite (the `Root*` /
`Og*` groups excluded) and stage the self-contained artifact folder described under *Users*: the
binary, `title/`, the user half of this README, `LICENSE`, the generated `THIRD_PARTY_NOTICES.md`
and `licenses/`, `BUILD-ID`. `--elf SLUS_123.45 [--irx STREAM.IRX]` (`-Elf` / `-Irx`) take loose
files; the environment variables above work too. The dist artifact *is* the title binary, so both
scripts refuse to stage one without an input (`--no-dist` builds the engine and tests only).
Other options: `--no-tests`, `--dist-only` (`-SkipBuild -Dist`), `--preset NAME`, `--jobs N`.

Before the script finishes, `tools/ci/asset_scan.py` runs over the staged folder (file names,
format magic, byte windows of the title's ELF inside every file): the folder carries the
recompiled program and the engine, never game data. The same scan in `--tree .` mode is the first
step of CI and must print `CLEAN` for the source tree. On macOS the distribution preset first
builds a static FFmpeg (`tools/ci/ffmpeg_static.sh`, minutes); `--preset macos-release` skips that
but its Homebrew FFmpeg is GPL-configured and the dist stage refuses to ship it, by design.

The acceptance shape for any change is: Release build with 0 FAILED / 0 `warning:` /
0 `ld: warning`, the component suite green, and — for anything touching the engine — the
deterministic-profile lanes identical to the previous state (*Harness and tests*).

### Known issues

The open items of the integration board (`docs/phases/INTEGRATION_STATUS.md`, last entries;
`docs/design/M_CUTOVER.md`; `docs/design/PLATFORMS.md`), as of 2026-10-14:

- **Frame skip at level entry.** The tick that cuts from the loading screen into the level runs
  long (29–37 ms on the current binary against the 16.7 ms budget) and a frame is dropped there;
  the candidate fix on the graphics branch (staggered mesh workers, wall-aware headroom) brought it
  to 22–24 ms, still over budget on the integrator's runs, and was sent back
  (INTEGRATION_STATUS 2026-10-14, "BUG A"; the judge script lives on that branch).
- **Mecha-Bandicoot arena flicker.** Grey presents during the boss fight (the play-mode frame skip
  erasing the dropped frame's boundary slice, which holds the next image's clear and world
  buckets). The fix exists on a branch; its judge fixture was re-recorded after the judge was
  frozen, so under the integration contract it is not on `main` yet (INTEGRATION_STATUS
  2026-10-14, "BUG B").
- **A3 parked — rendering above 60 Hz.** `video.render_rate=display` (re-rendering the tick's frame
  at the interpolated camera for high-refresh displays) is implemented as a mechanism but its
  pixel tier is open (`RootHighFrameRate` is red: the camera side channel at replay), so the
  Display-page row is hidden and the default stays 60 (INTEGRATION_STATUS 2026-10-13 wave 3;
  M_CUTOVER "A3 scaffold (pixels OPEN)"). Game speed is a fixed 60 Hz either way.
- **Windows build unverified.** `build.ps1` and the `windows-x64` CI lane have never run on a
  Windows machine. What is verified from macOS by cross-compiling against the real MSVC CRT and
  Windows SDK (`tools/build/win_cross_smoke.py`, ctest `PlatformWindowsCrossSmoke`): the host shim,
  every `engine/core` TU, the Windows host arm, the recompiled shards and the common TU compile to
  x86-64 COFF; the Windows arm links into a PE32+ with every import resolved. What a Windows
  developer hits first: the GL backend has no Windows GL loader (`PS2R_GL_HEADER` is undefined on
  `_WIN32`; every GL 4.1 entry point needs `wglGetProcAddress`), and six TUs still include POSIX
  headers (`app/host/{main,savestate_slots,first_run,launcher_settings,clock_census}.cpp`,
  `tools/vu1_replay/vu1_replay.cpp`) — `docs/design/PLATFORMS.md` §4 lists each with its fix.
- **Pre-existing red pins on the frozen play binary** (INTEGRATION_STATUS 2026-10-13 23:04):
  the shadow streak at level entry (`RootShadowLevelEntry`, 204–211 px), the title-logo sprite
  count (`RootSpriteTitleLogo`), `RootTitleScreenEffects`, the screen-filter level
  (`RootScreenFilterLevel` 58.77 vs 59.1), `RootAttractCutsceneFrames`, and long ticks after the
  intro movies (`RootNoLongTick` S3/S5/S8/S9: the post-FMV texture conversion tick at 14–15 ms,
  the first tick after the level cut at 16.7–16.9 ms). The game plays through them.
- **Not yet native.** Memory peaks at about 523 MB (board item 3b, L6/L8/L9); the distortion and
  cloth effect routes (2d) and the "1c residue" of the checkpoint certificate are carried open;
  `docs/design/HLE_COVERAGE.md` lists what still runs on a hardware model rather than a native
  service.
- **CI artifact and identity lanes need a title input.** The shipped binary is the recompiled
  title, so the `Stage dist/`, `linux-dist`, identity and release lanes only run where the runner
  supplies one (repository variable `NATIVEEE_TITLE_LANES`, see the `ci.yml` header); on the
  public repository they are skipped and CI is the asset scan, the five build lanes and the
  component suite.
- **The fresh-machine drill** ("zip + disc image → the beach") has been measured on a fresh home
  directory of the dev host (`docs/design/FIRST_RUN_DRILL.md`), not yet on a separate machine.

### Presets (CMakePresets.json)

| Preset | Dir | What |
|---|---|---|
| `macos-dev` | `build/` | Ninja, Debug, diagnostics off — the construction configuration |
| `macos-diag` | `build-diag/` | Debug with the `core/diag` mechanism compiled in (censuses, probes, recorders) |
| `macos-release` | `build-release/` | Release — the acceptance build |
| `macos-dist` | `build-macos-dist/` | Release, SDL2 and FFmpeg built from source and linked statically — the shipped macOS artifact (`./build.sh`) |
| `windows-dev` / `windows-release` | `build-windows[-release]/` | clang-cl + Ninja + vcpkg; the same shape on Windows |
| `windows-msvc-dev` | `build-windows-msvc/` | cl.exe compile check (no `-fwrapv` equivalent; never a release) |
| `linux-release` / `linux-dev` | `build-linux-release` / `build-linux-dev/` | Ninja, the environment's CC/CXX (GCC 12+ or Clang 15+); Mesa GL, SDL2 found or `-DPS2R_FETCH_SDL2=ON` |
| `linux-dist` | `build-linux-dist/` | Release, static SDL2 + FFmpeg + libstdc++ — the shipped Linux artifact (`./build.sh`) |
| `linux-release-gcc` / `-clang`, `linux-dev-gcc` / `-clang` | `build-linux-*-gcc` / `-clang/` | the same with the compiler pinned (`tools/ci/linux_build.sh` + `tools/ci/linux.Dockerfile` run them under Xvfb) |

### Tree

```
engine/     title-neutral PS2 semantics + host services (core, ee, iop, dmac, gif, vif, sif, vu, gs,
            cdvd, ipu, pad, mc, sound, render, platform, tools) -- zero game tokens (lint)
games/<title>/   everything title-bound: hle/ (the native replacements), recomp/
            (the recompiled EE code's manifests + inputs; the shards themselves are GENERATED into the
            build dir from your disc, never committed), vu1_library, renderer_profile, iop/stream_irx,
            savestate, symbols, verify (the hash-lane baselines)
app/        composition roots: host/ (the only place a title is linked), gui/
tests/      ctest suites, lint/, oracle/ (differential tests against the frozen original, PS2R_OLD_ROOT)
tools/      the recompilers (recompiler_v2, iop_recompiler), disc_extract, regen_from_frozen.sh, replay tools, ci/
verify/     the deterministic harness: run_leg.py, diff_legs.py, battery.py, bench/
docs/       design, phase records, bug knowledge base; the construction history is docs/CONSTRUCTION_README.md
```

Engine code never names the game (`LintEnginePurity`); one class per `.h`/`.cpp` pair
(`LintOneClass`); no mutable globals in headers; size budgets (`LintSizeBudget`). The
host-OS / compiler shims live in ONE header, `engine/platform/host/platform.h`
(Windows arm `host_windows.cpp`, POSIX arm `host_posix.cpp`); nothing outside
`engine/platform/` carries an `#if` on the host. The `ps2r_` / `PS2R_` prefix is the code's
namespace and stays.

### Harness and tests

The deterministic profile makes a run reproducible: `verify/run_leg.py` runs the binary
from a cold boot or a save-state anchor for N frames and writes a *leg*
(`frames.fhash`, `audio.ahash`, `mem.mhash`, `run.meta`); `verify/diff_legs.py` compares
two legs lane by lane and reports the first divergent key; `verify/battery.py` runs the
13-leg battery. The hash-lane baselines live under `games/<title>/verify/baseline/` (the RAM
probes they were taken against are game-derived and not in the tree).

```sh
python3 verify/run_leg.py --binary build-release/app/host/nativeee --tree new \
    --anchor coldboot --frames 1300 --profile deterministic --out /tmp/legs/a
python3 verify/diff_legs.py /tmp/legs/a /tmp/legs/b --lanes frames,audio
python3 verify/bench/run_bench.py --binary build-release/app/host/nativeee --legs slowspot_f37092_s1789428268 --out /tmp/bench
```

Play sessions (`--harness-window`, `--record-input`, F5/F8 save states) and how to turn a
session into a bug report: `docs/design/PLAY_SESSIONS.md`. The test groups: lints
(`Lint*`), per-subsystem component suites (`Core*`, `Gs*`, `Vu*`, `Vif*`, `Dmac*`, ...),
the generated-code pins (`Recomp*`), the host shim contract (`PlatformHostShims`), the
oracle suites against the frozen original (`Og*`; skip when `PS2R_OLD_ROOT` is unset), and the game
process pins (`Root*`; need the game files and run one game process at a time).

### Continuous integration

`.github/workflows/ci.yml`: the `guard` job runs `tools/ci/asset_scan.py --tree .` and
`tools/ci/asset_guard.py` first (a disc image, an extracted game file or any game-derived byte in
the checkout fails the workflow before anything builds), then five Release targets (macOS arm64
and x86-64, Windows x64, Linux x86-64 and aarch64) configure, build and run the component suite
(`ctest -E '^Root'`). Where the runner supplies a title input (`NATIVEEE_TITLE_LANES`, see the
`ci.yml` header), the lanes also stage `dist/` and check that every target's deterministic
math-oracle digests are identical to the macOS arm64 lane (`verify/component_identity.py`), and
`release.yml` attaches one zip per target plus `SHA256SUMS` to a GitHub release on a `v*` tag — see
*Known issues*. Validate workflow edits locally with
`actionlint .github/workflows/*.yml` (`docs/design/PLATFORMS.md` §6). The Windows-only compile
errors a macOS host *can* catch are caught by
`PS2R_XWIN_ROOT=<xwin splat> ctest --preset macos-release -R PlatformWindowsCrossSmoke`
(the splat: `cargo install xwin; xwin --accept-license splat --output <dir>`; Homebrew LLVM
for a clang with the Windows target).

### Where to read next

- `docs/design/ARCHITECTURE.md` — the target design; `docs/design/HLE_COVERAGE.md` — what has
  been replaced natively and what still runs on a hardware model.
- `docs/phases/INTEGRATION_STATUS.md` — the integration board: the frozen all-on binary, its
  exit-census expectations and the open items; `docs/design/M_CUTOVER.md` — the per-toggle record.
- `docs/design/PLATFORMS.md` — the platform matrix, the shim header, the per-TU compile-time table.
- `docs/CONSTRUCTION_README.md` — the construction history (phases 0–9, audits, file maps).
- `docs/bugs/` — the bug knowledge base.
