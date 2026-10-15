# engine/iop -- the IOP side of the runtime (PHASE4_PLAN section 2.1-2.2).
# Stage 4A: the cooperative kernel core (IopKernel over IopCoroutine and
# IopClock), IOP RAM, the R3000 register file and the sysmem heap.
# Stage 4A': the REALTIME host thread, the SIF RPC bridge, the module table
# (FRAME pump points) and the IOPM savestate section.
# Stage 4B/4B': the IRX loader and the kernel-facing HLE modules.
add_library(ps2r_iop STATIC
    iop_clock.cpp
    iop_coroutine.cpp
    iop_kernel.cpp
    iop_memory.cpp
    iop_context.cpp
    sysmem_heap.cpp
    iop_host_thread.cpp
    sif_rpc_bridge.cpp
    iop_module_table.cpp
    savestate/iopm_section.cpp
    hle/sysmem/sysmem_module.cpp
    hle/intrman/intrman_module.cpp
    hle/stdio/stdio_module.cpp
    hle/sysclib/sysclib_module.cpp
    iop_loader.cpp
    hle/thbase/thbase_module.cpp
    hle/thsemap/thsemap_module.cpp
    hle/sifcmd/sifcmd_module.cpp
    hle/sifman/sifman_module.cpp
    hle/timrman/timrman_module.cpp
    hle/ioman/ioman_module.cpp
    hle/libsd/libsd_module.cpp
    hle/cdvdman/cdvdman_module.cpp)
target_include_directories(ps2r_iop PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_iop PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_iop PUBLIC ps2r_core ps2r_platform ps2r_sound)

# ps2r_iop_models -- M12 (TRACK 3b D2/D3, then ledger L5/L7): the retained models
# as ONE test-only aggregate: the IOP kernel + stock modules + the libsd model
# (ps2r_iop), the recompiled STREAM.IRX + its plugin / LTI protocol / STREAM RPC
# server (ps2r_title_stream_irx), the SIF DMA model + the SIF register MMIO window
# (ps2r_sif_iop, L5) and the IPU command/DMA model (ps2r_ipu_model, L7). Linked by the
# oracle binary (nativeee_oracle, PS2R_IOP_ORACLE=1) and the component oracles
# under tests/; the play binary (nativeee) links none of it -- proven by
# tests/lint (no IOP symbol in the play binary) and the census row iop_linked=0.
add_library(ps2r_iop_models INTERFACE)
target_link_libraries(ps2r_iop_models INTERFACE ps2r_iop ps2r_sif_iop ps2r_title_stream_irx ps2r_ipu_model)
target_compile_definitions(ps2r_iop_models INTERFACE PS2R_IOP_ORACLE=1)
