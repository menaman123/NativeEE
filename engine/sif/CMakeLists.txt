# engine/sif -- the EE<->IOP interface (PHASE4_PLAN section 2.4). Stage 4C: the
# SIF register block on the MmioRouter (SifState), the RPC router, the sceSif
# HLE rows -- the EE-side surface the play binary keeps (sif_rows live:
# sceSifCallRpc / sceSifBindRpc route to the owned servers).
add_library(ps2r_sif STATIC
    sif_state.cpp
    hle/rpc_router.cpp
    hle/sce_sif.cpp
)
target_include_directories(ps2r_sif PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_sif PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_sif PUBLIC ps2r_ee)

# The SIF DMA model (M12 L5, test-only): the MainMemory-backed EE RAM sink of
# IOP->EE SIF DMA and the EE-side RPC server of IOP->EE calls (boundary slot 6)
# over the IOP kernel's SifRpcBridge. Part of ps2r_iop_models; the play binary
# links neither (its RPC traffic is the owned StreamTransport's).
add_library(ps2r_sif_iop STATIC
    hle/main_memory_ee_ram_sink.cpp
    hle/ee_rpc_server.cpp
)
target_include_directories(ps2r_sif_iop PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_sif_iop PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_sif_iop PUBLIC ps2r_sif ps2r_iop)
