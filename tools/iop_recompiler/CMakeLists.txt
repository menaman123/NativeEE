# tools/iop_recompiler -- the IOP recompiler (ps2r_iop_recompiler): STREAM.IRX -> iop_recompiled.{h,cpp}.
# Byte-identical copies of OLD's iop_recompiler/ sources (PROVENANCE.md); links the rabbitizer and
# elfio targets tools/recompiler_v2 defines, so it is added after that directory. Transplanted code is
# compiled WITHOUT -Werror.
if(NOT TARGET ps2r_tp_rabbitizer OR NOT TARGET ps2r_tp_elfio)
    message(FATAL_ERROR "tools/iop_recompiler needs tools/recompiler_v2 (ps2r_tp_rabbitizer, ps2r_tp_elfio) added first")
endif()
add_executable(ps2r_iop_recompiler main.cpp iop_recompiler.cpp)
target_include_directories(ps2r_iop_recompiler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ps2r_iop_recompiler PRIVATE ps2r_tp_rabbitizer ps2r_tp_elfio)
target_compile_features(ps2r_iop_recompiler PRIVATE cxx_std_17)
