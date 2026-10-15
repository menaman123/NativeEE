# engine/ee -- Emotion Engine side of the runtime (PHASE3_PLAN section 2).
# Stage 3A: cpu state + float helpers + memory (main RAM, scratchpad, address
# map, fastmem, MMIO router/sink, ELF loader, data-map applier).
# Stage 3B: the generated-code ABI (recomp_abi/), the seam providers (cpu/seams.cpp,
# cpu/guest_park), and the wrappers over the generated dispatch surface (dispatch_table,
# shadow_call_stack). The wrappers reference symbols the generated common TU defines,
# so ps2r_ee is only complete when linked together with ps2r_title_recomp.
# Stage 3C2: the syscall table, dyn-interp tripwire, boot env, libc, SDK binding manifest
# and the libkernl families (kernl_boot/timer/file_io/sif_rpc, libscf, libdev).
# Stage 3C1: the kernel objects (hle/thread_manager, semaphores, event_flags, alarms,
# rpc_callback_queue) and the scheduler (sched/ee_run_token, ee_event_queue).
# Stage 3D1: the timing domain (timing/tick_source, emu_domain, regime_detector,
# derived_clocks, budget_clock, phase_classifier, turbo_governor, warp).
# Stage 3D2: the boundary (timing/tick_sync, frame_rendezvous, boundary_sequencer,
# boundary_protocol) and the interrupt controller + EE timers (intc/).
add_library(ps2r_ee STATIC
    cpu/ps2_float.cpp
    cpu/cpu_context_init.cpp
    cpu/guest_park.cpp
    cpu/park_site_dump.cpp
    cpu/seams.cpp
    cpu/dispatch_table.cpp
    cpu/shadow_call_stack.cpp
    recomp_abi/recomp_abi.cpp
    hle/thread_manager.cpp
    hle/semaphores.cpp
    hle/event_flags.cpp
    hle/alarms.cpp
    hle/rpc_callback_queue.cpp
    hle/syscall_table.cpp
    hle/dyn_interp.cpp
    hle/boot_env.cpp
    hle/libc.cpp
    hle/sdk_binding_manifest.cpp
    hle/kernl_boot.cpp
    hle/kernl_timer.cpp
    hle/kernl_file_io.cpp
    hle/kernl_sbrk.cpp
    hle/kernl_sif_rpc.cpp
    hle/libscf_bindings.cpp
    hle/libdev_bindings.cpp
    sched/ee_run_token.cpp
    sched/ee_event_queue.cpp
    timing/tick_source.cpp
    timing/emu_domain.cpp
    timing/regime_detector.cpp
    timing/derived_clocks.cpp
    timing/budget_clock.cpp
    timing/phase_classifier.cpp
    timing/turbo_governor.cpp
    timing/warp.cpp
    timing/tick_sync.cpp
    savestate/memory_sections.cpp
    savestate/state_restorer.cpp
    savestate/thread_relauncher.cpp
    savestate/thread_section.cpp
    savestate/thread_capture.cpp
    savestate/kernel_sections.cpp
    savestate/heap_section.cpp
    savestate/intc_section.cpp
    savestate/timer_section.cpp
    timing/frame_rendezvous.cpp
    timing/boundary_sequencer.cpp
    timing/boundary_completion.cpp
    timing/game_boundary.cpp
    timing/boundary_protocol.cpp
    timing/tick_profile.cpp
    intc/intc.cpp
    intc/ee_timers.cpp
    intc/timer_codec.cpp
    mem/main_memory.cpp
    mem/scratchpad.cpp
    mem/address_map.cpp
    mem/fast_mem.cpp
    mem/mmio_router.cpp
    mem/mmio_sink.cpp
    mem/elf_loader.cpp
    mem/data_map_applier.cpp)
target_include_directories(ps2r_ee PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_ee PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_ee PUBLIC ps2r_core)
