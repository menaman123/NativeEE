// DiscVerifyVectors (CHARTER 2 C2): the disc verifier's accept / reject vectors
// over synthetic ISO9660 images (tests/lib Iso9660Image), and the FirstRun
// pipeline on the accepted one. Never a real disc image.
//   accept : plain .iso and raw .bin carrying SLUS_123.45 with the expected hash
//   reject : missing file; not ISO9660 (noise); no title (SYSTEM.CNF only);
//            PAL (SLES_525.68) -> "PAL release not supported"; NTSC-J (SLPM);
//            another NTSC-U title (SLUS_200.00) -> "Wrong disc"; the right
//            serial with another hash -> HashMismatch naming both digests;
//            truncated image -> ReadError
//   FirstRun: raw .bin -> plain .iso copy under <game_files>, the ELF byte-equal,
//            memcards, settings (paths.iso cleared), the stamp LAST; a refused
//            image leaves nothing behind; cancel removes the .part file AND the
//            ELF this run wrote (a later-stage failure likewise); a cancelled
//            re-extraction over a complete install leaves the install whole.
// argv[1] = scratch dir.
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app/host/disc_verify.h"
#include "app/host/first_run.h"
#include "app/host/game_files_stamp.h"
#include "app/host/user_data.h"
#include "engine/cdvd/iso_fs.h"
#include "engine/core/hash/sha256.h"
#include "tests/lib/iso9660_image.h"

using ps2r::app::DiscVerify;
using ps2r::app::FirstRun;

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

