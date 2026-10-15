# `engine/platform/linux/`

**Phase:** Charter 2 Track B (B1)  
**Purpose:** the Linux target (glibc; x86-64 and aarch64): `PlatformLinux` fills the `Platform` struct -- steady clock, named pthreads, stdio files, page size, `posix/` ucontext fibers -- and the `/proc` footprint + executable path behind `engine/platform/host/platform.h`. Built by the `linux-*` presets (GCC and Clang, `-Werror`); verified by `tools/ci/linux.Dockerfile` (Ubuntu 24.04, Mesa llvmpipe under Xvfb for the GL tests).

## Belongs here

- `PlatformLinux`

## Does not belong here

- anything reached by the application directly (it goes through `host/platform.h`)
- a QoS emulation: CFS has no performance/efficiency-core class; `PinCurrentThreadInteractive` names the thread only

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Plan: `docs/design/ARCHITECTURE.md` §16; `CUTOVER.md` Standing Charter 2, Track B.
