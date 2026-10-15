# BUG-060 — `ComputeMergeKeyAllPrim` seeds its FNV-1a with `1469598103934665603` — the 64-bit offset basis with its last decimal digit dropped (reproduced for key equality)

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-060 |
| **Title** | `ComputeMergeKeyAllPrim` seeds its FNV-1a with `1469598103934665603` — the 64-bit offset basis with its last decimal digit dropped (reproduced for key equality) |
| **User-visible symptom** | none — the key is compared, never recorded; kept bit-exact so the queue merge behaves identically on any future recording |
| **Scene / savestate** | n/a — pure function; two known vectors from OLD's function body |
| **Subsystem** | `gs-present` (draw-run merge key); `render-backend` (secondary) |
| **Root cause** | OLD `render.h:2338` `ComputeMergeKeyAllPrim` seeds FNV-1a with `1469598103934665603` (0x14650FB0739D0383) instead of the 64-bit offset basis `14695981039346656037` (0xCBF29CE484222325) — a dropped final digit. Found when the review-6B known-vector pin failed against a NEW key seeded with the true basis. NEW `DrawFlush::RunKey` uses `kMergeKeyBasis = 1469598103934665603` so the key VALUE equals OLD's. |
| **PS2 rule violated** | none (host-side hashing); the FNV-1a specification's 64-bit offset basis is `0xCBF29CE484222325` |
| **Fix + hatch** | Reproduced on purpose (`kMergeKeyBasis`); correcting the basis is harmless for equivalence (compared, not recorded) but is a deliberate change — record it when made; no hatch |
| **Regression test** | ctest `Gs_Inv32` (two known vectors over a minimal batch) |
| **Source record** | `docs/phases/PHASE6_IMPL.md` findings row `P6-MERGEKEY-BASIS` (`:99`), item 7 L7 |
| **Triage tips** | If a future recording/replay disagrees on merge decisions between trees, check the basis constant first — it is the one intentionally "wrong" number in the key path. |
