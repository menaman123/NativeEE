# `engine/platform/windows/`

**Phase:** 9.4 / Standing Charter 2 Track B2
**Purpose:** The Windows target: `PlatformWindows` fills the ONE Platform interface over the
host shim arm (`engine/platform/host/host_windows.cpp`); surface / input / audio come from
`platform/sdl` exactly as on macOS. Compiled on `_WIN32` only; the Windows CI job
(`.github/workflows/windows.yml`) is its compiler -- the dev host is macOS.

## Feasibility note (2026-10-12)

| Capability | Windows | Note |
|---|---|---|
| steady clock | QueryPerformanceCounter | `host::MonotonicNowNs` |
| threads with a stack size + name + interactive pin | `_beginthreadex` (reservation), `SetThreadDescription`, `THREAD_PRIORITY_ABOVE_NORMAL` | the 1 GiB EE stack is a reservation, committed lazily |
| coroutines (the EE thread model) | native fibers (`CreateFiberEx` / `SwitchToFiber`, float-switch flag) | the host thread converts itself on the first `fiber_current` |
| files | stdio + UCRT fds; positioned reads via `ReadFile` + `OVERLAPPED` | 64-bit seek/tell (`_fseeki64`) -- LLP64 `long` is 32-bit |
| crash handler | SEH `SetUnhandledExceptionFilter` + `CaptureStackBackTrace` + DbgHelp (`windows/crash_handler_windows.cpp`, behind `engine/platform/crash_handler.h`) | written (round 3); the triage bundle's writer is shared; first RUN on a Windows host pending |
| memory footprint | `GetProcessMemoryInfo` private usage | not `phys_footprint`: the peak_mb pins are platform-keyed |
| process relaunch (F8 restore) | `_spawnv` + exit | no `execv`; the relaunch gate must not pin the pid |
| GL | `opengl32.dll` + the GENERATED loader `windows/gl_loader.{h,cpp}` (TwinsanityEvolved's, verbatim: Khronos `<GL/glcorearb.h>` types + `ps2r_glwin_*` pointers behind the GL names, `ps2r::glwin::Load` filled by `host::GlLoadEntryPoints` once the context is current; `tools/build/gen_gl_loader.py` regenerates, ctest `PlatformGlLoaderCurrent`) | round 6: the round-3 `host/gl_windows.{h,cpp}` retired for the shape TwinsanityEvolved's Windows CI compiles and runs (Mesa llvmpipe) on `windows-2022`; `SDL_GL_DEPTH_SIZE 24` on Windows (the GDI Generic trap) from the same source |
| thread ranks | `ThreadRole` -> `THREAD_PRIORITY_TIME_CRITICAL` (Pacer) / `HIGHEST` (Game) / `ABOVE_NORMAL` (else) | round 6, TwinsanityEvolved's `PinCurrentThreadInteractive(role)`; `RaiseTimerResolution` + `SleepForNs`, `CpuDetect`, `FdSize` alongside ours |
| compiler | clang-cl (release), cl.exe (compile check) | the recompiled TUs need `-fwrapv`; cl has none |

## Belongs here

- PlatformWindows; later the Windows crash handler

## Does not belong here

- Win32 calls outside `host/host_windows.cpp` (this file names none)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4; `docs/design/PLATFORMS.md`.
