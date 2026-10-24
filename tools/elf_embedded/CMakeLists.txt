# tools/elf_embedded -- ps2r_elf_embedded: one IOP module out of a title ELF that embeds its IRX
# images, SHA-256 gated. Compiled from the engine's Sha256 source + the header-only IRX section
# parser (engine/iop/iop_relocator.h) directly, like tools/disc_extract: it runs BEFORE the title
# is generated (cmake/TitleInputs.cmake rows `<ELF>!<module>` / `<ELF>@<offset>+<size>`).
add_executable(ps2r_elf_embedded
    main.cpp
    ${CMAKE_SOURCE_DIR}/engine/core/hash/sha256.cpp)
target_include_directories(ps2r_elf_embedded PRIVATE ${CMAKE_SOURCE_DIR})
target_compile_options(ps2r_elf_embedded PRIVATE -Wall -Wextra -Werror)
