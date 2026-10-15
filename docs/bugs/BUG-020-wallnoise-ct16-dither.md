# BUG-020 — Screen-door noise on dark walls: DTHE/DIMX dropped at GIF, dither hardcoded and scale-wrong

| Field | Content |
|---|---|
| **ID** | BUG-020 |
| **Title** | [WN4-CT16Q] The GIF layer logged-and-DROPPED the game's DTHE/DIMX registers; the backend applied a hardcoded 4x4 dither matrix gated only on frame format — wrong matrix, wrong scale period, double quantization on the CT16 chain |
| **User-visible symptom** | Fine dense periodic small-rectangle noise on the DARK, low-contrast cavern wall, flickering with a still camera, clean where brighter — the signature of 16-bit quantization/dither error. |
| **Scene / savestate** | `save_states/ps2state_slot0_f39807.ps2st`, camera still; screen-door metric on the dark-wall band. |
| **Subsystem** | gs-format (primary), gif-vif-dmac |
| **Root cause** | The game's dither state never reached the renderer: `gif.cpp` parsed DTHE/DIMX but only logged them (no `g_gs_state` storage); `blend.glsl applyDither` used a hardcoded 4x4 matrix keyed only on frame format. Consequences on the cavern's CT16/CT16S accumulator chain: the game's authored DIMX pattern ignored; at SCALE=2 the dither period doubled on screen; and decode→re-render round-trips re-quantized already-quantized values (double quantization) — all rendering as dark-region screen-door noise. |
| **PS2 rule violated** | GS dithering rules (GS manual, DTHE/DIMX): CT16-family writes apply the game's 4x4 signed DIMX matrix in NATIVE pixel space during the 32→16 quantize, once per write — not a host-chosen matrix, not per-present, not at render scale. |
| **Fix + hatch** | `[WN4-CT16Q]` retail CT16 write rule (hatched, default ON): store DTHE/DIMX in GS state, apply the game's matrix at native-pixel coordinates in the CT16 quantize path, and fold `dthe/dimx` into the merge keys so state changes split batches correctly. Bounded residual documented (≤ one 5-bit step — the [WN4] precedent the accepted-difference protocol cites). Post-refactor: `dthe, dimx[4][4]` are first-class GsRegisters fields and DrawIR records them (ARCH §4.1/§4.6 — closes the .gsdl dither gap). |
| **Regression test** | Screen-door metric before/after both envs; frozen corpus adjudicated + re-frozen (18/18) + 1 new capture; anchors 0 FATAL; ctest ×3 both binaries. |
| **Source record** | `Ps2Recomp/docs/loop_state/CAVERN_WALLNOISE_IMPL.md` ([WN1]-[WN4]) |
| **Triage tips** | Noise that lives ONLY in dark/low-contrast regions is quantization-domain (the step size dominates small signals) — check the 16-bit chain before texturing. grep for registers that are parsed-then-logged-only: a register the game writes and we drop is a standing footgun. Dither must be applied in NATIVE pixel space; if an artifact's period doubles at scale 2, the pattern is being applied post-scale. |
