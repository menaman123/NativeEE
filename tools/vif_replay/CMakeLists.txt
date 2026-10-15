# tools/vif_replay -- the VIF equivalence arms: --unpack-bulk [--vif-fast]
# (the bulk UNPACK lanes vs the scalar reference on 30 synthetic streams);
# --mtvu / --runahead* are parsed and exit 2 until stage 5G. Built at -O2 with
# the same pins as ps2r_vif so the lanes run exactly as the engine ships them.
add_executable(vif_replay
    vif_replay.cpp)
target_include_directories(vif_replay PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(vif_replay PRIVATE -Wall -Wextra -Werror -O2 -fno-strict-aliasing -fwrapv)
# ps2r_vif carries ps2r_vu, ps2r_gif, ps2r_ee, ps2r_core and ps2r_gs_format as
# PUBLIC links; naming them again here makes ld warn about duplicate libraries.
target_link_libraries(vif_replay PRIVATE ps2r_vif ps2r_gs_ir)
