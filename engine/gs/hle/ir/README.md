# `engine/gs/hle/ir/`

**Phase:** 5 (RegisterOpStream), 6  
**Purpose:** DrawIR record defs (ARCH §4.6) + G-C1 verifier + direct ops + gsdl format + the Phase-5 register-op stream sink. Library `ps2r_gs_ir` (links `ps2r_gs_format` and `ps2r_core`); no GL, no SDL, no game constants.

## Contents

- `gsdl_format.h/.cpp` — `GsdlFormat`: the `.gsdl` recording format — constants (`kMagic`, `kVersion`, `Kind`, `kDrawRecSizeV1`), the packed on-disk structs `GsdlFileHeader` (24 B), `GsdlRecordHeader` (8 B), `GsdlNameRec` (8 B), `DrawRec` (220 B; v1 = first 188 B) with `static_assert`s on every field offset, `ValidateHeader` / `DrawRecSize`, and the hash primitives whose values are part of the format (`Fnv1a`, `Fnv1aFast`, `Fnv1aSampled`, `VertexHash`, `HandlerId`, `Bbox`).
- `vertex_rec.h` — `VertexRec` (36 B): the post-kick vertex, identical in flight and on disk.
- `job_rec.h` — `JobRec` (52 B) + `JobKind` (the pinned non-draw job codes and their argument meanings).
- `tex_read_view.h` — `TexReadView`: the TEX0 / TEX1 / CLAMP / TEXA / MIPTBP register set a draw was kicked with.
- `draw_ir.h/.cpp` — `DrawIr`: THE draw record every consumer sees (identity, PRIM, `TexReadView`, raster registers incl. DTHE/DIMX, context + XYOFFSET, vertices, bbox, recorder snapshot hashes); `FromDrawRec` / `ToDrawRec` field for field.
- `direct_ops.h/.cpp` — `DirectOps`: the register-op form of one GIF packet (`Op`, `kModeReglist` / `kOpFlush` / `kOpSrcId` / `kOpSrcName`, `PacketToOps`).
- `counting_draw_ir_sink.h/.cpp` — `CountingDrawIrSink : DrawIrSink` (integration item 6.4): counts draws / jobs / frames, chains an FNV-1a 64 over the `DrawRec` bytes (`DrawHash`, `run.meta drawir=` + the leg's `drawir.ohash`) and over the `JobRec` bytes (`JobHash`), and forwards to an optional next sink (an owned draw is hashed then forwarded by move) — the stand-in consumer until the render queue lands and the decorator shape of a recorder tap.
- `drawlist_ring.h/.cpp` — `DrawlistRing : DrawIrSink` (ALL-ON, NO EXCEPTIONS crash path): a decorator that keeps the last 200 frames of the DrawIR stream as compact per-draw records + per-frame summaries in a FIXED-SIZE ring (allocated once; BUG-083's lesson) and forwards to the next sink; `Dump()` is the triage bundle's `drawlist_last200.txt`, `Last()` the faulting frame's last draw. Compiled into every build (the `.gsdl` recorder in core/diag stays diagnostics-only).
- `draw_ir_lift.h/.cpp` — `DrawIrLift`: the draw-level lift of one GIF packet (`Item` / `Vert` / `Stream`, `PacketToDrawIR`) with the attribute-resolution exactness rules.
- `draw_ir_verifier.h/.cpp` — `DrawIrVerifier`: invariant 27 — both forms canonicalised to GS-observable events (`RunOps`, `RunIR`, `VerifyKickEquivalence`).
- `register_op_stream.h/.cpp` — `RegisterOpStream : GsSink` (moved here from `engine/gif/`, Phase-5 checklist (h) move 1): every parser call becomes one 24-byte `RegisterOp` with a running FNV-1a 64 — the GS-side observable of a GIF stream (`run.meta gsops_hash`, `gsops.ohash`). `GsSink` itself stays in `engine/gif/gs_sink.h`. Retention is a constructor choice: `RegisterOpStream(/*retain_records=*/false)` folds the same hash and counts the ops but stores none (the leg's recorder — a retaining one grew 24 bytes per sink call for the whole leg, BUG-083); replay/test consumers that read `Ops()` keep the retaining default.
- `draw_ir_sink.h` — `DrawIrSink`: the consumer boundary (`OnDraw`, `OnJob`, `OnFrame`) GsState, the recorder and the replay driver share; `OnDrawOwned(DrawIr&&)` (default `OnDraw(draw)`) is how the state machine hands its flushed draws, so a sink that keeps the draw (the cross-thread queue) moves the vertex vector instead of copying it (track 1d).
- `CMakeLists.txt` — the `ps2r_gs_ir` static library (links `ps2r_gs_format`, `ps2r_core`).

## Belongs here

- DrawIR, DrawIrVerifier, DirectOps, gsdl_format.h, VertexRec, RenderBatch, RegisterOpStream

## Does not belong here

- Readers of recordings (`tests/lib/gsdl_reader`), tools (`tools/drawlist_tool`, `tools/gs_replay`), any host graphics type.

## Transplanted from (OLD, read-only)

- `hle/gs_draw_ir.h, hle/gs_direct_ops.h, hle/drawlist_record.h, graphics/gpu_vertex.h, render.h IR types`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- The `.gsdl` byte layout is pinned by the recorded corpus: new fields append at the tail under a new version, never in the middle.
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
