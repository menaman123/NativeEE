# tests/cdvd -- Phase 4 disc suites (PHASE4_PLAN section 2.5 / 4.1). Stage 4D:
# the ISO9660 walker + read engine over the frozen disc image (registered only
# when CMake finds the image under PS2R_OG_ROOT -- never a red tree without it),
# and the libcdvd SDK rows (the fixed RTC; ISO-backed cases when the image is present).
# Stage 4D': the sceCdSt* stream ring (CdvdStreamReader; the ring round trip when the
# image is present) and the completion-visibility meter (DeliveryMeter).
set(PS2R_DISC_IMAGE "${PS2R_OG_ROOT}/game_files/example_title.rep/the title.iso")
set(PS2R_OG_GAME_FILES "${PS2R_OG_ROOT}/game_files")

add_executable(test_iso_fs test_iso_fs.cpp)
target_link_libraries(test_iso_fs PRIVATE ps2r_cdvd)
target_compile_options(test_iso_fs PRIVATE -Wall -Wextra -Werror)

# IsoFsSynthetic: both sector layouts (plain 2048 / raw 2352) over a synthetic
# volume -- registered unconditionally (no disc image involved).
add_executable(test_iso_fs_synthetic test_iso_fs_synthetic.cpp)
target_link_libraries(test_iso_fs_synthetic PRIVATE ps2r_cdvd ps2r_iso9660_image)
target_compile_options(test_iso_fs_synthetic PRIVATE -Wall -Wextra -Werror)
add_test(NAME IsoFsSynthetic COMMAND test_iso_fs_synthetic "${CMAKE_BINARY_DIR}/test_scratch/cdvd")
set_tests_properties(IsoFsSynthetic PROPERTIES TIMEOUT 60)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/test_scratch/cdvd")

add_executable(test_stream_reader test_stream_reader.cpp)
target_link_libraries(test_stream_reader PRIVATE ps2r_cdvd)
target_compile_options(test_stream_reader PRIVATE -Wall -Wextra -Werror)

add_executable(test_delivery_meter test_delivery_meter.cpp)
target_link_libraries(test_delivery_meter PRIVATE ps2r_cdvd)
target_compile_options(test_delivery_meter PRIVATE -Wall -Wextra -Werror)
add_test(NAME DeliveryMeter COMMAND test_delivery_meter)
set_tests_properties(DeliveryMeter PROPERTIES TIMEOUT 60)

add_executable(test_cdvdman_module test_cdvdman_module.cpp)
target_link_libraries(test_cdvdman_module PRIVATE ps2r_cdvd ps2r_iop)
target_compile_options(test_cdvdman_module PRIVATE -Wall -Wextra -Werror)

add_executable(test_libcdvd_bindings test_libcdvd_bindings.cpp)
target_link_libraries(test_libcdvd_bindings PRIVATE ps2r_cdvd ps2r_title_hle ps2r_ee)
target_compile_options(test_libcdvd_bindings PRIVATE -Wall -Wextra -Werror)

if(EXISTS "${PS2R_DISC_IMAGE}")
    add_test(NAME IsoFsAndReadEngine COMMAND test_iso_fs "${PS2R_DISC_IMAGE}" "${PS2R_OG_GAME_FILES}")
    set_tests_properties(IsoFsAndReadEngine PROPERTIES TIMEOUT 120)
    add_test(NAME LibcdvdBindings COMMAND test_libcdvd_bindings "${PS2R_DISC_IMAGE}")
    add_test(NAME CdvdStreamReader COMMAND test_stream_reader "${PS2R_DISC_IMAGE}")
    add_test(NAME CdvdmanModuleDisc COMMAND test_cdvdman_module "${PS2R_DISC_IMAGE}")
else()
    message(STATUS "tests/cdvd: disc image not found at ${PS2R_DISC_IMAGE}; IsoFsAndReadEngine not registered")
    add_test(NAME LibcdvdBindings COMMAND test_libcdvd_bindings)
    add_test(NAME CdvdStreamReader COMMAND test_stream_reader)
    add_test(NAME CdvdmanModuleDisc COMMAND test_cdvdman_module)
endif()
set_tests_properties(LibcdvdBindings CdvdStreamReader CdvdmanModuleDisc PROPERTIES TIMEOUT 60)

