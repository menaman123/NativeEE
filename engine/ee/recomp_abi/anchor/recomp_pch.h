// recomp_abi/anchor/recomp_pch.h -- the precompiled prologue of the generated EE shard TUs.
//
// Every recompiled_shard_NNN.cpp opens with the same include block (the recompiler's
// write_cpp_includes: the generated header, the eleven "../host_app/<name>.h" ABI
// forwarders, then the standard headers). This header is that block, spelled with the
// same "../host_app/" paths so it resolves through the SAME anchor the shards use, and
// is handed to target_precompile_headers for the shard object library: each shard then
// re-parses nothing of it. The generated header itself (recompiled.h) is the first PCH
// entry on the CMake side -- it lives under games/<title>/recomp/, which this engine
// file must not name.
//
// Nothing here changes what a shard sees: a precompiled header is the parsed form of
// headers the TU includes anyway (every entry has #pragma once), so the object code is
// byte-identical with and without it (tests/recomp RecompPchObjectIdentity pins that).
#pragma once

#include "../host_app/cpu_state.h"
#include "../host_app/memory.h"
#include "../host_app/syscalls.h"
#include "../host_app/sif_hle.h"
#include "../host_app/gs_hle.h"
#include "../host_app/hle_heap.h"
#include "../host_app/ps2_threading.h"
#include "../host_app/cdvd_stream.h"
#include "../host_app/timer_hle.h"
#include "../host_app/vu.h"
#include "../host_app/sif_rpc_handlers.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <fstream>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <set>
#include <map>
#include <functional>
