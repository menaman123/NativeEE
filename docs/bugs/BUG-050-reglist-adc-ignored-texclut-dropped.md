# BUG-050 — GIF REGLIST: XYZ2 kicks ignore the ADC bit (bit 47) and TEXCLUT (0x1C) writes are dropped

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-050 |
| **Title** | GIF REGLIST: XYZ2 kicks ignore the ADC bit (bit 47) and TEXCLUT (0x1C) writes are dropped |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit) on the corpus (every REGLIST ADC=1 vertex OLD drew is drawn identically by NEW) |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `gif-vif-dmac` (parser); `gs-classes` (secondary: vertex kick) |
| **Root cause** | OLD's REGLIST dispatcher forwards an XYZ2 word to the vertex kick regardless of bit 47 (ADC), so a REGLIST vertex meant as "no draw" still draws; the parser's drop table (FACTS §7.1) also drops register 0x1C TEXCLUT (and 0x0B/0x0E/0x0F/undefined) so `GsState::WriteRegister` never receives it. NEW keeps both as parser knowledge (`RegisterIsForwarded`, `P5-5A-DROP-TABLE`): the Phase-6 `GsState` never sees TEXCLUT, pinned by `texclut_dropped`. |
| **PS2 rule violated** | GS User's Manual: in REGLIST mode an XYZ2/XYZF2 word with bit 47 = 1 is treated as XYZ3/XYZF3 (vertex queued, no drawing kick); TEXCLUT (0x1C) sets CBW/COU/COV for CSM2 CLUT buffering and must reach the GS. |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field |
| **Regression test** | ctests `GifParser*` cases 1–10 (REGLIST XYZ2 bit-47 still draws; TEXCLUT dropped; NLOOP=0/EOP=0 count) — PHASE5_IMPL 5A `:126` |
| **Source record** | `docs/phases/PHASE5_IMPL.md` (j) row `P5-REGLIST-ADC-TEXCLUT (+ P5-5A-DROP-TABLE)`; 5A `:115`, `:126`, `:258` |
| **Triage tips** | A stray triangle at a REGLIST strip restart, or a CSM2 texture with a wrong palette row, points here; the `texclut_dropped` counter in the parser tests is the witness. |
