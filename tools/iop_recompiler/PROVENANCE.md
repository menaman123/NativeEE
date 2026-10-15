# PROVENANCE -- `tools/iop_recompiler/` (byte-identical copies of OLD files)

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
