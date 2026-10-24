#pragma once
// TitleProfile: the retail facts of ONE title that the engine mechanisms consume as data
// (PHASE3_PLAN 2.8, full form): identity, ELF facts, boot environment, the GS/VU quirks
// PCSX2's GameDB applies to the serial, the frame-budget model parameters, the savestate
// schema the anchors were written in, and the title-relative data paths.
//
// This is the CONTRACT, and it lives in engine/ because the composition root has to be able
// to name it without naming a title. The engine declares the shape and the accessor; the
// linked title module (games/<serial>_<slug>/hle) defines ActiveTitleProfile() with its own
// values. That makes the title a LINK-time dependency of app/host rather than a compile-time
// one -- the seam a second title is added across.
#include <cstdint>
#include <string>

#include "engine/ee/hle/boot_env.h"

struct TitleProfile {
    std::string serial;
    std::string display_name;   // the title as the launcher / window name it ("" = the serial)
    uint32_t crc = 0;
    std::string elf_path;     // path on the disc image
    std::string elf_dir;      // game_files/ subdirectory holding the ELF copy
    std::string elf_sha256;   // SHA-256 of the retail ELF this build recompiled (the disc verifier's expectation)
    uint32_t main_stack_top = 0;  // tid 1's $sp at the ELF entry (crt0 rebuilds its own frame)
    uint32_t entry = 0;
    uint32_t load_base = 0;   // PT_LOAD p_vaddr
    uint32_t load_end = 0;    // PT_LOAD memsz end (exclusive)
    uint32_t file_end = 0;    // PT_LOAD filesz end (exclusive)
    std::string sdk_version;
    BootParams boot;
    std::string sdk_bindings_file;  // hle/sdk_bindings.tsv, relative to the title directory
    std::string stream_irx_file;    // the disc-side IRX the composition root boots, relative to game_files
    std::string disc_image_file;    // the ISO9660 disc image, relative to game_files (Config.paths.iso overrides)

    // GameDB quirks (runtime-environment: XGKick gamefix, roundSprite mode).
    struct Quirks {
        bool xgkick_defer = false;      // VU1 XGKICK deferred until the MFLAG/FMAC drain
        int round_sprite_mode = 0;      // sprite vertex half-pixel rounding (0 off, 1 half, 2 full)
        // T0_MODE as the title's boot timer init programs it (T0_COUNT=0); the
        // seed for a savestate that carries no TIMR section.
        uint16_t boot_t0_mode = 0;
        // Per-field frame-budget model (OLD budget_math.h): the title's T0
        // budget clock in BUSCLK/256 counts -- anchor = the boundary tick's
        // count, base per field, low-draw base and the per-step drain.
        struct Budget {
            uint32_t anchor = 0;
            uint32_t base = 0;
            uint32_t base_low = 0;
            uint32_t step = 0;
            uint32_t lowdraw = 0;
        } budget;
    } quirks;

    struct Save {
        uint32_t schema_version = 0;     // .ps2st file version the anchors carry
        std::string anchor_dir;          // OLD/save_states (read-only oracle inputs)
        std::string expected_hashes;     // verify/restore_mhash_expected.tsv
        std::string manual_body_ledger;  // verify/manual_body_ledger.tsv
    } save;

    struct Recomp {
        uint32_t shard_count = 0;
    } recomp;

    // libpad 3.0.1's three statics in the title ELF (LibpadBindings::Statics):
    // the (port, slot) pointer table, the open-flag bytes, the per-slot
    // static buffers. The engine reads them as data.
    struct Pad {
        uint32_t port_table = 0;
        uint32_t open_flags = 0;
        uint32_t static_area = 0;
        // The controller state machine's entry (the boot UI's case-2 hook site)
        // and the level-load object pointer whose +4 word carries the case.
        uint32_t ctrl_sm_entry = 0;
        uint32_t ctrl_object_ptr = 0;
    } pad;

    // libgraph 3.0's static in the title ELF: the sceGsGParam block sceGsResetGraph
    // fills and sceGsSyncVCallback keeps its handler in (LibgraphBindings::Statics).
    struct Gs {
        uint32_t gparam_addr = 0;
        // The title's own vsync-wait routine when its ELF links no sceGsSyncV (a flag +
        // WaitSema the VBLANK INTC handler signals): the composition root installs the
        // engine's WaitForVblank mirror there (GsSync::WaitForVblank -> the frame boundary).
        // 0 = the title waits through sceGsSyncV (the libgraph row).
        uint32_t vblank_wait_entry = 0;
    } gs;

    // libmc 3.0.1's static in the title ELF: the sceSifClientData sceMcInit binds to
    // mcserv; its bound-server word (+0x24) is the readiness predicate every
    // sceMc* row but sceMcSync gates on (McCommandModel::Bound; -100 while 0).
    struct Mc {
        uint32_t client_addr = 0;
    } mc;
};

// Defined by the linked title module (games/<serial>_<slug>/hle/title_profile.cpp).
// A build configured with no title links no definition: every target that calls this is
// skipped by its CMakeLists, exactly as the generated-recomp targets are.
const TitleProfile& ActiveTitleProfile();
