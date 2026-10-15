# `engine/title/`

**Phase:** 3
**Purpose:** The title contract. `TitleProfile` (the retail facts of one title as data) and the
`ActiveTitleProfile()` accessor the linked title module defines. Title-neutral: this directory
names no serial and holds no title's values.

## Belongs here

- `title_profile.h` — the `TitleProfile` shape + `ActiveTitleProfile()` declaration

## Does not belong here

- any title's values (those are `games/<serial>_<slug>/hle/title_profile.cpp`)
- PS2 semantics, hooks, mechanisms

## Rules

- Header-only (`ps2r_title_api`, an INTERFACE target). No serial, no game token —
  `LintEnginePurity` scans this directory like the rest of `engine/`.
- The accessor is declared here and defined by the title. A build with `PS2R_TITLE` empty
  links no definition; every target that calls it is skipped by its own CMakeLists, the same
  way the generated-recomp targets are.
