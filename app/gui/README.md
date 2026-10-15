# `app/gui/`

**Phase:** 8  
**Purpose:** ImGui panels: session gui host (SDL2/GL3 backends), session panel (settings, reproduction saves, recording), savestate browser (+ pure savestate rows), sound section, triage section. `app/gui/third_party/imgui` is Dear ImGui vendored as-is under ITS OWN license (`LICENSE.txt`, `PROVENANCE.md`, `SOURCE_SHA256.txt`); lints exempt `/third_party/`.

## Belongs here

- SessionGui (gui host + ImGui SDL2/OpenGL3 backends), SessionPanel (settings window, reproduction/recording controls), SavestateBrowser, SavestateRows, SoundSection, TriageSection, third_party/imgui

## Does not belong here

- env/globals reads (Config + diag registry only)

## Transplanted from (OLD, read-only)

- `gui/*, host_app/third_party/imgui`

## Rules

- One class per `.h`/`.cpp` pair (`tests/lint/lint_one_class.py`); headers declarations-only; no `.inc` includes.
- No session tags in identifiers or comments; provenance goes in this README as links, never in code.
- Flag outcomes applied at transplant time (NORM winning arm only / SETTING = Config field / DIAG = core/diag / DELETE+TEST-ONLY not written).
- Plan: `docs/design/CONSTRUCTION_PLAN.md` §4.
