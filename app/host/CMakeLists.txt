# app/host — the composition root, and the generic host services around the engine.
#
# A title module (games/<serial>_<slug>/) owns the play composition: the recompiled EE code,
# the per-subsystem HLE and the wiring that boots them. This directory holds only what is
# title-neutral — the launcher and first-run flow, disc verification, user data and save
# slots, the session services (clock, controls, audio, window, workers, restore), the
# shutdown ladder and the censuses — plus main.cpp, which composes whatever title is linked.
#
# With no title linked, nativeee reports its build identity and says there is nothing to run
# (main.cpp). The engine, the component tests and the tools do not need one.

# The title-neutral host services. A static library so the tests can link the pieces they
# exercise without pulling in a composition root.
add_library(ps2r_app_host STATIC
    user_data.cpp
    game_files_stamp.cpp
    disc_verify.cpp
    first_run.cpp
    launcher.cpp
    launcher_settings.cpp
    controls_bindings.cpp
    controls_page_binding.cpp
    platform_input.cpp
    tick_thread.cpp
    clock_census.cpp
    camera_track.cpp
    display_sequence.cpp
    frame_pacer.cpp
    savestate_slots.cpp
    session_capture.cpp
    session_relaunch.cpp
    cold_restore.cpp
    live_restore_gate.cpp
    leg_closer.cpp
    mtvu_stack.cpp
    vu1_aot_cache.cpp
    vu1_aot_builder.cpp
    vu1_aot_pack.cpp
    shutdown_ladder.cpp
    shutdown_census.cpp
    subsystem_census.cpp
    texture_pack_path.cpp
    checkpoint_capture.cpp
    session_audio.cpp
    session_checkpoint.cpp
    session_clock.cpp
    session_controls.cpp
    session_files.cpp
    session_restart.cpp
    session_restore.cpp
    session_runtime.cpp
    session_runtime_restore.cpp
    session_window.cpp
    session_workers.cpp
)
target_include_directories(ps2r_app_host PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(ps2r_app_host PRIVATE ${PS2R_GENERATED_DIR})   # vu1_aot_paths.h (the builder's defaults), vu1_aot_abi_key.h
add_dependencies(ps2r_app_host vu1_aot_prelude_stage)   # the generated key header precedes the builder's compile
target_compile_options(ps2r_app_host PRIVATE -Wall -Wextra -Werror)
# iop_arm.h (the play/oracle seam) #errors without this definition. This tree builds only
# the PLAY shape (no IOP linked); a title that links ps2r_iop_models would override it.
target_compile_definitions(ps2r_app_host PUBLIC PS2R_IOP_ORACLE=0)
# Derived from what these sources include: core, ee, platform, vu, gif, title, ipu, dmac,
# cdvd, vif, render, pad, iop, gs. ps2r_iop PUBLIC-links ps2r_platform which PUBLIC-links
# ps2r_core, so only the leaves are named (naming a library twice makes ld warn).
target_link_libraries(ps2r_app_host PUBLIC
    ps2r_title_api ps2r_ee ps2r_vu ps2r_gif_hle ps2r_dmac ps2r_cdvd ps2r_mc ps2r_pad
    ps2r_sound ps2r_sound_hle ps2r_gs_hle_queue ps2r_render_gl ps2r_render_review
    ps2r_platform_sdl ps2r_ipu ps2r_imgui ps2r_session_checkpoint ps2r_session_clock)
if(PS2R_DIAGNOSTICS)
    target_compile_definitions(ps2r_app_host PRIVATE PS2R_DIAGNOSTICS=1)
    target_link_libraries(ps2r_app_host PUBLIC ps2r_diag)
endif()

# The binary. PS2R_TITLE_ID / PS2R_TITLE_DIR are empty when no title is configured; main.cpp
# reports that rather than attempting a boot it cannot complete.
add_executable(nativeee main.cpp ${PS2R_BUILD_ID_CPP})
add_dependencies(nativeee generate_build_id vu1_aot_prelude_stage)
# The VU1 AOT builder's generated image libraries resolve the engine's symbols (the GENERIC
# bodies, the flag pipeline, the kick) against this executable at load: macOS exports them by
# default (and the library is linked with -undefined dynamic_lookup); Linux needs -rdynamic.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(nativeee PRIVATE -rdynamic)
endif()
target_include_directories(nativeee PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(nativeee PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(nativeee PRIVATE PS2R_TITLE_ID="${PS2R_TITLE}")
if(PS2R_TITLE)
    target_compile_definitions(nativeee PRIVATE
        PS2R_TITLE_DIR="${PS2R_GAMES_DIR}/${PS2R_TITLE}")
endif()
target_link_libraries(nativeee PRIVATE ps2r_app_host)
# A title module that supplies a play composition links it in here.
if(TARGET ps2r_title_hle)
    target_link_libraries(nativeee PRIVATE ps2r_title_hle)
endif()
# The play composition itself: a title that defines ps2r_title_play (engine/title/title_play.h,
# `int TitlePlayMain(int, char**)`) is run by main.cpp; without it the binary reports that the
# title supplies no composition. Same if(TARGET ...) convention as the other title targets.
if(TARGET ps2r_title_play)
    target_link_libraries(nativeee PRIVATE ps2r_title_play)
    target_compile_definitions(nativeee PRIVATE PS2R_TITLE_HAS_PLAY=1)
endif()

set_source_files_properties(${PS2R_BUILD_ID_CPP} PROPERTIES GENERATED TRUE)
