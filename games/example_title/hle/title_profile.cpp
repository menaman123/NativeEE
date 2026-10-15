// The template title's TitleProfile. The engine DECLARES ActiveTitleProfile()
// (engine/title/title_profile.h); every title module DEFINES it with its own retail facts.
//
// Every value here is a placeholder. A real title fills them from its own ELF and from the
// runtime-environment ground truth for its serial -- see the field comments on TitleProfile for
// what each one means and where it comes from.
#include "engine/title/title_profile.h"

const TitleProfile& ActiveTitleProfile() {
    static const TitleProfile p = [] {
        TitleProfile t;
        // --- identity ---------------------------------------------------------------------
        t.serial = "SLUS_123.45";          // the serial as it appears on the disc
        t.crc = 0;                          // the ELF's CRC32, as PCSX2's GameDB keys on
        t.elf_path = "SLUS_123.45";        // the ELF's path on the ISO9660 volume
        t.elf_dir = "example_title.rep";   // the game_files/ subdirectory holding the copy
        t.elf_sha256 = "";                  // shasum -a 256 of YOUR ELF (the disc verifier's expectation)

        // --- ELF facts: read these off the real PT_LOAD ------------------------------------
        t.entry = 0;
        t.load_base = 0;
        t.load_end = 0;
        t.file_end = 0;
        t.main_stack_top = 0;
        t.sdk_version = "";

        // --- boot environment: argv block, sbrk break word, heap bounds --------------------
        // t.boot.argv0 = "cdrom0:\SLUS_123.45;1";
        // t.boot.argv_block = 0x...;

        // --- the files this module ships, relative to the title directory ------------------
        t.sdk_bindings_file = "hle/sdk_bindings.tsv";
        t.stream_irx_file = "DATA/SYS/MODULE.IRX";
        t.disc_image_file = "";

        // --- GameDB quirks and the frame-budget model --------------------------------------
        // Defaults are "no quirk". Set them from the runtime-environment ground truth for the
        // serial; the budget numbers come from measuring the title's own T0 clock.
        t.quirks.xgkick_defer = false;
        t.quirks.round_sprite_mode = 0;

        // --- savestate schema and recompiler settings -------------------------------------
        t.save.schema_version = 0;
        t.recomp.shard_count = 0;

        // --- SDK statics in THIS ELF (libpad / libgraph / libmc) ---------------------------
        // The engine reads these as data; find them in the recompiled output.
        return t;
    }();
    return p;
}
