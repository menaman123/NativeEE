# P4FIX-F1 — `ps2state_slot0_f57292` / `ps2state_slot0_f59991`: `[IND-JUMP-BAD] @PC=0x1f4940` → `[FATAL-NOJUMP] pc=0`

Status: OPEN (Phase-3 work items W1/W3 done; W2 verdict recorded below -- the census itself is a Phase-4 leg; closure is Phase 4/5).
Source: RECOMP_P4_FIX §3, CONSTRUCTION_PLAN §4.3, PHASE3_PLAN §3.5.

## Symptom (OLD `build-v2/host_app/host_app`)
Deterministic on both anchors: FUN_001f4730's callback slot reads 0 → `recomp_fcf_miss` → fatal. The legacy
binary runs both legs, so the family is "v2 loses a race" (timing / OS-layer ordering), not an emission defect.

## Phase-3 findings (stage 3F, 2026-09-27; NEW `nativeee`, headless deterministic profile)

| Item | Result |
|---|---|
| W1 repro leg — run to park (3F′) | With the relaunch wired (`ThreadRelauncher`): `--load-state ps2state_slot0_f57292.ps2st --frames 0` → `relaunch=2 syscalls_first=GetThreadId(47) sdk_rows_hit_list=sceSifSetRpcQueue@0x2d2be8:1 park=sif_rpc_absent park_pc=0x1e7fa4 park_tid=2 unhandled_mmio=0 finalized=1`, run.meta (minus wall_ms/build_id) md5 `6c6aa1c9763ee599de4fd385dc7be7aa` ×3; `ps2state_slot0_f59991.ps2st` → same park keys, md5 `66884db1ceb2e177c83824b56f4059eb` ×3. Neither leg reaches FUN_001f4730 under NEW (the IOP-dependent SIF row parks first, by design). |
| W1 repro leg — restore soundness | `--load-state ps2state_slot0_f57292.ps2st`: `restore_mhash=0xfc8deca68213dc9c restore_mhash_ok=1 restore_thrd_records=2 finalized=1`; `ps2state_slot0_f59991.ps2st`: `restore_mhash=0x9d674482db08223c restore_mhash_ok=1 restore_thrd_records=2 finalized=1`. Both equal `verify/state_hash.py` (the 3E table). The state FILES are sound; both parse with 2 THRD records (`ctx_size == sizeof(CpuContext) == 1600`). **The run-to-park half of W1 is not yet possible:** the root does not relaunch the restored guest threads (`relaunch=deferred`, see PHASE3_IMPL §3F). |
| W2 slot census | **Verdict (3F′): the census as specified has no static target, and no NEW leg reaches the frame before the SIF park.** The OLD crash line reads `[IND-JUMP-BAD] @PC=0x1f4940 r2 target=0x0 src=0x1c *src=0x0`: the site is `jalr *(r3+0x1c)` with `r3 ← EE[sp+0x10]` (FUN_001f4730's own frame, `recompiled_shard_016.cpp` Label 0x1f4934), so `src=0x1c` means the OBJECT POINTER on the caller's stack is NULL -- the "slot" is not an entry of a static callback table that a RAM-range FNV could census in the state file. The value only exists once a relaunched thread reaches FUN_001f4730. Under NEW, both anchors relaunch (2 threads each) and tid 2 parks at `sceSifSetRpcQueue@0x2d2be8` (caller `0x1e7fa4`) after one syscall, 3/3 reps identical (§3F′ table) -- before FUN_001f4730. So "0 in the file vs zeroed after restore" cannot be adjudicated by a static census; it is a Phase-4 leg: the first leg that passes the SIF park records `p4fix_obj=` (EE[sp+0x10] at FUN_001f4730 entry, tid) on NEW and on OLD `--tree v2` (bounded 60 frames). Nothing in the Phase-3 kernel/relaunch logic is implicated by the evidence in hand (RelaunchOrder green; restore byte-exact). |
| W3 invariant test | `RelaunchOrder` (tests/ee, landed 3C1) green in the 3F ctest run: after `EndRelaunch` the first runner is the thbase winner and no `EeEventQueue` entry is drained on a non-holder thread. |

No pins, no hand bodies (PHASE3_PLAN rule-6 exception honoured).
