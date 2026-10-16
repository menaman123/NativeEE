# PROVENANCE -- `tools/iop_recompiler/` (OLD's sources + the generalisation below)

OLD = `$PS2R_OLD_ROOT` (read-only; HEAD `5e1b17feed02552a0aa15e71d03c40a12c20e6d9` at copy time, 2026-10-14).
The three files below are UNMODIFIED copies of OLD's `iop_recompiler/{main.cpp, iop_recompiler.h, iop_recompiler.cpp}`
(the md5 is of both). OLD's `iop_recompiler/ExportIopFunctions.java` (a Ghidra export script) is not needed and not copied.

Proof of equivalence with the frozen tool whose output the tree was developed against: a scratch build of these
sources against `third_party/rabbitizer` + `third_party/elfio` run over STREAM.IRX (md5 `d24082fd3560959a77d18ae9c43db51b`,
116156 bytes) emitted `iop_recompiled.h` md5 `c95059f165f8b68de442169ac932f871` and `iop_recompiled.cpp` md5
`b2102157c7dfa8285f21eadbb42c41a4` -- the same bytes as the frozen `OLD/build-v2/iop_recompiler_tool` output recorded in
`games/<title>/iop/stream_irx/MANIFEST.md` (the committed files it replaced were byte-identical).

Transplanted code is compiled WITHOUT -Werror and is lint-exempt (as tools/recompiler_v2 is). The 'getenv' text in
`iop_recompiler.cpp` is the GENERATOR'S literal for the PS2_STRMQ probe arms it writes into the output (tests/core/check_getenv_sites.py).

| File (relative) | OLD path | bytes | md5 |
|---|---|---|---|
| `main.cpp` | `iop_recompiler/main.cpp` | 24747 | `6f3a49b5405e2a08b97b7880f11c985f` |
| `iop_recompiler.h` | `iop_recompiler/iop_recompiler.h` | 2348 | `dd06914c171de0099f8ee82e6b80781d` |
| `iop_recompiler.cpp` | `iop_recompiler/iop_recompiler.cpp` | 47800 | `5a0f6324c4d01a91458aaa56d935aa47` |


## Divergence from OLD (the generalisation to any IRX; the first title's behaviour behind `--legacy-title-stubs`)

The three files are no longer byte-identical to OLD's (md5 at the time of writing: `main.cpp` `be670997820ccb7d17cc937ff05b0105`,
`iop_recompiler.h` `4421463f2fa364de61769c0e8d632409`, `iop_recompiler.cpp` `d45b39996329ab6f94da2873f1d57022`). Every first-title
fact OLD hardcoded is now gated behind `--legacy-title-stubs` (default OFF); `tools/regen_from_frozen.sh --iop` passes it, so the
first title's regeneration keeps the frozen bytes. **Not re-proved here**: no STREAM.IRX was available on the machine that made this
change, so the byte identity under the flag is argued by construction (every legacy branch emits the OLD text; the generic branches
are reached only without the flag or with `--namespace`), not by a regen run -- run `regen_from_frozen.sh --iop` where the disc is.

| Change | Where | Gate |
|---|---|---|
| The 16 pinned function starts, the `la` guard's `hi <= 1` bound, the IMPORT_STUB_START/END exclusion range | `main.cpp` builtin_analysis / pass 3 | legacy ON only |
| The 5 `PS2_STRMQ` probe arms and the `0x128a8` `hle_stream_cd_read_rpc` thunk | `iop_recompiler.cpp` recompile_function | legacy ON only |
| Import-table scan reports table ranges; export-table scan (`0x41C00000`); table regions are never emitted as code and cap the preceding function | `main.cpp` identify_import_stubs / identify_export_tables / pass 3 | legacy OFF |
| Function starts from export entries and from relocated pointers (R_MIPS_32, HI16/LO16) that follow a `jr $ra` / `j` or open a frame; pointer-derived starts are WEAK and dropped when the preceding function branches into them (switch labels) | `main.cpp` builtin_analysis | legacy OFF |
| Function ends extend over branch targets past the last `jr $ra` (cold blocks after the return) | `main.cpp` pass 3 | legacy OFF |
| `--base <addr>`: the image is relocated (engine/iop/iop_relocator.h -- the loader's code) before disassembly; every emitted address is relocated | `main.cpp` parse_iop_elf | option |
| `--namespace <id>`: the TU lives in `namespace <id>`; indirect calls (jalr, jr to another function = tail call, J/JAL to an unknown target) go through `iop_stubs::iop_resolve`; `kBase/kEntry/kLoadExtent/kImportStubs` emitted | `iop_recompiler.cpp` writers / emit_indirect_call | option |
| `--import-call`: every import -> `iop_stubs::import_call("lib", idx, ctx)` (no silent `/* UNKNOWN STUB */ printf` fallback) | `iop_recompiler.cpp` import_call_expr | option |
| `--quiet`; unknown arguments are an error; an UNIMPLEMENTED instruction fails the run (listed in the summary) | `main.cpp` | always (OLD ignored unknown arguments) |
| `b` / `bal` pseudo-branches (rabbitizer's ids for `beq $zero,$zero` / `bgezal $zero`) are emitted as goto / link+call instead of being dropped | `iop_recompiler.cpp` | always (OLD's one title had none: 0 such sites in its TU) |
| ELF loaded through an in-memory stream; the .text bytes are copied (OLD kept a pointer into a destroyed ELFIO reader) | `main.cpp` parse_iop_elf | always (no output change) |
