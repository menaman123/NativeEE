# `engine/platform/macos/`

**Phase:** 1  
**Purpose:** macOS implementation: steady clock, threads + QoS, files, the mach footprint and dyld executable path (Phase 1, Charter 2 B1); fibers + crash handler live in `posix/`; SDL window/GL context via platform/sdl (Phase 6). Reached through `host/platform.h` only.

## Belongs here

- PlatformMacos

## Does not belong here

- —

## Transplanted from (OLD, read-only)

- `thread_qos.h, platform/macos/crash_handler_macos.cpp, platform/proc_stats.h`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
