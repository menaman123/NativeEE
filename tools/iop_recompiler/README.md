# `tools/iop_recompiler/`

**Purpose:** the IOP (R3000A) static recompiler that turns the title's `STREAM.IRX` into
`iop_recompiled.{h,cpp}` (games/<title>/iop/stream_irx/, built as `ps2r_title_stream_irx`).
Built as `ps2r_iop_recompiler` when `PS2R_BUILD_RECOMPILER=ON`; run by the build as a custom
command over the STREAM.IRX extracted from the user's disc image (cmake/TitleInputs.cmake):

```
ps2r_iop_recompiler --input STREAM.IRX --output-header iop_recompiled.h --output-cpp iop_recompiled.cpp --legacy-title-stubs
```

Any other IRX (a title's embedded modules, a stock module out of the IOP reboot image) is recompiled with the generic
options -- the first title's facts stay behind `--legacy-title-stubs`:

```
ps2r_iop_recompiler --input lgAud.irx --base 0x28000 --namespace iop_lgaud --import-call --quiet \
    --output-header iop_recompiled.h --output-cpp iop_recompiled.cpp
```

`--base` relocates the image (its `.rel.*` tables, through the same `engine/iop/iop_relocator.h` the runtime IopLoader
uses) so the emitted addresses are the module's addresses in IOP RAM; `--namespace` wraps the TU so several modules link
into one binary and routes computed calls through `iop_stubs::iop_resolve`; `--import-call` routes every library import
through `iop_stubs::import_call("lib", index, ctx)` -- the engine's `IopLibraryTable` (stock HLE bodies + the other hosted
modules' RegisterLibraryEntries tables) answers it. The generic ABI headers live in `engine/iop/abi/` (include root
`engine/iop/abi/anchor`). The tool refuses an output with UNIMPLEMENTED instructions (listed in its summary).

The output is never committed (it is derived from the disc) and is checked against
`games/<title>/iop/stream_irx/MANIFEST.sha256` after every run.

## Provenance

`PROVENANCE.md`: OLD's `iop_recompiler/` sources plus the generalisation table (what changed, where, behind
which flag). Transplanted code: compiled without -Werror, lint-exempt.
