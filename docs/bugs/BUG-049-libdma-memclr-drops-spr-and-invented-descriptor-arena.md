# BUG-049 — libdma HLE: `Memclr` drops stores outside main RAM (scratchpad), and `sceDmaGetChan` hands out an invented descriptor arena at `0x01CFF000`

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-049 |
| **Title** | libdma HLE: `Memclr` drops stores outside main RAM (scratchpad), and `sceDmaGetChan` hands out an invented descriptor arena at `0x01CFF000` |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit); a `sceDmaGetChan` caller comparing the returned descriptor against the SDK's static table, or a `Memclr` over the scratchpad, would misbehave |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `gif-vif-dmac` (libdma HLE); `ee-mem` (secondary) |
| **Root cause** | OLD `sce_hle/libdma.cpp:22-79`: `Memclr` clears 16-byte chunks through `MainMemory`-only pointers, so a destination in the scratchpad (`0x70000000`) is silently dropped (`P5-5F-MEMCLR-SPR`); `sceDmaGetChan($a0 = 0..9)` returns `0x01CFF000 + 0x40·ch`, a descriptor arena the host invented (the SDK's real descriptors live in libdma's `.data`), zero-filled on first request (`P5-5F-LIBDMA-ARENA`, HACK_LEDGER libdma row). NEW `LibdmaBindings` transcribes both with the divergence stated in the header + README; bodies installed by the composition root. |
| **PS2 rule violated** | SDK libdma: `sceDmaGetChan(id)` returns the address of libdma's own static `sceDmaChan` table entry for that channel (a symbol the manifest resolves), and `Memclr`-class helpers write every byte of the requested range wherever the EE maps it (scratchpad included). |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field. Root fix = manifest-resolved `sceDmaChan` symbol (the real table) and a `GuestMemory` store that reaches the scratchpad. |
| **Regression test** | ctest `DmacLibdmaRows` (6 rows, `tests/dmac/test_dmac_libdma.cpp`) pins the arena and the `Memclr` observable |
| **Source record** | `docs/phases/PHASE5_IMPL.md` (j) row `P5-5F-MEMCLR-SPR / P5-5F-LIBDMA-ARENA`; 5F `:1891`, `:2022`; (k) 5F REVIEW §5.1; OLD `sce_hle/libdma.cpp:22-79` |
| **Triage tips** | Pointers near `0x01CFF000..0x01CFF280` in a dump are the invented arena — a title reading `sceDmaChan` fields the HLE never fills gets zeros. A scratchpad range that stays dirty after a libdma clear is the `Memclr` drop. |
