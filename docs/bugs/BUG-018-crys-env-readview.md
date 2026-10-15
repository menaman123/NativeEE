# BUG-018 — Close-up crystal rainbow garbage: RT reads served in the target's geometry instead of the read's view geometry

| Field | Content |
|---|---|
| **ID** | BUG-018 |
| **Title** | [S111-READVIEW] The STQ prescale used the TARGET's dimensions for an RT-backed read whose TEX0 window (TW/TH) differed — every configuration rode the same wrong mapping ("no hatch removes it") |
| **User-visible symptom** | Red/yellow crystal facets covered in fine horizontal blue/cyan/white stripes + blocky rainbow patches at close range; ablation shows layer A (env read CT24@0x280000) is the carrier. |
| **Scene / savestate** | `ps2state_slot0_f46203_red_crystal.ps2st`, `ps2state_slot0_f46949_yellow_crystal.ps2st`. |
| **Subsystem** | gs-texsrc |
| **Root cause** | `vs_crys_dual` classifies as `kHleOther` (no registered class renderer), so its draws run the raw DrawBatch → ResolveTextureSeam → FindTargetTexture path. The GPU serve bound the target FBO, but the STQ prescale used `uv_tex_w/h` = the TARGET dims, while the read's TEX0 declares a different texel window (1<<TW × 1<<TH). Net UV mapped the window into the wrong pixel space — a pure geometry error on the shared serve path, which is why NO hatch (stale/decline/never-stale) removed it. |
| **PS2 rule violated** | The A3 §3 texture-source rule: a read returns stream-ordered bytes **in the read's view geometry** — TEX0's TW/TH/TBW define the sampling space, independent of however the underlying target is shaped. |
| **Fix + hatch** | `[S111-READVIEW]`, hatch `PS2_GL_RT_READVIEW` (default ON): the STQ prescale uses the READ's texel window dims while `u_tex_size` keeps target dims — net uv = (s/q)·2^TW/target_w. FST=1, depth/idx/alt16 serves and shuffle reads keep legacy dims (their shaders consume the target-dim contract). Engages only when window dims != target dims (census `readview_fix`; bit-identical otherwise). Post-refactor: `uv_prescale {w,h}` field of TextureSource (ARCH §4.5). |
| **Regression test** | Census-gated engage counter; crystal states clean post-fix; corpus byte-identity (fix engages only on mismatched-window reads, which the corpus adjudicated). |
| **Source record** | `Ps2Recomp/docs/loop_state/CAVERN_CRYSENV_IMPL.md` ([CE1]-[CE4]), `CAVERN_CRYSTRIPES_IMPL.md` (prior layers) |
| **Triage tips** | When NO hatch changes an artifact, stop bisecting hatches — the defect is on the shared path all of them ride; instrument the actual bound texture + sampler uniforms at the draw. "Rainbow" garbage on an indexed/aliased read usually means the sampling GRID is wrong (geometry), not the bytes. Check TEX0.TW/TH against the bound surface dims first. |
