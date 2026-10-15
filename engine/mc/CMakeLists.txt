# engine/mc -- the memory card (PHASE4_PLAN section 2.9). Stage 4E: the host
# directory store (McHostStore), the mcserv command state machine the libmc rows
# drive (McCommandModel), the libmc SDK rows (LibmcBindings) and the mcserv
# RPC face the recompiled sceMcInit binds (McservModule). savestate/: the MCHL
# section codec (MchlSection) over McCommandModel.
add_library(ps2r_mc STATIC
    hle/mc_host_store.cpp
    hle/mc_command_model.cpp
    hle/libmc_bindings.cpp
    hle/mcserv_module.cpp
    savestate/mchl_section.cpp
)
target_include_directories(ps2r_mc PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_mc PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_mc PUBLIC ps2r_ee ps2r_sif)
