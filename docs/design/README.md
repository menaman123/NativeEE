# `docs/design/`

The plan and its target. Read in this order: `ARCHITECTURE.md` (target; §3 tree has a
2026-09-26 banner pointing at the drift table), `CONSTRUCTION_PLAN.md` (the executable
greenfield plan — phases, gates, flag counts, risk register, frozen-tool invocation),
`EQUIVALENCE_PLAN.md` (how identity is proven), `HOOK_RECLASSIFICATION.md`,
`TRIAGE_CAPTURE.md`, `REFERENCE_ARCHITECTURES.md`. `MIGRATION_PLAN.md` is superseded in
shape (banner) and retained for its per-file dispositions.

Belongs: design decisions and plans. Does not belong: per-phase execution records
(`docs/phases/`), bug entries (`docs/bugs/`), per-file maps (`docs/filemap/`).

`PLATFORMS.md` (2026-10-12, Standing Charter 2 Track B): the host matrix, the ONE shim header
`engine/platform/host/platform.h` and its two arms, the per-compiler flag mapping and the
measured per-TU compile-time table of the recompiled code, the ordered Windows/Linux gap list,
and the developer path (`build.sh` / `build.ps1`, the `dist/<platform>/` layout).
