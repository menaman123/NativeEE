# tests/gs/invariants

The A3 §9 invariant checklist (INVESTIGATION §12 map) is registered as 38 NAMED ctests
`Gs_InvNN_*` so that `ctest -N | grep -c Gs_Inv` == 38 (PHASE6_PLAN §6 G11). 35 of them are
real tests living beside the module they pin (`tests/gs/{state,xfer,vram,targets,texsrc,
classrdr,ir}`, `tests/render`). The three that Phase 6 does NOT own are registered from this
directory as honest SKIP stubs:

| ctest | invariant | owner | why a stub |
|---|---|---|---|
| `Gs_Inv25_InterpRingRotateBeforeFire` | 25 — interp tick ring rotates before it fires | `engine/gs/hle/interp/frame_interpolator` | the frame interpolator is not in the 6A–6F stage table (PHASE6_PLAN §2 has no interp row); `video.interp` is OFF in the deterministic profile and in every corpus baseline (P6-INTERP-DEFERRED, integration / Phase 8) |
| `Gs_Inv26_InterpIdentityStampedOnly` | 26 — interp matches identity-stamped draws only | `frame_interpolator` | same |
| `Gs_Inv30_OwnedByPhase5` | 30 — GIF PATH priority P1 > P2 > P3 | `engine/gif` (Phase 5) | the GIF arbiter is Phase 5's; Phase 6 consumes the register stream after arbitration |

Invariant 38 has two halves: the roundSprite normalisation half is pinned by
`Gs_Inv38_RoundSpriteNormalise` (`tests/gs/state/test_gs_state.cpp` `Inv38`, a real test on
`VertexKick`); the XGKick-deferral half is Phase 5's (`engine/vu`) and the test prints that note.

## The SKIP convention

`skip_stub.sh <ctest name> <owner> <reason...>` prints one line
`SKIP <name>: owned by <owner> -- <reason>` and exits **77**; each stub is registered with
`SKIP_RETURN_CODE 77`, so ctest reports it as `***Not Run` / "Skipped", never as Passed. A stub
is therefore visible in every summary line as a skip, counts in `ctest -N` (the G11 column is
complete) and can never be mistaken for a proof. When the owning module lands, the stub row in
`tests/gs/CMakeLists.txt` is REPLACED by the real test under the same ctest name (grep the name).

## Does not belong here

Real invariant tests (they live beside their module), corpus legs (`tests/gs/replay`), lints.
