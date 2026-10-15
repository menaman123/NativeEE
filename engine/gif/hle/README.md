# `engine/gif/hle/`

**Phase:** 5 (reserved) — populated at integration (INTEGRATION_PLAN item 6.4).  
**Purpose:** the `GsSink` implementations the composition root hands the GIF parser.

## Contents

- `gs_state_sink.h/.cpp` — `GsStateSink : GsSink` over `ps2r::gs::GsState`: `BeginTag` → `GifTagRead()` (Q = 1.0; PRE → PRIM is the parser's own `WriteRegister`), `WriteRegister` → `GsState::WriteRegister`, the PACKED RGBAQ / ST / UV / FOG items re-packed into their PACKED layout for `WritePacked`, `KickVertex` → `GsState::KickVertex`, one IMAGE quadword → `WriteImage(16 bytes)`, `EndOfPacket` → `KickBoundary()` (the one GifEop flush), `FinishPath3` → a StateChange flush of a non-empty draw buffer (OLD `GIF::FinishDMA`). Counters (`Tags / RegisterWrites / PackedWrites / Kicks / ImageQwords / Eops / Path3Finishes`) feed `run.meta gs_sink=`; the same by the path of the tag in progress (`TagsOn / RegisterWritesOn / KicksOn / ImageQwordsOn`) feed `run.meta gs_sink_by_path=` — PATH1 = the VU1 programs' kicks (lowered or parsed), PATH2 = the title's VIF1 DIRECT packets (state, model draws, the texture uploads' IMAGE quadwords), PATH3 = GIF DMA. The identity stamp (invariant 28) is carried across the arbiter queue: `StageKickIdentity` (the HLE runtime's observer, before a kick's submit), `OnArbiterSubmit` (one FIFO slot per accepted PATH1 packet: staged or raw), `OnArbiterDeliver` (the drain observer: pop → `GsState::SetIdentity(handler, image, pc, ord, frame_inst)` / `ClearIdentity`; PATH2/PATH3 clear), `SetFrameInstance` (the root's boundary count); counters `IdentityStamped / IdentityRaw / IdentityUnderflows` feed `run.meta drawir=identity_*`. Attribution rows (track 1c-2): `ClassifyPath2` names a PATH2 packet's class from its first GIFtag at the submit observer (image / texregs = A+D led by TEXFLUSH, the title's WriteTextureRegisters / transfer_head = A+D led by BITBLTBUF..TRXDIR / state = any other A+D / geometry = a vertex register or PRE / reglist / other) → `Path2Packets / Path2Bytes` → `run.meta gif_path2_by_class=`; `SetBucketHint(bucket)` (the host's walk tracker: the render bucket whose `first` tag the VIF1 walk passed last, -1 = none) attributes PATH1 tags / kicks to the title's 28 render buckets → `Path1TagsInBucket / Path1KicksInBucket` → `run.meta gs_sink_path1_by_bucket=exact=…` (exact only under the inline VIF1 feed: the MTVU worker runs the programs after the walk).
- `gs_sink_fanout.h/.cpp` — `GsSinkFanout : GsSink` forwarding every call in order to N member sinks (null members skipped, `Add` for late members). The root's parser sink is `{GsStateSink, RegisterOpStream}` so the `gsops.ohash` recorder stays attached beside the façade.
- Library `ps2r_gif_hle` (links `ps2r_gif`, `ps2r_gs_state`) — kept out of `ps2r_gif` so the GIF proper never depends on the GS state machine. Tests: `tests/gif/test_gs_state_sink.cpp`.

## Belongs here

- GsSink adapters (the GS façade, fan-out, future recording decorators of the GIF stream)

## Does not belong here

- the parser (`engine/gif`), the GS state machine (`engine/gs/state`), the DrawIr consumers (`engine/gs/hle`)

## Transplanted from (OLD, read-only)

- `host_app/gif.cpp:4374-4425` (`FinishDMA`: the draw-buffer flush when no IMAGE transfer is mid-way)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4; `docs/phases/INTEGRATION_PLAN.md` item 6.4.
