# BUG-059 — NEW-side: a `TRXDIR 0` partial flush sized its payload from the intermediate TRXDIR latch but copied from the in-flight upload buffer — heap over-read (fixed)

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-059 |
| **Title** | NEW-side: a `TRXDIR 0` partial flush sized its payload from the intermediate TRXDIR latch but copied from the in-flight upload buffer — heap over-read (fixed) |
| **User-visible symptom** | none in a release run (ASan-only: `READ of size 256 … 0 bytes after 32-byte region`) and a payload tail of garbage where OLD has zeros — a NEW-side defect introduced by review-6B item 1 and fixed in review-6B fix 2 |
| **Scene / savestate** | synthetic: restage TRXREG 64×64, intermediate TRXDIR 3 and TRXDIR 2, then TRXDIR 0 (the `Gs_Inv03_TrxdirLatch` sequence) |
| **Subsystem** | `gs-vram` (transfer engine) |
| **Root cause** | After review-6B item 1 let an unfinished upload survive TRXDIR 1/2/3 (as OLD does), the next `TRXDIR 0` partial flush sized its payload from `prev_flush_` (the geometry the intermediate TRXDIR latched) but copied it from `linear_` (allocated for the upload in flight) with an unclamped iterator range — a heap read past the buffer whenever the intermediate latch was wider. Fixed: `TransferEngine::SetTrxDir` builds `part(pw*ph*4, 0)` and `memcpy`s `min(part.size(), linear_.size())` = OLD `gs_state.cpp:4404-4406` (real bytes, then zeros; the attribution `prev_w × prev_h` was already OLD's own). |
| **PS2 rule violated** | none of the GS's — a host memory-safety defect in NEW; the GS rule reproduced is OLD's flush attribution (payload = latched `prev_w × prev_h`, zero-padded) |
| **Fix + hatch** | FIXED in Phase 6 (6B review fix 2) — `engine/gs/xfer/transfer_engine.cpp` `TransferEngine::SetTrxDir`; no hatch |
| **Regression test** | ctest `Gs_Inv03_TrxdirLatch` (`payload_bytes == 256`, 16-byte prefix == written pixels, bytes 16..255 zero); ASan-clean `test_transfer_engine all`; a negative-control ASan build against the pre-fix line reproduces the overflow |
| **Source record** | `docs/phases/PHASE6_IMPL.md` findings row `P6-PARTIAL-FLUSH-OOB` (`:100`), "Review 6B fix 2", item 7 L7 |
| **Triage tips** | Garbage in the tail of a partial host→local flush payload, or an ASan report in `SetTrxDir`, means the latch/buffer pairing regressed — compare `prev_flush_` dims against `linear_.size()`. |
