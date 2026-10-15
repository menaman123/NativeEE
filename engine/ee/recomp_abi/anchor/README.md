# recomp_abi/anchor — include anchor for the generated code

The frozen recompiler emits `#include "../host_app/<name>.h"` (11 headers, `recompiled.cpp:1-20`).
The build adds `-I engine/ee/recomp_abi/anchor`, so `anchor/../host_app/<name>.h` resolves to
`engine/ee/recomp_abi/host_app/<name>.h`. No directory named `host_app` exists beside
`games/<title>/recomp/`, and nothing under `recomp/` is edited. This directory holds only this file.
