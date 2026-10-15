#pragma once
// FirstRun -- the first-run extraction (CHARTER 2 C2): from a verified disc
// image to a playable user data root.
//
//   1. verify   DiscVerify (ISO9660, SLUS_123.45 present, ELF SHA-256 == the
//               build's) -- a refusal stops here with the exact report;
//   2. elf      <game_files>/<elf_dir>/<serial> written from the volume;
//   3. disc     the disc image: COPIED into <game_files>/<disc_image_file> as
//               a plain 2048-byte-per-sector image (a raw .bin is converted on
//               the way; written through a .part temporary, renamed when
//               complete) -- or, when copy_disc is false, left in place and
//               referenced through `paths.iso` in the settings file;
//   4. memcards <mc_dir>/mc0, mc1 (+ the slot-1 twins) created empty -- the
//               title formats them on first use (McHostStore);
//   5. settings the settings file created when absent (a header line), the
//               launcher's own keys written (paths.iso when referenced);
//   6. stamp    GameFilesStamp::Write(<game_files>, build_id) -- the LAST step,
//               so an interrupted extraction never passes for a complete one.
// Progress is reported per stage with a byte count for the copy (the launcher's
// progress bar, the CLI's lines); `cancel` (optional) stops the copy between
// chunks and removes the partial file. A failure or cancel AFTER stage 2 also
// removes the ELF this run wrote (when none was there before), so
// GameFilesPresent reverts and the launcher's next start is the first-run
// screen again -- never Play over a half-finished extraction.
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "app/host/disc_verify.h"

namespace ps2r { class Config; }
struct TitleProfile;

namespace ps2r::app {

class FirstRun {
public:
    struct Plan {
        std::string image;             // the disc image the user supplied
        std::string game_files_dir;    // cfg.paths.game_files (resolved)
        std::string elf_dir;           // TitleProfile::elf_dir
        std::string serial;            // TitleProfile::elf_path ("SLUS_123.45")
        std::string disc_image_file;   // TitleProfile::disc_image_file (relative to game_files_dir)
        std::string mc_dir;            // cfg.paths.mc_dir (resolved)
        std::string settings_file;     // UserData::SettingsFile(cfg)
        std::string build_id;          // g_build_id
        std::string expected_sha256;   // launcher.elf_sha256 when set, else TitleProfile::elf_sha256
        bool copy_disc = true;
    };
    enum class Stage { Verify, Elf, Disc, Memcards, Settings, Stamp, Done, Failed };
    struct Progress {
        Stage stage = Stage::Verify;
        uint64_t done = 0;      // bytes (Disc) or 0/1 for the one-shot stages
        uint64_t total = 0;
        std::string message;
    };
    using ProgressFn = std::function<void(const Progress&)>;
    struct Result {
        bool ok = false;
        std::string error;             // the refusal / failure text (also in verify.message for stage 1)
        DiscVerify::Report verify;
        std::string elf_path;
        std::string disc_path;         // the copied image, or the referenced one
        bool disc_referenced = false;
        uint64_t bytes_copied = 0;
        std::string stamp_path;
        bool elf_removed = false;      // a later-stage failure/cancel removed the ELF this run had written
    };

    static const char* StageName(Stage s);
    // The plan for this Config / title / image; `copy_disc` from the caller.
    static Plan PlanFor(const Config& cfg, const TitleProfile& title, const std::string& build_id,
                        const std::string& image, bool copy_disc);
    // Is the extracted ELF where the composition root looks for it?
    static std::string ElfPath(const Config& cfg, const TitleProfile& title);
    static bool GameFilesPresent(const Config& cfg, const TitleProfile& title);
    // Runs the six stages. Synchronous; call from a worker when a GUI drives it.
    static Result Run(const Plan& plan, const ProgressFn& progress, const std::atomic<bool>* cancel = nullptr);
    // The CLI route (`--extract-disc <image>`): Run with stderr progress lines;
    // returns the process exit code (0 ok, 1 refused/failed).
    static int RunCli(const Plan& plan);

private:
    static bool MakeDirectories(const std::string& path, std::string* error);
};

}  // namespace ps2r::app
