# BUG-NNN — <short title>

[REV2 2026-09-23 D4-D8] Template for `docs/bugs/` entries. One bug = one file,
named `BUG-NNN-<slug>.md`. Every field is mandatory; "n/a" must say why. Entries
are seeded from the current repo's `docs/loop_state/` records and MUST cite the
source record path(s) — the KB is a distillation, the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-NNN (stable, never reused) |
| **Title** | one line, symptom-first |
| **User-visible symptom** | what a player sees/hears/feels, in plain words |
| **Scene / savestate** | the anchor state(s) + frame window that reproduce it (`save_states/…`, vf range, pad script if needed) |
| **Subsystem** | one primary + optional secondary, from the fixed vocabulary below |
| **Root cause** | the mechanism, 3-8 sentences, named code sites |
| **PS2 rule violated** | the hardware/OS semantics our runtime broke (GS manual section, SDK behavior, stream-order rule, timing contract…) |
| **Fix + hatch** | what landed, where, and the escape hatch name + default (post-refactor: the hatch's disposition per A2/MIGRATION) |
| **Regression test** | the named test/gate that pins the fix (ctest name, corpus gate, harness leg); if none exists yet, the INVARIANTS.md work item |
| **Source record** | path(s) into `Ps2Recomp/docs/loop_state/` (read-only history) |
| **Triage tips** | "if you see X, look at Y" — the transferable lesson |

Subsystem vocabulary (keep INDEX.md groupable): `recompiler`, `ee-timing`,
`ee-kernel`, `ee-mem`, `cdvd-loader`, `iop-sound`, `vu1-hle`, `gif-vif-dmac`,
`gs-format`, `gs-vram`, `gs-texsrc`, `gs-targets`, `gs-classes`, `gs-interp`,
`gs-present`, `render-backend`, `input`, `savestate`, `core-log`, `threading`.

Symptom vocabulary for the INDEX (pick ≥1): `freeze/hang`, `crash`,
`black-screen`, `wrong-texture`, `flicker`, `patchwork/blocks`, `noise/dither`,
`shadows`, `aliasing/shimmer`, `geometry-missing`, `audio-silence`,
`audio-desync`, `slow/fps`, `input-lag`, `progression-blocked`, `save/load`.

Rules:

- Root cause names the DEFECT, not the patch. If the entry documents a
  workaround that was later root-fixed, say both and link the
  HOOK_RECLASSIFICATION row.
- "PS2 rule violated" is the field that makes the KB portable to a second game:
  it states the invariant of the machine, not of the title. If the rule
  is game-idiom-specific, mark it `[title-idiom]`.
- Triage tips feed TRIAGE.md; write them so a stranger can act on them.
- Never edit history: if a fix is later superseded, append a `Superseded by`
  line; do not rewrite the entry.
