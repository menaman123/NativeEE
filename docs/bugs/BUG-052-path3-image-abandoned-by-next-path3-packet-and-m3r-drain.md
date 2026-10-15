# BUG-052 — A PATH3 IMAGE left mid-transfer is abandoned by the next PATH3 packet; an M3R-held PATH3 packet is lost at the drain's `FinishPath3`

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-052 |
| **Title** | A PATH3 IMAGE left mid-transfer is abandoned by the next PATH3 packet; an M3R-held PATH3 packet is lost at the drain's `FinishPath3` |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit); a texture upload split across two GIF chains would arrive truncated |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `gif-vif-dmac` |
| **Root cause** | OLD `gif.cpp:647-658`: a PATH3 packet arriving with `in_transfer` set resets the transfer and clears the tail (`P5-GIF-P3-STALE-RESET`). Through DMA (`P5-5F-P3-STALE-RESET-VIA-DMA`) a PATH3 IMAGE left mid-transfer by one GIF chain survives the drain's `FinishPath3` but the next GIF chain's `ReceiveData(Path3)` abandons it — a PATH3 IMAGE can only be completed by PATH1/PATH2 continuation. With MODE.M3R set at drain time the PATH3 packet is held in the parser tail and then cleared by the drain's `FinishPath3` — the data is lost unless the mask lifts before the drain (`P5-5F-M3R-DRAIN-ABANDON`; still to be confirmed against OLD's drain cadence once `GifRegs` is routed — 5G did not revisit). |
| **PS2 rule violated** | GS/GIF: a GIFtag's NLOOP×NREGS transfer continues across DMA packets until complete (the GIF keeps the tag state per path); M3R/M3P masks only stall PATH3 — masked data waits in the FIFO, it is never discarded. |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field |
| **Regression test** | ctest `DmacGifPath3` (stale-reset via DMA); the 5A parser pins (part 2); the NEW-inline M3R observable pin |
| **Source record** | `docs/phases/PHASE5_IMPL.md` (j) row `P5-GIF-P3-STALE-RESET / P5-5F-P3-STALE-RESET-VIA-DMA / P5-5F-M3R-DRAIN-ABANDON`; 5A `:102`, 5F `:2020-2021` |
| **Triage tips** | A texture whose bottom rows are stale/black after a chain boundary, or after a `MSKPATH3` window — check the GIF path counters for an IMAGE abandoned with bytes remaining; confirm the M3R drain cadence against OLD before changing anything. |
