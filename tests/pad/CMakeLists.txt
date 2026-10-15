# tests/pad -- Phase 4 pad suites (PHASE4_PLAN section 2.8 / 4.1). Stage 4E:
# the port of the original host_app/input/test_pad_buffer.cpp protocol half. Stage 4E':
# the InputLog round trip through PadStampDuty (record -> replay -> identical slot images).
add_executable(test_pad_protocol test_pad_protocol.cpp)
target_link_libraries(test_pad_protocol PRIVATE ps2r_pad)
target_compile_options(test_pad_protocol PRIVATE -Wall -Wextra -Werror)
add_test(NAME PadProtocol COMMAND test_pad_protocol)
set_tests_properties(PadProtocol PROPERTIES TIMEOUT 60)

# A port restored READY from a savestate keeps READY when the boundary clock
# restarts at 0 (the anchors' PADS section: opened_at 0, stage READY).
add_executable(test_pad_protocol_restore test_pad_protocol_restore.cpp)
target_link_libraries(test_pad_protocol_restore PRIVATE ps2r_pad)
target_compile_options(test_pad_protocol_restore PRIVATE -Wall -Wextra -Werror)
add_test(NAME PadProtocolRestore COMMAND test_pad_protocol_restore)
set_tests_properties(PadProtocolRestore PROPERTIES TIMEOUT 60)

add_executable(test_pad_round_trip test_pad_round_trip.cpp)
target_link_libraries(test_pad_round_trip PRIVATE ps2r_pad)
target_compile_options(test_pad_round_trip PRIVATE -Wall -Wextra -Werror)
add_test(NAME PadRoundTrip COMMAND test_pad_round_trip)
set_tests_properties(PadRoundTrip PROPERTIES TIMEOUT 60)

add_executable(test_libpad_bindings test_libpad_bindings.cpp)
target_link_libraries(test_libpad_bindings PRIVATE ps2r_pad)
target_compile_options(test_libpad_bindings PRIVATE -Wall -Wextra -Werror)
add_test(NAME LibpadBindings COMMAND test_libpad_bindings)
set_tests_properties(LibpadBindings PROPERTIES TIMEOUT 60)

# The PADS savestate section (PadsSection over PadProtocol): registry round
# trip, then the real blobs of the 11 frozen anchors (OLD read-only; SKIP 77 absent).
add_executable(test_pads_section test_pads_section.cpp)
target_link_libraries(test_pads_section PRIVATE ps2r_pad)
target_compile_options(test_pads_section PRIVATE -Wall -Wextra -Werror)
add_test(NAME PadsSectionRoundTrip COMMAND test_pads_section roundtrip)
add_test(NAME PadsSectionAnchors COMMAND test_pads_section anchors
    "${PS2R_OG_ROOT}/save_states"
    "${PS2R_GAMES_DIR}/${PS2R_TITLE}/verify/restore_mhash_expected.tsv")
set_tests_properties(PadsSectionRoundTrip PROPERTIES TIMEOUT 60)
set_tests_properties(PadsSectionAnchors PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
