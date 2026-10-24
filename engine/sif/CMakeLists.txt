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

# The SIF DMA model (M12 L5): the MainMemory-backed EE RAM sink/source of
# IOP<->EE SIF DMA, the EE-side RPC server of IOP->EE calls (boundary slot 6)
# over the IOP kernel's SifRpcBridge, and the generic EE-facing server of a
# hosted module's sceSifRegisterRpc record (IopRpcServer). Part of ps2r_iop_models;
# a title whose IOP modules are recompiled links it from its play composition.
add_library(ps2r_sif_iop STATIC
    hle/main_memory_ee_ram_sink.cpp
    hle/main_memory_ee_ram_source.cpp
    hle/ee_rpc_server.cpp
    hle/iop_rpc_server.cpp
)
target_include_directories(ps2r_sif_iop PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_sif_iop PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_sif_iop PUBLIC ps2r_sif ps2r_iop)
