# tests/app -- process-level pins of the composition root (app/host). Each test
# RUNS the built nativeee binary the way the harness does (verify/run_leg.py),
# bounded through verify/run_bounded.sh, and asserts on the leg's run.meta; so
# a seam the root must wire is proven by the process, not by a self-report.
# SKIP 77 when the title files (PS2R_OG_ROOT/game_files) are absent.
#
# TWO BINARIES (M12, TRACK 3b D2/D3; docs/design/M12_DELETION_LEDGER.md section 3):
#   nativeee         the PLAY binary, service-only -- no IOP kernel / STREAM.IRX /
#                     libsd / SIF-DMA model linked; hle.startup has the one arm
#                     (native) and `--set <key>=off` of a native-startup consumer
#                     REFUSES (the twelve degrade edges never boot an IOP here).
#   nativeee_oracle  the ORACLE binary -- the same sources + ps2r_iop_models; the
#                     deterministic profile's `hle.startup=retained` oracle arm.
# A Root pin runs on the ORACLE binary when it (a) loads a FROZEN anchor (the
# retained-era OLD save_states / probe images), (b) asks for a retained arm
# (PS2R_RETAINED_*_ARM, `--set hle.stream_files=off`, ... -- one of the twelve keys),
# (c) asserts IOP rows, or (d) compares lanes between binaries (the Release lane
# guard). Every other pin runs on the PLAY binary -- the one the user plays.

# --- the Root process pins run the built binaries: registered only when the title is being built
# (a title ELF -> nativeee; a disc image / STREAM.IRX -> nativeee_oracle too; cmake/TitleInputs.cmake).
# $<TARGET_FILE:...> of an absent target is a generate-time error, so the whole group is gated on both.
# The Root* pins drove the PLAY binary as a process against ONE title's frozen anchors,
# savestates and pad logs -- they needed nativeee_oracle and that title's private fixtures.
# A title module brings its own process pins.

# VideoStackDefersOffMainResize: the VideoStack contract for a window resize posted from a
# non-main thread (the GS consumer) -- recorded, never a synchronous call into the main
# thread, applied by the frame boundary through the session dispatch. Pins the pipelined
# relaunch deadlock (main in DrawIrThreadQueue::Sync() for the consumer; the consumer in
# OnWindowResizedOffMain -> SessionRuntime::OnMain for main). Opens a visible window (the
# deferral path is the visible window's); SKIP 77 without a window / GL context.

# DiscVerifyVectors (CHARTER 2 C2, Bernoulli): the disc verifier's accept / reject vectors
# (plain .iso, raw .bin, not ISO9660, no title, PAL / NTSC-J / other-title serials, wrong ELF
# hash, truncated image) and the FirstRun pipeline (raw -> plain copy, ELF byte-equal,
# memcards, settings, stamp LAST; refused leaves nothing; cancel removes the .part) over
# synthetic ISO9660 images from tests/lib -- never the user's disc.
add_executable(test_disc_verify test_disc_verify.cpp
    "${CMAKE_SOURCE_DIR}/app/host/disc_verify.cpp"
    "${CMAKE_SOURCE_DIR}/app/host/first_run.cpp"
    "${CMAKE_SOURCE_DIR}/app/host/game_files_stamp.cpp"
    "${CMAKE_SOURCE_DIR}/app/host/user_data.cpp")
target_include_directories(test_disc_verify PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_disc_verify PRIVATE ps2r_cdvd ps2r_core ps2r_title_hle ps2r_iso9660_image ps2r_platform_sdl)
target_compile_options(test_disc_verify PRIVATE -Wall -Wextra -Werror)
add_test(NAME DiscVerifyVectors COMMAND test_disc_verify "${CMAKE_BINARY_DIR}/test_scratch/disc_verify")
set_tests_properties(DiscVerifyVectors PROPERTIES TIMEOUT 120)

