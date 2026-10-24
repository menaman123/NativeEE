# engine/iop/abi/anchor -- the include root for a generic recompiled IOP module TU

`ps2r_iop_recompiler` emits `#include "../host_app/iop_context.h"`, `"../host_app/iop_memory.h"` and
`"../host_app/iop_stubs.h"` (the frozen tool's header names). A title's recompiled-module target adds
this directory as an include root so those paths resolve to `engine/iop/abi/host_app/` -- the generic
ABI (`IopAbiSurface`: memory, `import_call`, `iop_resolve`) that serves every module produced with
`--namespace <id> --import-call`. A title with its own named stub surface (the first title's
`iop/stream_irx/abi/`) keeps its own anchor instead. Nothing else lives here.
