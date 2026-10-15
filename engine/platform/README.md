# `engine/platform/`

**Phase:** 1 (macos), 6 (sdl), 9.4 (others)  
**Purpose:** ONE Platform interface (ARCH §16); no #ifdef outside this tree. One folder per target, each ONLY with a feasibility note + CI-buildable stub. `host/platform.h` is the ONE header the application and the tests include (the `host::` free functions + `PS2R_GL_HEADER`); `CMakeLists.txt` and `host/platform.cpp` are the two places that pick the target by OS.

## Belongs here

- platform.h, host/ (the ONE host header), macos/, linux/, posix/ (fibers + crash handler shared by both), sdl/ (shared implementation composed by targets), _template/; later windows/ psvita/ wii/ gamecube/

## Does not belong here

- aspirational empty target folders (rule: note + stub or nothing)

## Transplanted from (OLD, read-only)

- `thread_qos.h, platform/{crash_handler.h,backtrace_compat.h,proc_stats.h,win_compat.h}, platform/macos/crash_handler_macos.cpp, main.cpp SDL setup, input/*, sound/mixer.cpp SDL half`

## Flags consumed (FLAG_LEDGER outcomes applied at transplant; Phase 1B)

| Flag | Outcome | Where |
|---|---|---|
| `PS2_NO_QOS` | NORM — every engine thread is created with the USER_INTERACTIVE QoS pin; no opt-out | `PlatformMacos::thread_create`, `PinCurrentThreadInteractive` |
| crash-handler env toggles (none survive) | the handler is always installed by the composition root; SIGSEGV/BUS/ILL/FPE/ABRT → backtrace + `RingLog::DumpAllToPath(DUMP_CRASH)` + default re-raise | `CrashHandler::Install` |

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
