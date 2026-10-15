# `games/example_title/`

**Purpose:** The template title — the contract a real title module implements. Copy this
directory to `games/<serial>_<slug>/`, delete this template, and fill it in.

It defines exactly one target, `ps2r_title_hle`, holding the one thing every title must supply:
a `TitleProfile`. Everything else is a stub with a `STATUS` line explaining what belongs there.
That means the tree configures, the engine and its component tests build, and `nativeee` runs —
and reports that no title supplies a play composition yet.

## The contract

A directory under `games/` is a title when it has `hle/CMakeLists.txt`. The root CMakeLists
discovers it automatically; with several present, choose one with `-DPS2R_TITLE=<directory>`.

| Path | Target | What a real title puts here |
|---|---|---|
| `hle/` | `ps2r_title_hle` | **Required.** `ActiveTitleProfile()` plus the native replacements the engine dispatches to: EE functions, loader sync, graphics / audio / FMV / streaming services, workarounds, and `sdk_bindings.tsv`. |
| `recomp/` | `ps2r_title_recomp` | The recompiled EE code. Never committed — generated into the build directory from the user's own ELF. |
| `iop/stream_irx/` | `ps2r_title_stream_irx` | The title's custom IOP module, recompiled. Also generated. |
| `vu1_library/` | `ps2r_title_vu1_library` | One native routine per VU1 microprogram, keyed by microcode hash. |
| `renderer_profile/` | `ps2r_title_renderer_profile` | The GS draw taxonomy, native renderer `Accept()` tables and VRAM layout. |
| `TITLE_INPUTS.sha256` | — | **Required.** SHA-256 of each disc file the generated code is made from. The build refuses any other bytes. |

Every target except `ps2r_title_hle` is optional: consumers gate on `if(TARGET ps2r_title_*)`,
so an absent one is a skip, never an error. That is what makes a partial port buildable.

## Belongs here

- the title's own facts, data and native code; its own tests

## Does not belong here

- engine mechanisms (those are `engine/`, and `LintEnginePurity` keeps them title-free)
- the game's own files — discs, ELFs, IRX modules, extracted assets are **never** committed;
  they come from the user's copy at build time (`cmake/TitleInputs.cmake`)

## Porting, in order

1. **Fill in `TitleProfile`** (`hle/title_profile.cpp`) — serial, ELF facts, boot environment.
   `TITLE_INPUTS.sha256` gets the real hashes. The build can now take your disc.
2. **Get the EE code recompiling** — `ps2r_title_recomp` over the generated output.
3. **Boot on the fallbacks.** The engine's hardware models carry anything with no native
   implementation. Expect it to be slow; expect it to be *correct*.
4. **Replace the fallbacks, subsystem by subsystem** — VU1 programs, the renderer profile, the
   hot EE functions, the custom IOP module. This is the bulk of the work, and
   `docs/design/HLE_COVERAGE.md` is where it is tracked.

README.md's "Per-title HLE" section explains why step 4 exists and cannot be skipped.
