# tools/vu1_aot_pack -- the PRE-BUILT VU1 library pack builder: emit + compile + load +
# re-verify every image of a corpus (Vu1AotBuilder, the same pipeline the game runs
# at first run) and write a pack (app/host/vu1_aot_pack.h: pack.txt + one library per
# image, keyed by the engine's VU1 AOT ABI key) that a nativeee built from the same VU
# sources loads from next to its executable -- a player without a compiler gets the
# native tier from the first frame. Links the builder's two app/host sources directly
# (like tests/app/test_vu1_aot_native): no window, no title.
add_executable(vu1_aot_pack vu1_aot_pack.cpp
    "${CMAKE_SOURCE_DIR}/app/host/vu1_aot_builder.cpp"
    "${CMAKE_SOURCE_DIR}/app/host/vu1_aot_pack.cpp")
add_dependencies(vu1_aot_pack vu1_aot_prelude_stage)
target_include_directories(vu1_aot_pack PRIVATE ${CMAKE_SOURCE_DIR} ${PS2R_GENERATED_DIR})
target_compile_options(vu1_aot_pack PRIVATE -Wall -Wextra -Werror)
target_link_libraries(vu1_aot_pack PRIVATE ps2r_vu)
