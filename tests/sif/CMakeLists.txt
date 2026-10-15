# tests/sif -- Phase 4 SIF suites (PHASE4_PLAN section 4.1). Stage 4C: the SIF
# register block's semantics on the router (SifState) and the RPC router (bind table,
# routing, end-callback delivery order).
add_executable(test_sif_state test_sif_state.cpp)
target_link_libraries(test_sif_state PRIVATE ps2r_sif)
target_compile_options(test_sif_state PRIVATE -Wall -Wextra -Werror)
add_test(NAME SifState COMMAND test_sif_state)
add_executable(test_rpc_router test_rpc_router.cpp)
target_link_libraries(test_rpc_router PRIVATE ps2r_sif ps2r_ee)
target_compile_options(test_rpc_router PRIVATE -Wall -Wextra -Werror)
add_test(NAME SifRpcRouter COMMAND test_rpc_router)
# Stage 4C''': the SIF flag words' single source of truth (the sceSifSetReg/GetReg
# syscalls read and write the SifState word; SceSif::InitRpc's IOP-side reply is
# visible to the syscall poll).
add_executable(test_sif_flag_source test_sif_flag_source.cpp)
target_link_libraries(test_sif_flag_source PRIVATE ps2r_sif ps2r_title_hle ps2r_ee)
target_compile_options(test_sif_flag_source PRIVATE -Wall -Wextra -Werror)
add_test(NAME SifFlagSingleSource COMMAND test_sif_flag_source)
set_tests_properties(SifFlagSingleSource PROPERTIES TIMEOUT 60)
# SIF DMA both directions, byte-exact, with the sceSifSetDma return rule.
add_executable(test_sce_sif_dma test_sce_sif_dma.cpp)
target_link_libraries(test_sce_sif_dma PRIVATE ps2r_sif_iop ps2r_title_hle ps2r_ee)
target_compile_options(test_sce_sif_dma PRIVATE -Wall -Wextra -Werror)
add_test(NAME SceSifDma COMMAND test_sce_sif_dma)
set_tests_properties(SceSifDma PROPERTIES TIMEOUT 60)
# The IOP -> EE RPC round trip through EeRpcServer over the kernel's bridge (FRAME).
add_executable(test_ee_rpc_server test_ee_rpc_server.cpp)
target_link_libraries(test_ee_rpc_server PRIVATE ps2r_sif_iop ps2r_title_hle ps2r_ee)
target_compile_options(test_ee_rpc_server PRIVATE -Wall -Wextra -Werror)
add_test(NAME EeRpcServer COMMAND test_ee_rpc_server)
set_tests_properties(EeRpcServer PROPERTIES TIMEOUT 120)
# A blocking sceSifCallRpc's end function runs before the call returns (libkernl
# 3.0.1); NOWAIT waits for the completion duty; no runner bound = the duty.
add_executable(test_sce_sif_blocking_end test_sce_sif_blocking_end.cpp)
target_link_libraries(test_sce_sif_blocking_end PRIVATE ps2r_sif ps2r_title_hle ps2r_ee)
target_compile_options(test_sce_sif_blocking_end PRIVATE -Wall -Wextra -Werror)
add_test(NAME SceSifBlockingEndFunction COMMAND test_sce_sif_blocking_end)
set_tests_properties(SceSifBlockingEndFunction PROPERTIES TIMEOUT 60)
