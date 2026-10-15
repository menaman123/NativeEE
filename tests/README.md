# `tests/`

**Phase:** 0+  
**Purpose:** ctest suites + replay tools (gs_replay, vu1_replay, vif_replay, drawlist_tool). Top-level tree mirrors module paths (tests/gs/format, tests/ee_timing, …). Replay tools build against subsystem libs only.

## Belongs here

- lint/, core/, oracle/, per-module suites, lib/, support/

## Does not belong here

- the differential harness (verify/), dead OLD root tests (never)

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.

- `test_title_logo_draw.cpp` / `test_title_frame_replay.cpp` (P8.21-A; `--repeat n` replays the frame n times with a boundary between passes so the title's display copy carries content — `Render_TitleFrameReplayDisplay`): one draw / one whole frame of the PCSX2 title dump (`verify/baseline/title_screen/`) through the full lowering over GlDevice, measured against the console's screenshot regions (`Render_TitleLogoDrawPin`, `Render_TitleFrameReplay`); the frame tool takes `--skip a-b` / `--only a-b` / `--upload-dbp hex` / `--uploads-first` / `--dump out.ppm` for ablation; the logo pin carries the dump's MIPTBP1 chain and takes `--split <verts>` (the live run's per-kick strips) (P8.24-A). `tests/gs/texsrc/test_title_atlas_decode` takes `--vram-in` (a stored page, e.g. a `TexresTrace` window) and `--dump-levels <prefix>` (the authored mip levels 1-2). `tests/app/root_title_screen_effects.py` = `RootTitleScreenEffects`: the live title gate (Release coldboot 2520, frame 2519's ORB / LOGO / OCEAN vs the console; `--frame <ppm>` judges an existing frame; `--selftest`). `tests/render/attract_replay_pin.py` = `Render_AttractReplay_<dump>` ×5 (P8.36-R): each `coldboot_attract` dump's frame 1 exported at test time by `tools/gsdump/gsf1_export.py` into the build's scratch dir, replayed through the full lowering and measured against the dump's own screenshot — the letterbox rows black (asserted), the six cutscene region means within the live gate's 12/channel + the CAPTION bright share, the title dump by the three title regions; `--measure-only` records without asserting; `--stream` replays an existing GSF1; `--selftest`.
