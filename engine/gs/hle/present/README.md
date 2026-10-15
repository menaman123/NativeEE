# `engine/gs/hle/present/`

**Phase:** 6  
**Purpose:** Scanout composition, present policy + pacing, decimation (video.fps_cap), CRTC classifier, viewport math, view state, composite guard.

## Belongs here

- Landed (6D): `CrtcClassifier` (dual-circuit one-line rule, packed snapshot, native config via `VramLayout`), `ViewportMath` (fit / fill / integer / custom rects), `ScanoutComposer` (scanout fbp / DISPFB / content dims; `GrabDisplay` = the scanout target's colour attachment, most-recent-drawn fallback, full physical rect RGBA8), `Presenter` (MarkEeFrameEnd, grab hook, Backend::Present). 6F: `ScanoutComposer::PresentBlank` / `Presenter::PresentBlank` — the swap without the display quad or any mirror maintenance, the corpus replay's frame end (the reference replay tool's present was the pending flush + swap; its scanout composite never ran).
- Landed (6D‴): `ScanoutComposer::DrawScanout` — the display quad (display-only, no hash reads it): the pick (scanout target with content → most-recent-drawn fallback → the stale-display stand-in by the title's main frame buffer, `VramLayout::main_fb_fbp`, ages 30 / 16 frames), the `Scanout` ledger flush over the display footprint, NATIVE (CRTC class dual-identical-1px + the queue-order DISPFB tuple is the native tuple + no fallback + `ResolvesToTarget` over the `VramImage` page authors) → ledger record + `NoteGpuElided`, else the drawn-rows deposit AT THE SCANOUT BASE (DISPFB-format encode for the stale-display round trip); then the colour attachment over the content rect (`Backend::Present`), or the mirror decode (`TextureDecoder::DecodeScanoutBuffer` → scratch surface) for the round trip / a target without a colour view. OLD `opengl_backend.cpp:24290-25037` (`DrawScanout` live arms: pick `:24290-24385`, flush `:24790-24798`, native `:24752-24815`, deposits `:24842`, `:24857`, quad `:25030-25036`), `:18540-18620` (`DrawScanoutFromRT`), `:18796-19060` (`ScanoutFromVRAM`), `gs_page_tracker.h:322-350` (`ScanoutResolve`). `PS2_GL_SCANOUT_RT` (default ON: from-RT) is the winning arm.
- Landed (row 6.7, I8.10-A): `PresentPolicy` -- the play-mode per-boundary decision {Live: the GS produced a grabbable image this boundary | Hold: the last shown image again (the CRTC re-reads the frame buffer every field; a load screen or the gap between movie pictures shows the previous picture, never black) | None: before the first image}. `video.fps_cap` bound here (0 / >= 60 = every field; below: an even accumulator share of the fields, the first image always shown). Pure: no clock, no GPU; the composition root owns the held pixels and consults it ONLY in play mode (the deterministic Verify-slot rule is untouched; pin `RootPlayPolicyInert`). The display rate is the pacer's period (DP-13's wall-locked boundary: one boundary per field). The window swap itself stays the slot-0 `Presenter::Present` (the backend presents when content exists; a boundary without content leaves the last swapped frame on screen -- the same hold on the GPU side). Census `present_policy=live=,hold=,none=,cap=,cap_skips=,fmv=`; the FMV-active signal's third consumer (counted; the table is the same during FMV).
- Later: PresentDecimator (the cap at the GPU swap), Pacer(+SampleRing, BandSelector, PacePlan, CapStepper, LoadGrace), ViewState, CompositeGuard; `Presenter::Present` integration over `DrawScanout` (6E).

## Does not belong here

- loading-logo / base-layer special cases (games/<title>/hle/scene_hooks)

## Transplanted from (OLD, read-only)

- `graphics_thread.cpp, present_pace.h, quantum.h half, gs_crtc_class.h, graphics/viewport_math.h, display_settings live half, opengl_backend.cpp scanout`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
