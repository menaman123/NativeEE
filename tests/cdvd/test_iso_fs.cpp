// IsoFsAndReadEngine: the ISO9660 walker over the frozen disc image (read-only)
// and the sector/file read service on top of it.
//   * the PVD parses (volume id, root extent);
//   * SLUS_123.45 and DATA/SYS/STREAM.IRX resolve (with/without ";1", either
//     separator, the cdrom0: prefix, any case) to entries whose sizes match the
//     frozen game_files copies byte for byte (whole-file extraction);
//   * positioned reads (start / middle / tail-partial) equal the whole-file slices;
//   * ReadEngine: fd table (open/read/seek/close, EOF, bad fd), byte-exact sector
//     reads against a direct read of the image, short read past the image end.
// The image path is argv[1]; the game_files dir argv[2].
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "engine/cdvd/iso_fs.h"
#include "engine/cdvd/read_engine.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

static bool ReadWhole(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), {});
    return true;
}

static void CheckFile(const IsoFs& fs, ReadEngine& eng, const char* iso_path, const std::string& host_path, const char* tag) {
    IsoFs::Entry e;
    std::string n = std::string(tag) + "_resolves";
    CHECK(fs.Find(iso_path, e) && !e.directory, n.c_str());
    std::vector<uint8_t> host;
    if (!ReadWhole(host_path, host)) { std::printf("SKIP %s: no host copy at %s\n", tag, host_path.c_str()); return; }
    n = std::string(tag) + "_size_matches_host_copy";
    CHECK(e.size == host.size(), n.c_str());
    std::printf("  %s: lba %u size %u date %02u-%02u-%02u\n", iso_path, e.lba, e.size, e.date[0] + 1900, e.date[1], e.date[2]);
    // Whole-file through the fd path.
    const int32_t fd = eng.Open(iso_path, 0);
    std::vector<uint8_t> got(e.size + 16, 0xEE);
    int32_t total = 0;
    while (total < static_cast<int32_t>(e.size)) {
        const int32_t r = eng.Read(fd, got.data() + total, 0x3001);  // odd chunk: crosses sectors
        if (r <= 0) break;
        total += r;
    }
    n = std::string(tag) + "_whole_file_byte_exact";
    CHECK(total == static_cast<int32_t>(e.size) && std::memcmp(got.data(), host.data(), e.size) == 0 && got[e.size] == 0xEE, n.c_str());
    CHECK(eng.Read(fd, got.data(), 16) == 0, (std::string(tag) + "_read_at_eof_is_zero").c_str());
    // Positioned reads: start / middle / tail-partial.
    uint8_t buf[4096];
    const uint64_t base = static_cast<uint64_t>(e.lba) * IsoFs::kSectorSize;
    bool pos_ok = fs.ReadBytes(base, 4096, buf) == 4096 && std::memcmp(buf, host.data(), 4096) == 0;
    const uint32_t mid = (e.size / 2) & ~3u;
    pos_ok &= fs.ReadBytes(base + mid, 1000, buf) == 1000 && std::memcmp(buf, host.data() + mid, 1000) == 0;
    const uint32_t tail = e.size - 777;
    pos_ok &= eng.Seek(fd, static_cast<int32_t>(tail), ReadEngine::kSeekSet) == static_cast<int32_t>(tail) &&
              eng.Read(fd, buf, 4096) == 777 && std::memcmp(buf, host.data() + tail, 777) == 0;
    CHECK(pos_ok, (std::string(tag) + "_positioned_reads_equal_slices").c_str());
    CHECK(eng.Close(fd) == 0 && eng.Close(fd) == -1, (std::string(tag) + "_close_then_bad_fd").c_str());
}

int main(int argc, char** argv) {
    if (argc < 3) { std::printf("usage: test_iso_fs <iso> <game_files_dir>\n"); return 2; }
    const std::string iso = argv[1], gf = argv[2];
    IsoFs fs;
    std::string err;
    CHECK(fs.Open(iso, &err), "iso_opens_and_pvd_parses");
    if (!fs.IsOpen()) { std::printf("  %s\n", err.c_str()); return 1; }
    std::printf("  volume '%s' root lba %u size %u sectors %u image %llu B\n", fs.VolumeId().c_str(), fs.RootLba(), fs.RootSize(),
                fs.VolumeSectors(), static_cast<unsigned long long>(fs.ImageBytes()));
    CHECK(fs.RootLba() > 16 && fs.RootSize() >= 34, "root_extent_sane");
    ReadEngine eng(fs);
    CheckFile(fs, eng, "SLUS_123.45;1", gf + "/example_title.rep/SLUS_123.45", "slus");
    CheckFile(fs, eng, "cdrom0:\\DATA\\SYS\\STREAM.IRX", gf + "/STREAM.IRX", "stream_irx");
    // Lookup variants resolve to the same extent.
    IsoFs::Entry a, b, c, d;
    CHECK(fs.Find("SLUS_123.45", a) && fs.Find("/slus_123.45;1", b) && fs.Find("cdrom0:SLUS_123.45;1", c) && a.lba == b.lba && b.lba == c.lba,
          "lookup_variants_same_extent");
    CHECK(!fs.Find("NOPE.BIN", d) && !fs.Find("DATA/SYS/NOPE.IRX", d) && !fs.Find("", d), "missing_paths_miss");
    CHECK(fs.Find("DATA/SYS", d) && d.directory, "directory_entry_flagged");
    CHECK(eng.Open("DATA/SYS", 0) == -1 && eng.Open("NOPE.BIN", 0) == -1, "open_directory_or_missing_fails");
    // Sector reads byte-exact against a direct read of the image.
    std::vector<uint8_t> sec(IsoFs::kSectorSize * 3), direct(IsoFs::kSectorSize * 3);
    const uint32_t lba = a.lba + 1;
    CHECK(eng.ReadSectors(lba, 3, sec.data()) == 3, "read_three_sectors");
    {
        std::ifstream in(iso, std::ios::binary);
        in.seekg(static_cast<std::streamoff>(lba) * IsoFs::kSectorSize);
        in.read(reinterpret_cast<char*>(direct.data()), static_cast<std::streamsize>(direct.size()));
    }
    CHECK(std::memcmp(sec.data(), direct.data(), sec.size()) == 0, "sectors_byte_exact_vs_image");
    const uint32_t last = static_cast<uint32_t>(fs.ImageBytes() / IsoFs::kSectorSize);
    CHECK(eng.ReadSectors(last - 1, 4, sec.data()) == 1 && eng.ReadSectors(last + 10, 1, sec.data()) == 0, "short_read_past_image_end");
    CHECK(eng.OpenFiles() == 0 && eng.SectorsRead() == 4, "census_open_files_and_sectors");
    std::printf("  sectors_read=%llu file_bytes_read=%llu\n", static_cast<unsigned long long>(eng.SectorsRead()),
                static_cast<unsigned long long>(eng.FileBytesRead()));
    std::printf(g_fail ? "FAILURES: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
