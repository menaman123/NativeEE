# `engine/ee/hle/`

**Phase:** 3  
**Purpose:** Kernel/OS surface: syscalls, threads/semaphores (thbase 1.02), boot environment writer, libc SdkWrap rows, SdkBindingManifest + the ONE installer, libkernl/libscf/libdev bindings, dyn-decode fallback.

## Belongs here

- SyscallTable, DynInterp, RpcCallbackQueue, ThreadManager, Semaphores, BootEnv, Libc, SdkBinding(+Manifest), kernl_*, libscf/libdev bindings

## Does not belong here

- address-keyed hooks (only signature-bound SdkBinding rows), argv string / args_base (TitleProfile.boot)

## Transplanted from (OLD, read-only)

- `syscalls.{h,cpp}, ps2_syscall_wrappers.{h,cpp}, ps2_threading.{h,cpp}, ps2_argv.h, libc_hle.{h,cpp}, sce_hle/{sce_hle.h,sce_hle_registry.cpp,libkernl.cpp,libscf.cpp,libdev.cpp}`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
