# BUG-058 — Class-native generic shader arms inherit the previous DrawBatch draw's register-program uniforms (CT16 quantisation applied to CT32 draws)

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-058 |
| **Title** | Class-native generic shader arms inherit the previous DrawBatch draw's register-program uniforms (CT16 quantisation applied to CT32 draws) |
| **User-visible symptom** | load_transition vf855 — CT32 accumulator draws after a CT16 frame draw land 5-bit quantised (blue 181 → 176, white → 248): subtly banded/dimmed colours on the transition frame. Reproduced in NEW so the frozen corpus/baselines match |
| **Scene / savestate** | GS corpus capture `load_transition`, frame vf855 (replay); the frames lane |
| **Subsystem** | `gs-classes` (primary); `render-backend` (secondary) |
| **Root cause** | OLD `host_app/graphics/opengl/{skinned,sprite_hud,particles,fmv_blit}_native.inc` generic arms (skinned `:1107-1170`/`:1375`) omit `SetU*` for `u_ct16q`, `u_dthe`, `u_dimx_lo/hi`, `u_draw_scale`, `u_uv_native` (+ blend a..d / fix_alpha, zsrc_num/den per shape) that `DrawBatch` sets on every draw (`opengl_backend.cpp:10817-10823`, `:10660-10671`, `:11136-11140`, `:11288-11292`, `:11457-11459`); the generic program keeps the previous DrawBatch draw's values. NEW: `render::InheritedUniform` + `ClassRenderer::kInherit*Arm` (named, one-line retirement: masks → 0). Live-mode caveat: OLD's live path routes textured VRAM-decode draws to the SPECIALISED programs which carry no `ct16q / dthe / dimx` uniforms — once `has_snapshot` is true a class-routed draw OLD would send to `spec` must NOT inherit (PHASE6_IMPL INTEGRATION CHECKLIST item 5). |
| **PS2 rule violated** | A draw's frame format alone decides the 16-bit storage quantisation (FRAME.PSM of the draw, with DTHE/DIMX of the draw's own environment). |
| **Fix + hatch** | Reproduced on purpose (`kInherit*Arm` masks); retiring it (masks → 0) is an accepted difference against the frozen baselines — requires a `DP-<n>` / EQUIVALENCE_PLAN §7 row; no env var. Ledgered as HACK_LEDGER §3 dual-path row L4 |
| **Regression test** | the corpus sweep (18 captures, `GsCorpus_*`) + the frames lane; load_transition `REPLAY_HASH` identity (PHASE6_IMPL `:79`) |
| **Source record** | `docs/phases/PHASE6_IMPL.md` item 7 L4/L7 (`P6-NATIVE-STALE-UNIFORMS`), 6F′ section; HACK_LEDGER §3 (L4) |
| **Triage tips** | If a scene-transition frame shows CT32 colours quantised to 5 bits (248 whites), look for a preceding CT16 draw through a register program and the generic arm that followed; `render::InheritedUniform` masks are the switch. |
