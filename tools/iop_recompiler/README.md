# `tools/iop_recompiler/`

**Purpose:** the IOP (R3000A) static recompiler that turns the title's `STREAM.IRX` into
`iop_recompiled.{h,cpp}` (games/<title>/iop/stream_irx/, built as `ps2r_title_stream_irx`).
Built as `ps2r_iop_recompiler` when `PS2R_BUILD_RECOMPILER=ON`; run by the build as a custom
command over the STREAM.IRX extracted from the user's disc image (cmake/TitleInputs.cmake):

```
ps2r_iop_recompiler --input STREAM.IRX --output-header iop_recompiled.h --output-cpp iop_recompiled.cpp
```

The output is never committed (it is derived from the disc) and is checked against
`games/<title>/iop/stream_irx/MANIFEST.sha256` after every run.

## Provenance

`PROVENANCE.md`: byte-identical copies of OLD's `iop_recompiler/` sources, proved to reproduce the
frozen tool's output byte-for-byte. Transplanted code: compiled without -Werror, lint-exempt.
