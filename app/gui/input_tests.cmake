# Isolated compilation of the existing SDL provider and the new host handoff.
# Root's production target already owns platform_input.cpp and the SDL library.
if(NOT TARGET ps2r_platform_sdl)
    set(input_dir "${PS2R_PRODUCT_ROOT}/engine/platform/sdl/input")
    file(READ "${input_dir}/default_bindings.ini" bindings_text)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/input_default_bindings.h"
        "#pragma once\nnamespace ps2r::platform::sdl {\ninline const char* const kDefaultBindingsIni = R\"INPUT(${bindings_text})INPUT\";\n}\n")
    add_library(ps2r_product_input_support STATIC
        ${input_dir}/pad_sample.cpp ${input_dir}/input_mapper.cpp
        ${input_dir}/gamepad_convert.cpp ${input_dir}/keyboard_mouse_backend.cpp
        ${input_dir}/gamepad_backend.cpp ${input_dir}/input_system.cpp
        ${PS2R_PRODUCT_ROOT}/engine/core/threads/park.cpp)
    target_include_directories(ps2r_product_input_support PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_include_directories(ps2r_product_input_support PUBLIC "${PS2R_PRODUCT_ROOT}")
    target_compile_features(ps2r_product_input_support PUBLIC cxx_std_17)
    target_link_libraries(ps2r_product_input_support PUBLIC SDL2::SDL2 Threads::Threads)
    set(product_input_support ps2r_product_input_support)
else()
    set(product_input_support ps2r_platform_sdl)
endif()
target_sources(test_session_controls PRIVATE ../host/platform_input.cpp)
target_link_libraries(test_session_controls PRIVATE ${product_input_support})
target_compile_definitions(test_session_controls PRIVATE PS2R_PRODUCT_INPUT_TESTS=1)
