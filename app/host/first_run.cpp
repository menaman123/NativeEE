#include "app/host/first_run.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#include "app/host/game_files_stamp.h"
#include "app/host/user_data.h"
#include "engine/platform/host/platform.h"
#include "engine/cdvd/iso_fs.h"
#include "engine/core/config/config.h"
#include "engine/core/config/settings_file.h"
#include "engine/title/title_profile.h"

namespace ps2r::app {
namespace {

constexpr size_t kCopyChunk = 4u << 20;   // 4 MB of logical sectors per read

std::string Dirname(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

bool SameFile(const std::string& a, const std::string& b) {
    // std::filesystem::equivalent: same device + inode on POSIX, the same file ID on Win32
    // (where struct stat's st_ino is always 0); false when either path is missing.
    std::error_code ec;
    return std::filesystem::equivalent(a, b, ec) && !ec;
}

bool WriteWhole(const std::string& path, const std::vector<uint8_t>& bytes, std::string* error) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { *error = "cannot write " + path + ": " + std::strerror(errno); return false; }
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    const bool closed = std::fclose(f) == 0;
    if (!ok || !closed) { *error = "short write to " + path; return false; }
    return true;
}

}  // namespace

const char* FirstRun::StageName(Stage s) {
    switch (s) {
        case Stage::Verify: return "verify";
        case Stage::Elf: return "elf";
        case Stage::Disc: return "disc";
        case Stage::Memcards: return "memcards";
        case Stage::Settings: return "settings";
        case Stage::Stamp: return "stamp";
        case Stage::Done: return "done";
        case Stage::Failed: return "failed";
    }
    return "?";
}

bool FirstRun::MakeDirectories(const std::string& path, std::string* error) {
    // std::filesystem::create_directories: every missing component, either separator,
    // a drive prefix (C:) left alone; true when the tree already exists.
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec || !ps2r::host::IsDirectory(path.c_str())) {
        if (error) *error = "cannot create " + path + ": " + (ec ? ec.message() : std::string("not a directory"));
        return false;
    }
    return true;
}

std::string FirstRun::ElfPath(const Config& cfg, const TitleProfile& title) {
    return cfg.paths.game_files + "/" + title.elf_dir + "/" + title.elf_path;
}

