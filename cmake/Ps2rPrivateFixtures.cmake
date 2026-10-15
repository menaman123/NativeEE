# cmake/Ps2rPrivateFixtures.cmake -- the private game-derived fixture contract.
#
# The public tree carries NO game bytes: RAM savestate probes (*.ps2st), PCSX2 GS dumps
# (*.gs.zst, *.gsf1), console screenshots (*_shot.ppm, *_pcsx2shot.png), texture atlas pages /
# vertex records lifted from the dumps (*.bin), the retail VU0 microprogram captures
# (tests/vu/fixtures/*.bin) and the title's .data-section word dump (data/ram_data_map_v50.txt)
# were removed (tools/ci/asset_scan.py --tree . is the gate that keeps them out).
#
# Tests that need one of those files read the environment variable
#
#     NATIVEEE_PRIVATE_FIXTURES=<dir>
#
# which mirrors the ORIGINAL repo-relative layout: the probe that was
# games/<title>/verify/baseline/coldboot/probe/old_b120.ps2st is expected at
# $NATIVEEE_PRIVATE_FIXTURES/games/<title>/verify/baseline/coldboot/probe/old_b120.ps2st. When the
# variable is unset, or the file is absent, the test SKIPs (ctest SKIP_RETURN_CODE 77) -- never fails.
#
# Three faces of one contract:
#   ps2r_private_fixture_path(<out> <rel>)      configure-time resolution for compile definitions:
#                                               $ENV{NATIVEEE_PRIVATE_FIXTURES}/<rel> when the variable
#                                               is set at configure time, else ${CMAKE_SOURCE_DIR}/<rel>
#                                               (a developer who drops the files back in place -- they
#                                               are .gitignored -- gets them without the variable).
#   ps2r_add_private_fixture_test(NAME <n> FIXTURES <rel>... COMMAND <cmd> <args>...)
#                                               a ctest row run through tests/lib/private_fixture_runner.py:
#                                               at RUN time the runner resolves the root (variable, else
#                                               the source root), exits 77 unless every FIXTURES entry
#                                               exists, substitutes the literal token {PRIVATE_FIXTURES}
#                                               in the command with the root and forwards the exit code.
#                                               Extra set_tests_properties() apply as usual.
#   ps2r_private_fixtures (library target)      tests/lib/private_fixtures.{h,cpp}: PrivateFixtures::Path()
#                                               / Exists() / Skip() for C++ tests that open fixtures
#                                               themselves (the VU0 captures, the title data map);
#                                               tests/lib/private_fixtures.py is the Python twin.
set(PS2R_PRIVATE_FIXTURES_ENV "NATIVEEE_PRIVATE_FIXTURES")
set(PS2R_PRIVATE_FIXTURES_SKIP_CODE 77)
set(PS2R_PRIVATE_FIXTURE_RUNNER "${CMAKE_SOURCE_DIR}/tests/lib/private_fixture_runner.py")

function(ps2r_private_fixture_path out_var rel)
    if(DEFINED ENV{${PS2R_PRIVATE_FIXTURES_ENV}} AND NOT "$ENV{${PS2R_PRIVATE_FIXTURES_ENV}}" STREQUAL "")
        set(${out_var} "$ENV{${PS2R_PRIVATE_FIXTURES_ENV}}/${rel}" PARENT_SCOPE)
    else()
        set(${out_var} "${CMAKE_SOURCE_DIR}/${rel}" PARENT_SCOPE)
    endif()
endfunction()

function(ps2r_add_private_fixture_test)
    cmake_parse_arguments(_pf "" "NAME" "FIXTURES;COMMAND" ${ARGN})
    if(NOT _pf_NAME OR NOT _pf_COMMAND)
        message(FATAL_ERROR "ps2r_add_private_fixture_test: NAME and COMMAND are required")
    endif()
    set(_runner_args --source-root "${CMAKE_SOURCE_DIR}")
    foreach(_rel IN LISTS _pf_FIXTURES)
        list(APPEND _runner_args --fixture "${_rel}")
    endforeach()
    # add_test() resolves a target name only as the FIRST word of COMMAND; behind the runner it
    # must be spelled as the target's file.
    list(GET _pf_COMMAND 0 _pf_first)
    if(TARGET "${_pf_first}")
        list(REMOVE_AT _pf_COMMAND 0)
        list(INSERT _pf_COMMAND 0 "$<TARGET_FILE:${_pf_first}>")
    endif()
    add_test(NAME ${_pf_NAME}
        COMMAND ${Python3_EXECUTABLE} "${PS2R_PRIVATE_FIXTURE_RUNNER}" ${_runner_args} -- ${_pf_COMMAND})
    set_tests_properties(${_pf_NAME} PROPERTIES SKIP_RETURN_CODE ${PS2R_PRIVATE_FIXTURES_SKIP_CODE})
endfunction()
