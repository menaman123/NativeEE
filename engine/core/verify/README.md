# `engine/core/verify/`

**Phase:** 1  
**Purpose:** In-binary harness emitters (EQUIVALENCE §2): frames.fhash / audio.ahash / mem.mhash / run.meta. Armed by Config.harness (CLI), never env.

## Belongs here

- FrameHasher, AudioHasher, MemoryCheckpointer, RunMeta, VerifyHost

## Does not belong here

- the runner/differ (verify/ at repo root), any game token

## Transplanted from (OLD, read-only)

- `host_app/verify/{frame_hasher,audio_hasher,memory_checkpointer,run_meta,verify_env,verify_present,verify_boundary}.{h,cpp}`

## Flags consumed (FLAG_LEDGER outcomes applied at transplant; Phase 1B)

| Flag | Outcome | Where |
|---|---|---|
| `PS2_VERIFY_OUT`, `PS2_VERIFY_HEADLESS`, `PS2_VERIFY_RECORD_INPUT`, `PS2_VERIFY_STATE_EVERY`, `PS2_VERIFY_TREE`, `PS2_VERIFY_PROFILE` | SETTING `harness.*` — CLI-only (`--harness-out --headless --frames --load-state --inputs --state-every --profile`); refused from ini/`--set`/`PS2R_SET`; `tree=new` is a constant | `VerifyHost::Arm`, `RunMeta` |
| `PS2_GS_SCALE` (the frame hasher's scale read) | SETTING `video.scale` passed as the `OnPresent(scale)` argument | `VerifyHost::OnPresent`, `FrameHasher` |
| `CollectEnv()` over `environ` (run.meta `env_ps2`/`env_hash`) | replaced by `Config::Fingerprint()` / `FingerprintHash()` (no environment read) | `RunMeta` |
| `--bench-frames` | renamed `--frames` → `harness.frames`; `FrameCap` compiled in all builds | `FrameCap` |

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
