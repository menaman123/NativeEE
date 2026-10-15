# `engine/platform/posix/`

**Phase:** 1 (as `macos/`), Charter 2 B1 (shared)  
**Purpose:** the pieces the macOS and Linux targets share byte-for-byte: `FiberPosix` (ucontext fibers with a guard page; the IOP kernel's coroutines) and `CrashHandler` (sigaction + execinfo backtrace + ring-log dump). Moved here from `macos/` unchanged except the file names, the class name and the clang-only deprecation pragma.

## Belongs here

- `FiberPosix`, `CrashHandler`

## Does not belong here

- a target's `Platform` instance (`macos/`, `linux/`); Win32 fibers / SEH (the `windows/` target)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Transplanted from (OLD, read-only): `host_app/platform/macos/{fiber_macos.cpp,crash_handler_macos.cpp}`, `host_app/platform/platform.h`.
