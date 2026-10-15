# BUG-062 — OLD replay tool quirk: the render-height fallback is the window height seeded at `Init`; a capture without a SetWindow job renders 64 rows tall (replay driver only)

Filed by integration stage IDOC-A (2026-10-01) from the phase record's BUG-KB candidate row (source record below); the record stays the evidence.

| Field | Content |
|---|---|
| **ID** | BUG-062 |
| **Title** | OLD replay tool quirk: the render-height fallback is the window height seeded at `Init`; a capture without a SetWindow job renders 64 rows tall (replay driver only) |
| **User-visible symptom** | replay-tool only — no game-visible effect; five corpus legs depend on the seeded height |
| **Scene / savestate** | GS corpus captures lacking a SetWindow job (five legs) |
| **Subsystem** | `gs-present` (replay driver) |
| **Root cause** | OLD's render-height fallback is the window height seeded at `Init` (`SDL_GetWindowSize`, `opengl_backend.cpp:2952`); a capture without a SetWindow job renders 64 rows tall. NEW `ReplayDriver::BuildScene` seeds `DrawPipeline::OnSetWindow(win w/h)` the same way so the five legs hash identically. |
| **PS2 rule violated** | none — host tool behaviour (the GS has no window; display size is DISPLAY1/2 DW/DH) |
| **Fix + hatch** | Reproduced in `tools/gs_replay/replay_driver.cpp` only; the live path takes its size from the real window; no hatch |
| **Regression test** | the five corpus legs (`GsCorpus_*`) are the pin |
| **Source record** | `docs/phases/PHASE6_IMPL.md` findings row `P6-WINDOW-SEED` (`:147`), item 7 L7 |
| **Triage tips** | A corpus replay that renders a 64-row-tall frame means the capture has no SetWindow job and the seed was lost — check `BuildScene`'s `OnSetWindow` call. |
