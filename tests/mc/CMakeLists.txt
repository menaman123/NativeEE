# tests/mc -- Phase 4 memory-card suites (PHASE4_PLAN section 2.9 / 4.1). Stage 4E:
# the libmc rows over McCommandModel + McHostStore against a temp card root
# (the port of the original tests/test_libmc.cpp save-flow replay).
add_executable(test_mc_save_flow test_mc_save_flow.cpp)
target_link_libraries(test_mc_save_flow PRIVATE ps2r_mc ps2r_ee)
target_compile_options(test_mc_save_flow PRIVATE -Wall -Wextra -Werror)
add_test(NAME LibMcSaveFlow COMMAND test_mc_save_flow)
set_tests_properties(LibMcSaveFlow PROPERTIES TIMEOUT 60)

# The MCHL savestate section (MchlSection over McCommandModel): registry round
# trip, then the real blobs of the 11 frozen anchors (OLD read-only; SKIP 77 absent).
add_executable(test_mchl_section test_mchl_section.cpp)
target_link_libraries(test_mchl_section PRIVATE ps2r_mc ps2r_ee)
target_compile_options(test_mchl_section PRIVATE -Wall -Wextra -Werror)
add_test(NAME MchlSectionRoundTrip COMMAND test_mchl_section roundtrip)
add_test(NAME MchlSectionAnchors COMMAND test_mchl_section anchors
    "${PS2R_OG_ROOT}/save_states"
    "${CMAKE_SOURCE_DIR}/games/${PS2R_TITLE}/verify/restore_mhash_expected.tsv")
set_tests_properties(MchlSectionRoundTrip PROPERTIES TIMEOUT 60)
set_tests_properties(MchlSectionAnchors PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)

# McCommandModel::Bound derived from the restored client record (DeriveBound):
# MCHL carries no Bound; the mcserv init command still binds a cold boot.
add_executable(test_mc_bound_derivation test_mc_bound_derivation.cpp)
target_link_libraries(test_mc_bound_derivation PRIVATE ps2r_mc ps2r_ee)
target_compile_options(test_mc_bound_derivation PRIVATE -Wall -Wextra -Werror)
add_test(NAME McBoundDerivedFromClientRecord COMMAND test_mc_bound_derivation)
set_tests_properties(McBoundDerivedFromClientRecord PROPERTIES TIMEOUT 60)
