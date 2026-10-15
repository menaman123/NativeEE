include_guard(GLOBAL)

# Narrow host-state fixture support: never links ps2r_ee or a title target.
function(ps2r_add_checkpoint_support target)
    add_library(${target} STATIC
        "${PS2R_PRODUCT_ROOT}/engine/core/savestate/savestate_format.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/core/savestate/savestate_writer.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/core/savestate/section_registry.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/core/savestate/payload_reader.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/core/threads/park.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/core/log/logger.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/savestate/thread_section.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/savestate/thread_capture.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/savestate/thread_relauncher.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/hle/thread_manager.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/timing/tick_profile.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/hle/semaphores.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/hle/event_flags.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/sched/ee_run_token.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/sched/ee_event_queue.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/cpu/ps2_float.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/cpu/shadow_call_stack.cpp"
        "${PS2R_PRODUCT_ROOT}/engine/ee/cpu/guest_park.cpp")
    target_include_directories(${target} PUBLIC "${PS2R_PRODUCT_ROOT}")
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_link_libraries(${target} PRIVATE Threads::Threads)
    # logger.cpp and thread_manager.cpp reach the OS through the ONE host shim header
    # (engine/platform/host/platform.h); its arm is ps2r_host in a root build, or the arm's
    # TUs compiled in when this module is configured alone.
    if(TARGET ps2r_host)
        target_link_libraries(${target} PUBLIC ps2r_host)
    elseif(WIN32)
        target_sources(${target} PRIVATE
            "${PS2R_PRODUCT_ROOT}/engine/platform/platform.cpp"
            "${PS2R_PRODUCT_ROOT}/engine/platform/host/host_windows.cpp")
        target_link_libraries(${target} PUBLIC dbghelp psapi)
    else()
        target_sources(${target} PRIVATE
            "${PS2R_PRODUCT_ROOT}/engine/platform/platform.cpp"
            "${PS2R_PRODUCT_ROOT}/engine/platform/host/host_posix.cpp"
            "${PS2R_PRODUCT_ROOT}/engine/platform/posix/fiber_posix.cpp")
    endif()
endfunction()