bool FirstRun::GameFilesPresent(const Config& cfg, const TitleProfile& title) {
    std::FILE* f = std::fopen(ElfPath(cfg, title).c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

FirstRun::Plan FirstRun::PlanFor(const Config& cfg, const TitleProfile& title, const std::string& build_id,
                                 const std::string& image, bool copy_disc) {
    Plan p;
    p.image = image;
    p.game_files_dir = cfg.paths.game_files;
    p.elf_dir = title.elf_dir;
    p.serial = title.elf_path;
    p.disc_image_file = title.disc_image_file;
    p.mc_dir = cfg.paths.mc_dir;
    p.settings_file = UserData::SettingsFile(cfg);
    p.build_id = build_id;
    p.expected_sha256 = cfg.launcher.elf_sha256.empty() ? title.elf_sha256 : cfg.launcher.elf_sha256;
    p.copy_disc = copy_disc;
    return p;
}

FirstRun::Result FirstRun::Run(const Plan& plan, const ProgressFn& progress, const std::atomic<bool>* cancel) {
    Result r;
    auto report = [&](Stage s, uint64_t done, uint64_t total, const std::string& msg) {
        if (progress) progress(Progress{s, done, total, msg});
    };
    // The ELF this run wrote (stage 2) when none was there before: a failure or
    // a cancel in a LATER stage removes it again, so GameFilesPresent reverts and
    // the next start returns to the first-run screen instead of offering Play on
    // a half-finished extraction (no disc copy, no paths.iso, no stamp). A
    // re-extraction over a complete install (the ELF already present, byte-equal
    // by the hash check) leaves it alone: the old install stays whole.
    std::string elf_written;
    auto fail = [&](Stage s, const std::string& why) {
        r.ok = false;
        r.error = why;
        if (!elf_written.empty()) {
            std::remove(elf_written.c_str());
            r.elf_removed = true;
        }
        report(Stage::Failed, 0, 0, std::string(StageName(s)) + ": " + why);
        return r;
    };

    // 1. verify
    report(Stage::Verify, 0, 1, "verifying " + plan.image);
    r.verify = DiscVerify::Verify(plan.image, plan.serial, plan.expected_sha256);
    if (r.verify.state != DiscVerify::State::Ok) return fail(Stage::Verify, r.verify.message);
    report(Stage::Verify, 1, 1, r.verify.message);

    IsoFs fs;
    std::string error;
    if (!fs.Open(plan.image, &error)) return fail(Stage::Elf, error);

    // 2. elf
    const std::string elf_dir = plan.game_files_dir + "/" + plan.elf_dir;
    if (!MakeDirectories(elf_dir, &error)) return fail(Stage::Elf, error);
    r.elf_path = elf_dir + "/" + plan.serial;
    report(Stage::Elf, 0, r.verify.elf_size, "writing " + r.elf_path);
    {
        const bool preexisting = [&] {
            std::FILE* f = std::fopen(r.elf_path.c_str(), "rb");
            if (!f) return false;
            std::fclose(f);
            return true;
        }();
        std::vector<uint8_t> elf(r.verify.elf_size);
        const size_t got = fs.ReadBytes(static_cast<uint64_t>(r.verify.elf_lba) * IsoFs::kSectorSize, elf.size(), elf.data());
        if (got != elf.size()) return fail(Stage::Elf, "short read of " + plan.serial + " from the image");
        if (!preexisting) elf_written = r.elf_path;   // a partial write is ours to remove too
        if (!WriteWhole(r.elf_path, elf, &error)) return fail(Stage::Elf, error);
    }
    report(Stage::Elf, r.verify.elf_size, r.verify.elf_size, "ELF written");

    // 3. disc
    const std::string copy_target = plan.game_files_dir + "/" + plan.disc_image_file;
    if (plan.copy_disc && SameFile(plan.image, copy_target)) {
        // A re-extraction from the copy itself: nothing to copy over itself.
        r.disc_path = copy_target;
        report(Stage::Disc, 1, 1, "disc image already in place: " + copy_target);
    } else if (plan.copy_disc) {
        r.disc_path = copy_target;
        if (!MakeDirectories(Dirname(r.disc_path), &error)) return fail(Stage::Disc, error);
        const std::string part = r.disc_path + ".part";
        std::FILE* out = std::fopen(part.c_str(), "wb");
        if (!out) return fail(Stage::Disc, "cannot write " + part + ": " + std::strerror(errno));
        const uint64_t total = fs.ImageBytes();
        std::vector<uint8_t> buf(kCopyChunk);
        uint64_t pos = 0;
        report(Stage::Disc, 0, total, (fs.RawSectors() ? "converting raw image to " : "copying disc image to ") + r.disc_path);
        while (pos < total) {
            if (cancel && cancel->load()) {
                std::fclose(out);
                std::remove(part.c_str());
                return fail(Stage::Disc, "cancelled");
            }
            const size_t want = static_cast<size_t>(std::min<uint64_t>(buf.size(), total - pos));
            const size_t got = fs.ReadBytes(pos, want, buf.data());
            if (got == 0) { std::fclose(out); std::remove(part.c_str()); return fail(Stage::Disc, "read error at byte " + std::to_string(pos)); }
            if (std::fwrite(buf.data(), 1, got, out) != got) {
                std::fclose(out);
                std::remove(part.c_str());
                return fail(Stage::Disc, "write error at byte " + std::to_string(pos) + " of " + part + " (disk full?)");
            }
            pos += got;
            r.bytes_copied = pos;
            report(Stage::Disc, pos, total, "");
        }
        if (std::fclose(out) != 0) { std::remove(part.c_str()); return fail(Stage::Disc, "close failed: " + part); }
        std::remove(r.disc_path.c_str());
        if (std::rename(part.c_str(), r.disc_path.c_str()) != 0)
            return fail(Stage::Disc, "cannot rename " + part + ": " + std::strerror(errno));
        report(Stage::Disc, total, total, "disc image in place");
    } else {
        r.disc_path = plan.image;
        r.disc_referenced = true;
        report(Stage::Disc, 1, 1, "disc image referenced in place: " + plan.image);
    }

    // 4. memcards
    report(Stage::Memcards, 0, 1, "creating memory cards under " + plan.mc_dir);
    for (const char* card : {"mc0", "mc1", "mc0_slot1", "mc1_slot1"})
        if (!MakeDirectories(plan.mc_dir + "/" + card, &error)) return fail(Stage::Memcards, error);
    report(Stage::Memcards, 1, 1, "memory cards ready");

    // 5. settings
    report(Stage::Settings, 0, 1, "settings " + plan.settings_file);
    if (!MakeDirectories(Dirname(plan.settings_file), &error)) return fail(Stage::Settings, error);
    {
        std::ifstream probe(plan.settings_file);
        if (!probe.is_open()) {
            std::ofstream init(plan.settings_file);
            init << "# ps2r.ini -- the title (NativeEE) settings. One `section.key = value` per line;\n"
                    "# the in-game Options pages and the launcher write here. Keys: docs/filemap/settings_table.tsv\n";
            if (!init) return fail(Stage::Settings, "cannot create " + plan.settings_file);
        }
    }
    // paths.iso: the referenced image, or cleared so the copy under game_files is used.
    if (!SettingsFile::Set(plan.settings_file, "paths.iso", r.disc_referenced ? plan.image : std::string(), &error))
        return fail(Stage::Settings, error);
    report(Stage::Settings, 1, 1, "settings written");

    // 6. stamp -- last.
    report(Stage::Stamp, 0, 1, "stamping game files with build " + plan.build_id);
    if (!GameFilesStamp::Write(plan.game_files_dir, plan.build_id))
        return fail(Stage::Stamp, "cannot write " + GameFilesStamp::Path(plan.game_files_dir));
    r.stamp_path = GameFilesStamp::Path(plan.game_files_dir);
    report(Stage::Stamp, 1, 1, "stamp written");
    r.ok = true;
    report(Stage::Done, 1, 1, "game files ready under " + plan.game_files_dir);
    return r;
}

int FirstRun::RunCli(const Plan& plan) {
    Stage last = Stage::Verify;
    uint64_t last_pct = ~0ull;
    const Result r = Run(plan, [&](const Progress& p) {
        if (p.stage == Stage::Disc && p.total > 0 && p.message.empty()) {
            const uint64_t pct = p.done * 100 / p.total;
            if (pct != last_pct && (pct % 10 == 0 || pct == 100)) {
                std::fprintf(stderr, "nativeee: extract disc: %llu%% (%llu / %llu MB)\n", (unsigned long long)pct,
                             (unsigned long long)(p.done >> 20), (unsigned long long)(p.total >> 20));
                last_pct = pct;
            }
            return;
        }
        if (!p.message.empty()) std::fprintf(stderr, "nativeee: extract %s: %s\n", StageName(p.stage), p.message.c_str());
        last = p.stage;
    });
    (void)last;
    if (!r.ok) {
        std::fprintf(stderr, "nativeee: extract: REFUSED -- %s%s\n", r.error.c_str(),
                     r.elf_removed ? " (the ELF written by this run was removed)" : "");
        return 1;
    }
    std::fprintf(stderr,
                 "nativeee: extract: OK elf=%s disc=%s%s bytes_copied=%llu stamp=%s\n", r.elf_path.c_str(),
                 r.disc_path.c_str(), r.disc_referenced ? " (referenced)" : "", (unsigned long long)r.bytes_copied,
                 r.stamp_path.c_str());
    return 0;
}

}  // namespace ps2r::app
