// app/host/iop_arm.h -- the ONE seam between the play binary and the oracle binary
// (M12, TRACK 3b D2/D3; docs/design/M12_DELETION_LEDGER.md section 3).
//
//   PS2R_IOP_ORACLE=1  nativeee_oracle: the retained IOP arm is compiled in --
//                      the cooperative IOP kernel + stock modules, the recompiled
//                      STREAM.IRX and its plugin, the libsd model, the SIF DMA model
//                      (ps2r_iop_models). hle.startup=retained is selectable and
//                      the deterministic profile's oracles boot the IOP.
//   PS2R_IOP_ORACLE=0  nativeee (the PLAY binary): none of it is linked. The IOP
//                      never exists; hle.startup has the one arm (native); a
//                      composition whose only fallback was the IOP REFUSES with the
//                      key named (ConfigLoader::SetRetainedStartupAvailable(false)).
//
// Every `#if PS2R_IOP_ORACLE` in app/host is a use of this seam; nothing else in
// the tree tests the macro. The census row `iop_linked=` publishes the value.
#pragma once

#ifndef PS2R_IOP_ORACLE
#error "PS2R_IOP_ORACLE must be defined by the host binary's CMake target (0 = play, 1 = oracle)"
#endif

#include <cstdint>

namespace ps2r::app {
constexpr bool kIopLinked = PS2R_IOP_ORACLE != 0;
// The STREAM RPC server id the EE's sceSifBindRpc names (0x12345): the retained
// module registers it (ModuleLayout::kRpcServerId, oracle binary -- the
// static_assert in ee_bootstrap_services.cpp pins the two equal) and the owned
// StreamTransport answers it in both binaries.
constexpr uint32_t kStreamRpcServerId = 0x12345u;
}  // namespace ps2r::app
