# BUG-008 — Shadows missing only at PS2_GS_SCALE=2: the zsrc logical-pixel map skipped the specialized strip shaders

| Field | Content |
|---|---|
| **ID** | BUG-008 |
| **Title** | [SH8] The [GE1] `u_zsrc_num/u_zsrc_den` fragcoord→logical-pixel mapping was wired into generic shaders but NOT the specialized world-strip shaders — the S9 stencil pass-E z-test read the wrong depth pixels at scale 2 |
| **User-visible symptom** | Under the user's enhancement env (`PS2_GS_SCALE=2 PS2_GS_INTERP=1 …`) shadows vanish camera-dependently (ZERO=39/69 frames per sweep); at defaults (scale 1) everything is healthy — SH1-SH7 never ran the enhanced env. |
| **Scene / savestate** | `ps2state_bad_shadows.ps2st`, [SH6-SWEEP] pad script through the user's own `run_play.sh` (env overrides appended). |
| **Subsystem** | gs-classes (primary), render-backend |
| **Root cause** | The chain walk proved the SH7 deposit and the carve correct at scale; the break was the STENCIL pass-E z-test: the [GE1] zsrc scale map (which converts `gl_FragCoord` to logical GS pixels when sampling a depth source rendered at a different scale) was applied to the generic programs but missed the specialized strip shaders used by the world-tristrip native class — so at SCALE=2 the z-test sampled physical pixels as if logical, rejecting the strips. |
| **PS2 rule violated** | Scale invariance contract [GE1]: enhancement scaling must be invisible to GS semantics — every shader that samples cross-scale sources must map coordinates through ONE shared rule. (GS itself has no scale; this is our enhancement layer's own invariant.) |
| **Fix + hatch** | Apply the zsrc map in the specialized strip shaders (code-tagged `PS2_SH8` in `world_tristrip_native.inc`; no env gate — code-level arm only, `PS2_SH8=0` build-time defect arm used for the audit pair). Post-refactor: DrawShapeSpec-parameterized class renderers share ONE shader-side coordinate mapping helper (D5 renderer-profile split), and scale>1 legs join the harness as adjudicated (non-bit) gates. |
| **Regression test** | User-env sweep legs ZERO=0/1 with nz in healthy band; defaults no-regression leg; G-E1 scale gates (1x-vs-2x replay counter blocks identical). |
| **Source record** | `Ps2Recomp/docs/loop_state/PLAYTEST_R1_IMPL.md` (§ [SH8]) |
| **Triage tips** | A defect that exists ONLY at scale>1 is an enhancement-layer coordinate bug: diff the shader paths the scene actually uses (specialized vs generic) — fixes wired into "the" shader often miss cloned specializations. Always re-run a proven fix under the user's REAL env; defaults-only proof is not proof. |
