# engine/pad -- the pad (PHASE4_PLAN section 2.8). Stage 4E: the scePad wire
# image and its staging (PadProtocol); 4E': the libpad rows (LibpadBindings),
# the padman RPC face (PadmanModule), the boundary stamp duty (PadStampDuty:
# PadProtocol + core/triage's InputLog replay/recorder). savestate/: the PADS
# section codec (PadsSection) over PadProtocol.
add_library(ps2r_pad STATIC
    hle/pad_protocol.cpp
    hle/libpad_bindings.cpp
    hle/padman_module.cpp
    hle/pad_stamp_duty.cpp
    savestate/pads_section.cpp
)
target_include_directories(ps2r_pad PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_pad PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ps2r_pad PUBLIC ps2r_ee ps2r_sif)