# Stage 4D'': the delivery budget (the original's nine accounting cases), the loader
# completion-visibility model as the meter's first producer, the slot-4 duty order
# (4E': CD delivery -> RPC end-callbacks -> pad stamp).
add_executable(test_loader_model test_loader_model.cpp)
target_link_libraries(test_loader_model PRIVATE ps2r_cdvd ps2r_sif ps2r_ee ps2r_pad)
target_compile_options(test_loader_model PRIVATE -Wall -Wextra -Werror)
add_test(NAME LoaderModelAndDeliveryBudget COMMAND test_loader_model)
set_tests_properties(LoaderModelAndDeliveryBudget PROPERTIES TIMEOUT 60)

# Stage 4F''': the title's five loader file-IO bodies over the real disc (byte-exact,
# delivery at the boundary, the blocking wait).
if(EXISTS "${PS2R_DISC_IMAGE}")
else()
endif()

# Stage I4.10-A: the title's own completion predicate (the REAL recompiled FUN_002adaf0 ->
# FUN_001e3308 STREAM slot status) over a host-served async read: pending from the issue
# until the boundary delivers bytes + ring entry, done only after (BUG-041). Skips 77 without the image.
# (Title tests: these link the GENERATED recompiled code -- registered only with a title ELF, TitleInputs.)
if(TARGET ps2r_title_recomp)
if(EXISTS "${PS2R_DISC_IMAGE}")
else()
endif()

# The retail window routing by size against the callback ring's capacity (FUN_002aac80): an
# oversized async window is DMAed straight to dest with no staged completion copy, so the REAL
# FUN_002ad110 leaves dest = file bytes and the ring/cursor untouched; a window below the
# capacity goes through the ring and the completion copy advances the cursor (BUG-042).
if(EXISTS "${PS2R_DISC_IMAGE}")
else()
endif()
else()
    message(STATUS "tests/cdvd: LoaderCompletionOrder / LoaderCompletionOversized SKIPPED (no title ELF)")
endif()

# The ring rule of the title's windowed reader (FUN_002aac80, loader.ring_cache): the coldboot
# request list replayed over the disc image yields one read record per fill boundary (19, not 906),
# the console's Default.rm2 fill chain in fs+0xc, the console's staged words, and -- with the saved
# console images -- the console's ring bytes after the +0x1ddef8 fill and after the EOF fill. The
# same binary with `--ring-cache off` is the red side (906 records, fs+0xc never set).
if(EXISTS "${PS2R_DISC_IMAGE}")
else()
endif()

# The load-wait loop lift (LoaderSpinLift over LoaderModel::ShouldYieldPollSlice at both load-poll sites
# of the headroom query): fires only under both guards with the read still in flight; a read
# that can land in-slice (already delivered) or no in-flight read never lifts. No disc needed.

# The title's chunk state machine as the load classifier (ChunkStateCensus over StepStates /
# StepChunkData): loader steps ending Loading/Unloading, chunk steps in Releasing/Released and a
# chunk's FIRST Shown step (the level-entry CUT) are TickProfile chunk events; the originals run
# unchanged, an interior re-entry is not a step, nothing is written to guest memory. No disc needed.

# A STREAM record the title stages ITSELF (the REAL FUN_002ad9a8 -> FUN_001e69a8, type 0x7e) served
# by the REAL recompiled STREAM.IRX under the cooperative IOP kernel: the rig follows the whole-file
# loader FUN_002ab160 (the REAL FUN_002ad650 opens the slot's ring stream -- STREAM cmd 13 -- before
# the file is registered, cmd 31) and the blocking wait yields to the kernel through Deps::pump_module
# (bound as the root binds it) until the REAL FUN_002adaf0 answers done -- bytes through sifman's SIF
# DMA, slot byte retired by the client's end-callback (BUG-043; the module's post-DMA poll needs
# sceSifDmaStat < 0). Skips 77 without the image.
set(PS2R_TITLE_HLE_DIR "${PS2R_GAMES_DIR}/${PS2R_TITLE}/hle")
if(TARGET ps2r_title_recomp AND TARGET ps2r_title_stream_irx)
if(EXISTS "${PS2R_DISC_IMAGE}")
else()
endif()
else()
    message(STATUS "tests/cdvd: LoaderCompletionModule SKIPPED (needs the title ELF and STREAM.IRX)")
endif()
