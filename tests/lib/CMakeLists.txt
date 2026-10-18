# tests/lib -- support libraries shared by ctest suites and repo tools (never linked
# by the engine or the host binary).

# ps2r_private_fixtures: PrivateFixtures -- the C++ face of the private game-derived fixture
# contract (cmake/Ps2rPrivateFixtures.cmake): $NATIVEEE_PRIVATE_FIXTURES/<repo-relative path>,
# else the source tree; absent -> the test exits 77 (SKIP). Pure C++ (no engine dependency).
add_library(ps2r_private_fixtures STATIC private_fixtures.cpp)
target_include_directories(ps2r_private_fixtures PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_definitions(ps2r_private_fixtures PRIVATE PS2R_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_options(ps2r_private_fixtures PRIVATE -Wall -Wextra -Werror)
add_test(NAME PrivateFixtureRunnerSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/private_fixture_runner.py --selftest)
add_test(NAME PrivateFixturesPySelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/private_fixtures.py --selftest)
set_tests_properties(PrivateFixtureRunnerSelftest PrivateFixturesPySelftest PROPERTIES TIMEOUT 60)

# ps2r_gsdl_reader: the .gsdl recording loader (GsdlReader) over engine/gs/hle/ir.
add_library(ps2r_gsdl_reader STATIC gsdl_reader.cpp)
target_include_directories(ps2r_gsdl_reader PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_gsdl_reader PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_gsdl_reader PUBLIC ps2r_gs_ir)

# ps2r_fake_backend: the CPU render::Backend for GL-free tests above the interface (Phase 6).
add_library(ps2r_fake_backend STATIC fake_backend.cpp)
target_include_directories(ps2r_fake_backend PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_fake_backend PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_fake_backend PUBLIC ps2r_render ps2r_gs_ir ps2r_gs_format)

# ps2r_iso9660_image: the synthetic ISO9660 writer (plain 2048 / raw 2352) the
# disc-verification vectors and RootFirstRunExtract build their fake disc with;
# make_synthetic_iso is its CLI for the Python Root pins.
add_library(ps2r_iso9660_image STATIC iso9660_image.cpp)
target_include_directories(ps2r_iso9660_image PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_iso9660_image PRIVATE -Wall -Wextra -Werror)
add_executable(make_synthetic_iso make_synthetic_iso.cpp)
target_link_libraries(make_synthetic_iso PRIVATE ps2r_iso9660_image)
target_compile_options(make_synthetic_iso PRIVATE -Wall -Wextra -Werror)
