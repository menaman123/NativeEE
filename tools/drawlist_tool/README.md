# `tools/drawlist_tool/`

**Phase:** 6  
**Purpose:** CPU-only inspector and differ for `.gsdl` recordings.

```
drawlist_tool stats  <rec>
drawlist_tool frames <rec>
drawlist_tool dump   <rec> [--frame N] [--verts] [--max N]
drawlist_tool diff   <a> <b> [--frame N] [--offset K] [--tol-xy F] [--tol-z F] [--tol-rgba N] [--tol-st F] [--ignore-tex-hash] [--max N]
```

`diff` aligns frame i of A with frame i+K of B and compares the ordered draw lists: GS state exactly, vertex counts exactly, vertex attributes within tolerance; exit 0 iff no frame mismatched, 2 on usage / load errors.

## Contents

- `drawlist_tool.cpp` — `main` + the four subcommands over `tests/lib` `GsdlReader`.

## Does not belong here

- Replay / rendering (`tools/gs_replay`); the format itself (`engine/gs/hle/ir/gsdl_format.h`).

## Transplanted from (OLD, read-only)

- `tests/drawlist_tool.cpp`

## Rules

- Output grammar is pinned: ctest `GsDrawlistToolDiffClean` (and the stage record's OLD-vs-NEW diff on the corpus) prove identity with the frozen tool.
- No session tags; no game constants.
