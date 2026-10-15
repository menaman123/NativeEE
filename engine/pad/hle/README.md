# `engine/pad/hle/`

**Phase:** 4 (stage 4E: `PadProtocol`; 4E′: `LibpadBindings` (the 19 `libpad.a` rows), `PadmanModule` (the two padman servers), `PadStampDuty` (the slot-4 stamp duty over `InputLog` replay/record)). The pad section of the savestate codec is Phase 6; SDL device backends stay Phase 7.  
**Purpose:** PadProtocol (wire words, slot phase/seq stamps at the boundary) + libpad SdkBinding rows + the padman RPC face + the boundary stamp duty. The title's libpad statics and the readiness hook site are `TitleProfile::Pad` (games/); the hook itself is `games/<t>/hle/pad_readiness_hook`.

## Belongs here

- PadProtocol, LibpadBindings, PadmanModule, PadStampDuty

## Does not belong here

- probes (core/diag); title addresses (TitleProfile::Pad); the title hook that raises the readiness event (games/<t>/hle)

## Transplanted from (OLD, read-only)

- `pad_input.{h,cpp}` (protocol half; the press latch of `:226-262` not transplanted -- see PHASE4_IMPL §4E′), `sce_hle/libpad.cpp`, `sif_rpc_handlers.cpp:875-1015` (`HandlePadman`), `core/triage` `InputLog` for the script grammar

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