# ControlsBindingsPressToBind (CHARTER 2 A5, Bernoulli): the launcher Controls page's model under
# SYNTHETIC controller / key events (the launcher's own event decode, no window, no pad): the
# gamepad-button remap lands in bindings.ini, the real InputMapper accepts the file and
# GamepadConvert presses the remapped PS2 button; a control moves off the row that had it.
add_executable(test_controls_bindings test_controls_bindings.cpp
    "${CMAKE_SOURCE_DIR}/app/host/controls_bindings.cpp")
target_include_directories(test_controls_bindings PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(test_controls_bindings PRIVATE ps2r_platform_sdl)
target_compile_options(test_controls_bindings PRIVATE -Wall -Wextra -Werror)
add_test(NAME ControlsBindingsPressToBind COMMAND test_controls_bindings "${CMAKE_BINARY_DIR}/test_scratch/controls_bindings")

# DisplaySequenceSchedule (Charter 3 item 4, Bernoulli): harness.display_sequence's pure
# parser / schedule -- mode names to the Display page's requests, malformed items skipped
# and counted, each step submitted once at the first boundary >= its frame, the first
# submission's boundary and pad reading kept, the census row rendered (incl. the s8 red).
add_executable(test_display_sequence test_display_sequence.cpp
    "${CMAKE_SOURCE_DIR}/app/host/display_sequence.cpp")
target_include_directories(test_display_sequence PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(test_display_sequence PRIVATE -Wall -Wextra -Werror)
add_test(NAME DisplaySequenceSchedule COMMAND test_display_sequence)
set_tests_properties(ControlsBindingsPressToBind PROPERTIES TIMEOUT 60)

# RootFirstRunExtract (CHARTER 2 C2, Bernoulli): the first-run extraction on the actual play binary
# over a SYNTHETIC raw .bin (make_synthetic_iso) carrying a fake SLUS_123.45 whose SHA-256 the test
# injects (`--set launcher.elf_sha256=`); `--extract-disc` lands the plain .iso copy, the ELF, the
# memcards, ps2r.ini and the BUILD-ID stamp in a temp user data root; three refusals (the build's
# own hash, PAL, noise) leave nothing. Never the user's disc. No game process (no RUN_SERIAL need).
if(TARGET nativeee)   # the play binary exists only with a title ELF (TitleInputs)
add_test(NAME RootFirstRunExtract
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/app/root_first_run_extract.py
            --binary $<TARGET_FILE:nativeee> --make-iso $<TARGET_FILE:make_synthetic_iso> --timeout 120)
set_tests_properties(RootFirstRunExtract PROPERTIES TIMEOUT 300 SKIP_RETURN_CODE 77)
endif()
add_test(NAME RootFirstRunExtractSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/app/root_first_run_extract.py --selftest)
set_tests_properties(RootFirstRunExtractSelftest PROPERTIES TIMEOUT 60)

# RootLauncherScreens (CHARTER 2 C2): the launcher GUI opens a window and presents its first-run and
# settings screens (--launcher-screenshot frames judged non-blank); a harness command line bypasses
# it. No game process; SKIP 77 without a display. RUN_SERIAL: it owns a window.
if(TARGET nativeee)
add_test(NAME RootLauncherScreens
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/app/root_launcher_screens.py
            --binary $<TARGET_FILE:nativeee> --make-iso $<TARGET_FILE:make_synthetic_iso> --timeout 180
            --game-files ${PS2R_OG_ROOT}/game_files --runner ${CMAKE_SOURCE_DIR}/verify/run_bounded.sh)
set_tests_properties(RootLauncherScreens PROPERTIES TIMEOUT 400 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
endif()
add_test(NAME RootLauncherScreensSelftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/app/root_launcher_screens.py --selftest)
set_tests_properties(RootLauncherScreensSelftest PROPERTIES TIMEOUT 60)

# Each process fixture owns the title runtime and its GL/audio resources.
# Keep game legs serial even when independent unit tests run concurrently.
get_property(ps2r_root_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${ps2r_root_tests} PROPERTIES RESOURCE_LOCK ps2r_game_process)
