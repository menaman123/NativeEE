# `engine/platform/host/`

**Phase:** Charter 2 Track B (B1)  
**Purpose:** the ONE host-platform header, `platform.h` (ARCH §16: no `#ifdef` on the OS outside `engine/platform/`). Free functions in `ps2r::platform::host` that the application and the tests call instead of a target class; `platform.cpp` forwards them to the target folder this build is for (`macos/` or `linux/`) and is the only TU that picks a target by OS. Also the GL header spelling: `#include PS2R_GL_HEADER`.

## Belongs here

- `platform.h`: `RegisterSelf`, `Instance`, `Name`, `PinCurrentThreadInteractive`, `PhysFootprintBytes`, `ExecutablePath`, `NowNs`, `WriteBacktrace`; `PS2R_GL_HEADER`, `PS2R_HOST_MACOS` / `PS2R_HOST_LINUX`
- `platform.cpp`: the forwarders (`using Target = PlatformMacos | PlatformLinux`)

## Does not belong here

- a target's implementation (that is `macos/`, `linux/`, shared POSIX pieces in `posix/`)
- SDL (`sdl/`), windows/vita/wii targets (their own folders, ARCH §16)

## The equivalence table (what each macOS call became)

| macOS | `host::` | Linux |
|---|---|---|
| `pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE)` + `pthread_setname_np(name)` | `PinCurrentThreadInteractive(name)` | `pthread_setname_np(pthread_self(), name)` (15 chars); no QoS class exists under CFS -- the intent (every emulation thread in main's class) already holds |
| `task_info(TASK_VM_INFO).phys_footprint` | `PhysFootprintBytes()` | `/proc/self/statm` resident pages × page size |
| `_NSGetExecutablePath` + `canonical` | `ExecutablePath(err)` | `readlink(/proc/self/exe)` |
| `std::chrono::steady_clock` | `NowNs()` | the same |
| `<execinfo.h>` backtrace | `WriteBacktrace(fd, skip)` | the same (glibc) |
| `<OpenGL/gl3.h>` (+ `GL_SILENCE_DEPRECATION`) | `#include PS2R_GL_HEADER` | `<GL/glcorearb.h>` + `GL_GLEXT_PROTOTYPES`; Mesa/libglvnd export the core entry points, no loader library |
| `ucontext` fibers (`FiberMacos`) | `Platform::fiber_*` | the same TU, `posix/fiber_posix.cpp` (`MAP_ANONYMOUS`, clang-only pragma) |
| `posix_spawn[p]` + pipe + `waitpid` | `host::ProcessRun(image, argv, search_path, &output)` (the VU1 AOT builder's compiler runs; PATH search inside libc, no environment read by the engine) | the same; Win32 arm: `_spawnv[p](_P_WAIT)` with the output through a temp file |
| `dlopen(RTLD_NOW\|RTLD_LOCAL)` / `dlsym` / `dlclose` | `host::DynamicLibraryOpen / DynamicLibrarySymbol / DynamicLibraryClose / DynamicLibraryExtension` (the generated VU1 image libraries) | the same (`so`); Win32 arm: `LoadLibraryA` / `GetProcAddress` / `FreeLibrary` (`dll`) |

Nothing here feeds the game's time domain or arithmetic: the deterministic lanes are platform-neutral by construction; the per-OS behaviour is scheduling (QoS) and diagnostics.

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Transplanted from (OLD, read-only): `host_app/thread_qos.h`, `host_app/platform/{win_compat.h,backtrace_compat.h,proc_stats.h}` -- the list of what needed shimming; Windows shims (B2) join this header, not a second one.
