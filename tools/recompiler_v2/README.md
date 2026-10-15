# `tools/recompiler_v2/` — the EE recompiler (NEW owns it: DP-23)

**Stage:** P8.8-T (2026-10-03); first fix P8.9-A (2026-10-03, BUG-072)
**Purpose:** The PS2 EE (R5900) static recompiler that produces `games/<title>/recomp/`
(`recompiled.h`, `recompiled.cpp`, 32 shards). Until this stage the only producer of that
output was the frozen OLD binary `OLD/build-v2-nog/recompiler_tool_v2`; BUG-072 (a function
split at its own branch delay slot) needs a GENERATOR fix, so the tool's sources now live here.

## What is here

Byte-identical copies of OLD's `recompiler_tool_v2` target (OLD `CMakeLists.txt:78-100` and the
`recomp_v2_engine`, `EEAnalyze`, `elfio`, `rabbitizer` targets it links), laid out as OLD has them so
OLD's `PROJECT_ROOT`-relative includes resolve against this directory:

| Tree | Files | OLD origin |
|---|---|---|
| `recompiler_tool/` | 24 | `OLD/recompiler_tool/{main_v2.cpp,recompiler.{h,cpp},emit/,translate/,corpus/,discovery/,elf/,legacy/}` (the target's transitive include closure; `main.cpp`, `vu_aot_gen.cpp`, `inject_fun_1017c8_*` are NOT part of the v2 target and were not copied) |
| `EEAnalyze/` | 5 | `OLD/EEAnalyze/{analyze.{h,cpp},DataFlowEngine.h,Function.h,register_state.h}` |
| `host_app/` | 3 | `OLD/host_app/{cpu_state.h,memory.h}` — headers `recompiler.cpp` compiles against (the generator's view of the register file); NEW's engine never includes them. `host_app/common/Pcsx2Types.h` is NOT a copy: OLD's file is a verbatim PCSX2 header (SPDX GPL-3.0+, forbidden by STAGE_RULES rule 5); NEW provides its own header of the same name/type names (PROVENANCE.md row marked NEW-authored; INT-P88T-PCSX2-GPL-HEADER) |
| `include/json.hpp` | 1 | `OLD/include/json.hpp` (nlohmann/json, MIT — licence text in-file, lines 6-7; vendored here, not under `third_party/`, because the tool's `#include <include/json.hpp>` needs this layout) |
| `../../third_party/elfio/` | 16 | `OLD/include/ELFIO/elfio/*.hpp` + LICENSE (MIT) |
| `../../third_party/rabbitizer/` | 234 | `OLD/include/rabbitizer/{src,cplusplus/src,include,cplusplus/include,tables/tables}` + LICENSE (MIT) |

`PROVENANCE.md` in each tree lists OLD path, size and md5 of every file. Ctest
`RecompToolProvenanceIntact` (`tests/recomp/check_provenance.py`) re-hashes the trees against those
tables and fails on an edited copy or an unlisted file. A deliberate fix (P8.9-A on) updates the row
with the new md5 and names the change.

## Build

CMake option `PS2R_BUILD_RECOMPILER` (default ON) adds this directory from the root `CMakeLists.txt`
(it enables the C language for rabbitizer's `src/*.c`). Targets: `ps2r_tp_elfio` (INTERFACE),
`ps2r_tp_rabbitizer`, `ps2r_rv2_eeanalyze`, `ps2r_rv2_engine`, `ps2r_recompiler_v2`
(`build/tools/recompiler_v2/ps2r_recompiler_v2`). Same flags as OLD's target (C++17, no `-Werror`;
0 warnings on the dev toolchain at transplant time). OLD linked `SDL2::SDL2`; no source in the
closure includes an SDL header, so it is not linked (the build proves it).

## Regenerating the title's output

```
cmake --build --preset macos-dev --target ps2r_recompiler_v2
tools/regen_from_frozen.sh --tool build/tools/recompiler_v2/ps2r_recompiler_v2 --out "$PWD/build/regen_new/a"
tools/regen_from_frozen.sh --tool build/tools/recompiler_v2/ps2r_recompiler_v2 --out "$PWD/build/regen_new/b"
diff -r --exclude=cwd --exclude=recomp_gen.log build/regen_new/a build/regen_new/b          # determinism
diff -rq --exclude=cwd --exclude=recomp_gen.log --exclude='MANIFEST*' --exclude=INVOCATION.md \
     --exclude=README.md --exclude=CMakeLists.txt --exclude=inputs \
     build/regen_new/a games/<title>/recomp/                              # vs the frozen output
```

The same protocol as the frozen tool (cwd pinned to a scratch dir holding the two stub inputs; the
tool writes `logs/recompiler_tool_v2/runtime_log.txt` relative to cwd; `--shards 32 --direct-calls`;
no `--ghidra-input`). P8.8-T: both diffs EMPTY and the 34 SHA-256s equal `MANIFEST.sha256`, i.e. this
build of the unmodified copy IS the frozen generator. Installing a regenerated set into
`games/<title>/recomp/` (with its manifests) is a stage decision, not something `--tool` does.

## Rules

- Lint-exempt (LintOneClass `/tools/recompiler_v2/`, `/third_party/`; LintEnginePurity does not
  scan `tools/`): a transplanted tool, title-aware by nature, not NEW engine code. `CoreSingleGetenvSite`
  pins the `getenv` TEXTS in the copies per file (generator string literals + OLD's `memory.h`).
- OLD stays read-only; fixes are made HERE, each with a PROVENANCE.md row update and a regenerated,
  re-manifested `games/<title>/recomp/` whose golden diff names exactly the intended sites.

## P8.9-A — the first fix made here (BUG-072, DP-18 / DP-23)

- `recompiler_tool/discovery/functions.cpp`: (1) **a weak seed in a branch delay slot is never a
  function start** (`Walker::in_delay_slot`, applied in `eligible` to prologue / data-pointer /
  code-pointer seeds; the slot executes with its transfer on both paths, so a function cannot begin
  there — the retail `0x178470` shape, whose `addiu sp,sp,-0x10` sits in the guard branch's delay slot,
  was split there and leaked 0x10 of sp per call); a STRONG seed in a delay slot (jal / table / ELF
  entry) is kept and reported. (2) **every externally-entered address is a leader of the function that
  contains it** (branch fall-through, jal/jalr return site, external jump-table target), so the emitted
  `switch (addr)` has a case for it. `discovery_types.h` carries the two diagnostic sets;
  `main_v2.cpp` prints `[v2] delay-slot rule: N weak seed(s) rejected, M strong entr(y|ies) in a delay slot`.
- `PROVENANCE.md`: the three files carry a `diverged at P8.9-A` note with OLD's md5; the other 280
  files are still byte-identical copies. `RecompToolProvenanceIntact` re-pinned.
- `tools/regen_from_frozen.sh` now runs THIS tool by default (`build/tools/recompiler_v2/ps2r_recompiler_v2`,
  md5 reported); `--frozen-old` runs OLD's frozen binary (the pre-fix generation, for proofs/diffs).
- Result on the title: `[v2] discovered 8807 functions, 177 jump-table sites, 8848 unreached gap words`
  (was 8819; the same gap words — no code lost), 12 weak seeds rejected, 1 strong entry reported
  (`0x2d2180`, preceded by an unexecuted `jr ra` padding word). Pins: `RecompDiscoveryDelaySlotRule`
  (synthetic fixture, `tests/recomp/test_discovery_delay_slot.cpp`), `Bug072SpBalance`,
  `RootAnchorF57292` / `RootAnchorF59991` (600 frames).