static std::vector<uint8_t> Pattern(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = static_cast<uint8_t>(seed + i * 13 + (i >> 7));
    return v;
}
static bool Exists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0; }
static void RemoveTree(const std::string& p) {
    struct stat st;
    if (::lstat(p.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        if (DIR* d = ::opendir(p.c_str())) {
            while (dirent* e = ::readdir(d)) {
                if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
                RemoveTree(p + "/" + e->d_name);
            }
            ::closedir(d);
        }
        ::rmdir(p.c_str());
    } else {
        std::remove(p.c_str());
    }
}
static std::vector<uint8_t> ReadWhole(const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}
static std::string WriteImage(const std::string& path, bool raw, const char* serial, const std::vector<uint8_t>& elf,
                              bool with_title = true) {
    ps2r::testlib::Iso9660Image img;
    img.SetVolumeId("FAKE_TWIN");
    if (with_title) img.AddFile(serial, elf);
    img.AddFile("SYSTEM.CNF", std::vector<uint8_t>{'B', 'O', 'O', 'T', '2'});
    img.AddFile("DATA/STREAM.IRX", Pattern(3000, 0x33));
    std::string err;
    if (!img.Write(path, raw, &err)) std::printf("  write %s: %s\n", path.c_str(), err.c_str());
    return path;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: test_disc_verify <scratch>\n"); return 2; }
    const std::string S = argv[1];
    ::mkdir(S.c_str(), 0755);
    // The FirstRun cases assert on what a run leaves behind: start them from
    // nothing (a previous run's layout would read as a pre-existing install).
    for (const char* sub : {"data", "ref", "bad", "cancel", "fail"}) RemoveTree(S + "/" + sub);
    const std::vector<uint8_t> elf = Pattern(70000, 0x5a);   // > 32 sectors: the hash walks several chunks
    const std::string hash = ps2r::hash::Sha256::HexOf(elf.data(), elf.size());
    const std::string other_hash = ps2r::hash::Sha256::HexOf("x", 1);
    const char* kSerial = "SLUS_123.45";

    // Accept: plain and raw.
    const std::string plain = WriteImage(S + "/ok.iso", false, kSerial, elf);
    const std::string raw = WriteImage(S + "/ok.bin", true, kSerial, elf);
    {
        const DiscVerify::Report r = DiscVerify::Verify(plain, kSerial, hash);
        CHECK(r.state == DiscVerify::State::Ok && !r.raw_sectors && r.elf_size == elf.size() && r.elf_sha256 == hash &&
              r.region == "NTSC-U", "accept_plain_iso");
        std::printf("  %s\n", r.message.c_str());
        const DiscVerify::Report u = DiscVerify::Verify(raw, kSerial, "ABCDEF" + hash.substr(6));
        CHECK(u.state == DiscVerify::State::HashMismatch, "hash_compare_is_exact");
        // Upper-case expectation accepted.
        std::string up = hash; for (char& c : up) c = static_cast<char>(std::toupper(c));
        CHECK(DiscVerify::Verify(raw, kSerial, up).state == DiscVerify::State::Ok, "accept_raw_bin_uppercase_expectation");
        CHECK(DiscVerify::Verify(raw, kSerial, hash).raw_sectors, "raw_layout_reported");
    }
    // Reject vectors.
    {
        const DiscVerify::Report r = DiscVerify::Verify(S + "/missing.iso", kSerial, hash);
        CHECK(r.state == DiscVerify::State::NotFound && r.message.find("Cannot open") != std::string::npos, "reject_missing_file");
    }
    {
        const std::string noise = S + "/noise.bin";
        std::FILE* f = std::fopen(noise.c_str(), "wb");
        const std::vector<uint8_t> n = Pattern(2048 * 40, 0x99);
        std::fwrite(n.data(), 1, n.size(), f);
        std::fclose(f);
        const DiscVerify::Report r = DiscVerify::Verify(noise, kSerial, hash);
        CHECK(r.state == DiscVerify::State::NotIso9660 && r.message.find("ISO9660") != std::string::npos, "reject_not_iso9660");
        std::printf("  %s\n", r.message.c_str());
    }
    {
        const DiscVerify::Report r = DiscVerify::Verify(WriteImage(S + "/notitle.iso", false, kSerial, elf, false), kSerial, hash);
        CHECK(r.state == DiscVerify::State::NoTitle && r.message.find("No PlayStation 2 title") != std::string::npos, "reject_no_title");
    }
    {
        const DiscVerify::Report r = DiscVerify::Verify(WriteImage(S + "/pal.iso", false, "SLES_525.68", elf), kSerial, hash);
        CHECK(r.state == DiscVerify::State::WrongRegion && r.found_serial == "SLES_525.68" && r.region == "PAL" &&
              r.message.find("PAL release not supported") != std::string::npos, "reject_pal_region");
        std::printf("  %s\n", r.message.c_str());
        const DiscVerify::Report j = DiscVerify::Verify(WriteImage(S + "/jp.iso", false, "SLPM_655.55", elf), kSerial, hash);
        CHECK(j.state == DiscVerify::State::WrongRegion && j.region == "NTSC-J" &&
              j.message.find("NTSC-J release not supported") != std::string::npos, "reject_ntscj_region");
        const DiscVerify::Report o = DiscVerify::Verify(WriteImage(S + "/other.iso", false, "SLUS_200.00", elf), kSerial, hash);
        CHECK(o.state == DiscVerify::State::WrongRegion && o.message.find("Wrong disc") != std::string::npos &&
              o.message.find("SLUS_200.00") != std::string::npos, "reject_other_ntscu_title");
    }
    {
        const DiscVerify::Report r = DiscVerify::Verify(plain, kSerial, other_hash);
        CHECK(r.state == DiscVerify::State::HashMismatch && r.message.find(hash) != std::string::npos &&
              r.message.find(other_hash) != std::string::npos, "reject_wrong_elf_hash_names_both");
        std::printf("  %s\n", r.message.c_str());
    }
    {
        // Truncated: cut the image inside the ELF extent.
        const std::vector<uint8_t> whole = ReadWhole(plain);
        const std::string trunc = S + "/trunc.iso";
        std::FILE* f = std::fopen(trunc.c_str(), "wb");
        std::fwrite(whole.data(), 1, whole.size() - 40000, f);
        std::fclose(f);
        const DiscVerify::Report r = DiscVerify::Verify(trunc, kSerial, hash);
        CHECK(r.state == DiscVerify::State::ReadError && r.message.find("truncated") != std::string::npos, "reject_truncated_image");
    }
    CHECK(DiscVerify::RegionOf("SCES_123.45;1") == "PAL" && DiscVerify::RegionOf("SLUS_123.45") == "NTSC-U" &&
          DiscVerify::RegionOf("SYSTEM.CNF").empty() && !DiscVerify::LooksLikeSerial("SLUS_12345"), "serial_shapes");

    // FirstRun on the raw image: plain copy, ELF, memcards, settings, stamp last.
    FirstRun::Plan plan;
    plan.image = raw;
    plan.game_files_dir = S + "/data/game_files";
    plan.elf_dir = "example_title.rep";
    plan.serial = kSerial;
    plan.disc_image_file = "example_title.rep/the title.iso";
    plan.mc_dir = S + "/data/memcards";
    plan.settings_file = S + "/data/ps2r.ini";
    plan.build_id = "TEST-BUILD-1";
    plan.expected_sha256 = hash;
    plan.copy_disc = true;
    std::vector<FirstRun::Stage> stages;
    uint64_t disc_total = 0, disc_max = 0;
    const FirstRun::Result r = FirstRun::Run(plan, [&](const FirstRun::Progress& p) {
        if (stages.empty() || stages.back() != p.stage) stages.push_back(p.stage);
        if (p.stage == FirstRun::Stage::Disc) { disc_total = p.total; if (p.done > disc_max) disc_max = p.done; }
    });
    CHECK(r.ok, "firstrun_ok");
    if (!r.ok) std::printf("  %s\n", r.error.c_str());
    CHECK(ReadWhole(r.elf_path) == elf, "firstrun_elf_byte_equal");
    {
        IsoFs fs;
        CHECK(fs.Open(r.disc_path) && !fs.RawSectors() && fs.VolumeId() == "FAKE_TWIN", "firstrun_disc_copy_is_plain_iso");
        IsoFs::Entry e;
        std::vector<uint8_t> got(elf.size());
        CHECK(fs.Find(kSerial, e) && fs.ReadBytes(static_cast<uint64_t>(e.lba) * 2048, got.size(), got.data()) == got.size() && got == elf,
              "firstrun_disc_copy_carries_elf");
        CHECK(disc_total == fs.ImageBytes() && disc_max == disc_total, "firstrun_progress_reaches_total");
        CHECK(!Exists(r.disc_path + ".part"), "firstrun_no_part_left");
    }
    CHECK(Exists(plan.mc_dir + "/mc0") && Exists(plan.mc_dir + "/mc1") && Exists(plan.mc_dir + "/mc0_slot1"), "firstrun_memcards");
    {
        const std::vector<uint8_t> ini = ReadWhole(plan.settings_file);
        const std::string text(ini.begin(), ini.end());
        CHECK(text.find("paths.iso = ") != std::string::npos && text.find("# ps2r.ini") == 0, "firstrun_settings_file");
    }
    {
        const ps2r::app::GameFilesStamp::Check c = ps2r::app::GameFilesStamp::Verify(plan.game_files_dir, "TEST-BUILD-1");
        CHECK(c.state == ps2r::app::GameFilesStamp::State::Match, "firstrun_stamp_matches_build");
        const bool order = stages.size() >= 7 && stages[0] == FirstRun::Stage::Verify && stages[1] == FirstRun::Stage::Elf &&
                           stages[2] == FirstRun::Stage::Disc && stages[3] == FirstRun::Stage::Memcards &&
                           stages[4] == FirstRun::Stage::Settings && stages[5] == FirstRun::Stage::Stamp &&
                           stages[6] == FirstRun::Stage::Done;
        CHECK(order, "firstrun_stage_order_stamp_last");
    }
    // Referenced (no copy): paths.iso names the image; no copy under game_files.
    {
        FirstRun::Plan ref = plan;
        ref.game_files_dir = S + "/ref/game_files";
        ref.mc_dir = S + "/ref/memcards";
        ref.settings_file = S + "/ref/ps2r.ini";
        ref.copy_disc = false;
        const FirstRun::Result rr = FirstRun::Run(ref, nullptr);
        const std::vector<uint8_t> ini = ReadWhole(ref.settings_file);
        const std::string text(ini.begin(), ini.end());
        CHECK(rr.ok && rr.disc_referenced && text.find("paths.iso = " + raw) != std::string::npos &&
              !Exists(ref.game_files_dir + "/" + plan.disc_image_file), "firstrun_reference_in_place");
    }
    // Refused: nothing laid down.
    {
        FirstRun::Plan bad = plan;
        bad.image = S + "/pal.iso";
        bad.game_files_dir = S + "/bad/game_files";
        bad.mc_dir = S + "/bad/memcards";
        bad.settings_file = S + "/bad/ps2r.ini";
        const FirstRun::Result rb = FirstRun::Run(bad, nullptr);
        CHECK(!rb.ok && rb.verify.state == DiscVerify::State::WrongRegion && !Exists(S + "/bad"), "firstrun_refused_leaves_nothing");
    }
    // Cancelled during the copy: the .part is removed, no stamp -- AND the ELF this
    // run wrote is removed, so GameFilesPresent reverts (review C2 loop 1: a
    // half-finished extraction must not read as "ready" on the next start).
    {
        FirstRun::Plan c = plan;
        c.game_files_dir = S + "/cancel/game_files";
        c.mc_dir = S + "/cancel/memcards";
        c.settings_file = S + "/cancel/ps2r.ini";
        const std::string c_elf = c.game_files_dir + "/" + plan.elf_dir + "/" + kSerial;
        std::atomic<bool> cancel{false};
        bool elf_seen_written = false;
        const FirstRun::Result rc = FirstRun::Run(c, [&](const FirstRun::Progress& p) {
            if (p.stage == FirstRun::Stage::Elf && p.message == "ELF written") elf_seen_written = Exists(c_elf);
            if (p.stage == FirstRun::Stage::Disc) cancel.store(true);
        }, &cancel);
        CHECK(!rc.ok && rc.error == "cancelled" && !Exists(c.game_files_dir + "/" + plan.disc_image_file + ".part") &&
              !Exists(ps2r::app::GameFilesStamp::Path(c.game_files_dir)), "firstrun_cancel_removes_part_no_stamp");
        CHECK(elf_seen_written && rc.elf_removed && !Exists(c_elf), "firstrun_cancel_removes_written_elf");
    }
    // A later-stage FAILURE (memcards: mc_dir's parent is a regular file) also
    // removes the written ELF; the disc copy (complete by then) stays, no stamp.
    {
        FirstRun::Plan f = plan;
        f.game_files_dir = S + "/fail/game_files";
        f.mc_dir = S + "/fail/blocker/memcards";
        f.settings_file = S + "/fail/ps2r.ini";
        ::mkdir((S + "/fail").c_str(), 0755);
        if (std::FILE* b = std::fopen((S + "/fail/blocker").c_str(), "wb")) std::fclose(b);
        const FirstRun::Result rf = FirstRun::Run(f, nullptr);
        CHECK(!rf.ok && rf.elf_removed && !Exists(f.game_files_dir + "/" + plan.elf_dir + "/" + kSerial) &&
              Exists(f.game_files_dir + "/" + plan.disc_image_file) && !Exists(ps2r::app::GameFilesStamp::Path(f.game_files_dir)),
              "firstrun_later_failure_removes_written_elf");
    }
    // A cancelled RE-extraction over a complete install leaves the install whole:
    // the pre-existing ELF (byte-equal by the hash check), the old disc copy and
    // the old stamp all stay.
    {
        std::atomic<bool> cancel{false};
        const FirstRun::Result rr = FirstRun::Run(plan, [&](const FirstRun::Progress& p) {
            if (p.stage == FirstRun::Stage::Disc) cancel.store(true);
        }, &cancel);
        const ps2r::app::GameFilesStamp::Check c = ps2r::app::GameFilesStamp::Verify(plan.game_files_dir, "TEST-BUILD-1");
        CHECK(!rr.ok && !rr.elf_removed && ReadWhole(r.elf_path) == elf && Exists(r.disc_path) &&
              c.state == ps2r::app::GameFilesStamp::State::Match, "firstrun_cancelled_reextract_keeps_install");
    }
    // UserData::CarryOverLegacyRoot -- the former product name's root
    // (kLegacyAppName) is copied into an EMPTY new root once; a non-empty root,
    // or no legacy sibling, is left alone; the legacy directory is never changed.
    {
        const std::string base = S + "/userdata";
        const std::string legacy = base + "/" + ps2r::app::UserData::kLegacyAppName;
        const std::string root = base + "/" + ps2r::app::UserData::kAppName;
        RemoveTree(base);
        ::mkdir(base.c_str(), 0755);
        ::mkdir(root.c_str(), 0755);
        std::string note = "x";
        CHECK(!ps2r::app::UserData::CarryOverLegacyRoot(root, &note) && note.empty() && !Exists(root + "/ps2r.ini"),
              "legacy_root_absent_nothing_copied");
        ::mkdir(legacy.c_str(), 0755);
        ::mkdir((legacy + "/memcards").c_str(), 0755);
        std::ofstream(legacy + "/ps2r.ini") << "video.vsync = on\n";
        std::ofstream(legacy + "/memcards/mc0.ps2") << "card";
        CHECK(ps2r::app::UserData::CarryOverLegacyRoot(root, &note) && note.find("copied the former root") != std::string::npos &&
              ReadWhole(root + "/ps2r.ini") == ReadWhole(legacy + "/ps2r.ini") && Exists(root + "/memcards/mc0.ps2") &&
              Exists(legacy + "/ps2r.ini") && Exists(legacy + "/memcards/mc0.ps2"),
              "legacy_root_copied_into_empty_root_source_kept");
        std::ofstream(legacy + "/later.txt") << "after";
        CHECK(!ps2r::app::UserData::CarryOverLegacyRoot(root, &note) && note.empty() && !Exists(root + "/later.txt"),
              "legacy_root_not_recopied_into_nonempty_root");
        CHECK(!ps2r::app::UserData::CarryOverLegacyRoot("", &note) && note.empty(), "legacy_root_empty_root_noop");
    }
    if (g_fail) { std::printf("test_disc_verify: %d FAILURE(S)\n", g_fail); return 1; }
    std::printf("test_disc_verify: all checks passed\n");
    return 0;
}
