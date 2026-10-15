# BUG-061 — A blended DATE-only draw leaves the previous draw's GL blend enable/func in place (OLD; reproduced)

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-061 |
| **Title** | A blended DATE-only draw leaves the previous draw's GL blend enable/func in place (OLD; reproduced) |
| **User-visible symptom** | none observed — latent in the frozen baselines: the 12 harness legs and the 3,293-case VU1/GIF corpus never exercise the arm (every baseline hash was produced WITH the OLD behaviour, so NEW reproduces it bit-for-bit) (reproduced verbatim; recorded so a future difference in that corner is explained) |
| **Scene / savestate** | n/a — no anchor reproduces it (not reached by the 12 legs); the regression test is the reproduction |
| **Subsystem** | `render-backend` (GL draw executor); `gs-classes` (secondary) |
| **Root cause** | OLD: a blended DATE-only draw that is neither RT-textured nor FBA/fbmsk16/wrap (`alpha_blend && needs_rt_copy && !sw-blend route`) leaves the GL blend enable/func of the PREVIOUS draw in place — `opengl_backend.cpp:11291-11294` only disables for `!alpha_blend`. NEW `GlDrawExecutor::BlendState` reproduces the second branch verbatim. |
| **PS2 rule violated** | GS ALPHA_1/2 + TEST.DATE: each draw's blend equation is its own context's ALPHA register (A−B)·C+D — never the previous primitive's. |
| **Fix + hatch** | none — reproduced OLD-faithfully in NEW (bug-compatible) so the baselines stay identical; the correction is an EQUIVALENCE_PLAN §7 accepted-difference candidate (a `DP-<n>` decision), not a hatch; no env var, no Config field |
| **Regression test** | `Render_BlendFfMapTable` / the 6C blend-state pins; the corpus sweep |
| **Source record** | `docs/phases/PHASE6_IMPL.md` findings row `P6-BLEND-INHERIT` (`:106`), item 7 L7 |
| **Triage tips** | A DATE-masked blended draw that blends with the wrong factors right after a differently-blended draw — compare the executor's `BlendState` branch taken against the draw's own ALPHA. |
