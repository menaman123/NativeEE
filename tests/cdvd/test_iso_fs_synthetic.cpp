// IsoFsSynthetic: IsoFs over a synthetic ISO9660 volume (tests/lib
// Iso9660Image) in both layouts -- plain 2048-byte sectors and raw 2352-byte
// (.bin, Mode 1) sectors. No disc image of the user's is read.
//   * both layouts open, parse the PVD (volume id) and report the layout;
//   * a root file and a DIR/file resolve; the bytes read back equal the input
//     (whole-file, mid-file positioned, tail-partial, across sector seams);
//   * the raw image presents the same logical size as the plain one;
//   * a non-ISO file (random bytes) and a missing file are refused with a reason.
// argv[1] = scratch dir.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/cdvd/iso_fs.h"
#include "tests/lib/iso9660_image.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

static std::vector<uint8_t> Pattern(size_t n, uint8_t seed) {
    std::vector<uint8_t> v(n);
    for (size_t i = 0; i < n; i++) v[i] = static_cast<uint8_t>(seed + i * 7 + (i >> 8));
    return v;
}

static void Exercise(const std::string& path, bool raw, const std::vector<uint8_t>& elf, const std::vector<uint8_t>& irx,
                     uint64_t logical_bytes) {
    const std::string tag = raw ? "raw" : "plain";
    IsoFs fs;
    std::string err;
    CHECK(fs.Open(path, &err), (tag + "_opens").c_str());
    if (!fs.IsOpen()) { std::printf("  %s\n", err.c_str()); return; }
    CHECK(fs.RawSectors() == raw, (tag + "_layout_detected").c_str());
    CHECK(fs.VolumeId() == "FAKE_TWIN", (tag + "_volume_id").c_str());
    CHECK(fs.ImageBytes() == logical_bytes, (tag + "_logical_bytes").c_str());
    IsoFs::Entry e;
    CHECK(fs.Find("SLUS_123.45;1", e) && !e.directory && e.size == elf.size(), (tag + "_root_file_resolves").c_str());
    std::vector<uint8_t> got(elf.size());
    CHECK(fs.ReadBytes(static_cast<uint64_t>(e.lba) * IsoFs::kSectorSize, got.size(), got.data()) == got.size() &&
          got == elf, (tag + "_root_file_bytes_equal").c_str());
    // Positioned: a 1000-byte slice straddling a sector seam, and the tail.
    uint8_t buf[4096];
    const uint64_t base = static_cast<uint64_t>(e.lba) * IsoFs::kSectorSize;
    bool pos_ok = fs.ReadBytes(base + 2000, 1000, buf) == 1000 && std::memcmp(buf, elf.data() + 2000, 1000) == 0;
    const size_t tail = elf.size() - 333;
    pos_ok &= fs.ReadBytes(base + tail, 333, buf) == 333 && std::memcmp(buf, elf.data() + tail, 333) == 0;
    CHECK(pos_ok, (tag + "_positioned_reads_equal_slices").c_str());
    IsoFs::Entry d;
    CHECK(fs.Find("cdrom0:\\DATA\\STREAM.IRX", d) && d.size == irx.size(), (tag + "_subdir_file_resolves").c_str());
    std::vector<uint8_t> got2(irx.size());
    CHECK(fs.ReadBytes(static_cast<uint64_t>(d.lba) * IsoFs::kSectorSize, got2.size(), got2.data()) == got2.size() &&
          got2 == irx, (tag + "_subdir_file_bytes_equal").c_str());
    IsoFs::Entry none;
    CHECK(!fs.Find("SLES_525.68", none), (tag + "_missing_file_refused").c_str());
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("usage: test_iso_fs_synthetic <scratch>\n"); return 2; }
    const std::string scratch = argv[1];
    const std::vector<uint8_t> elf = Pattern(5000, 0x7f), irx = Pattern(2048 * 3 + 17, 0x11);
    ps2r::testlib::Iso9660Image img;
    img.SetVolumeId("FAKE_TWIN");
    img.AddFile("SLUS_123.45", elf);
    img.AddFile("DATA/STREAM.IRX", irx);
    img.AddFile("SYSTEM.CNF", std::vector<uint8_t>{'B', 'O', 'O', 'T', '2', '\n'});
    const std::string plain = scratch + "/synthetic.iso", raw = scratch + "/synthetic.bin";
    std::string err;
    CHECK(img.Write(plain, false, &err), "write_plain");
    CHECK(img.Write(raw, true, &err), "write_raw");
    Exercise(plain, false, elf, irx, img.LogicalBytes());
    Exercise(raw, true, elf, irx, img.LogicalBytes());
    // Not ISO9660: a file of noise.
    const std::string noise = scratch + "/noise.bin";
    {
        std::FILE* f = std::fopen(noise.c_str(), "wb");
        const std::vector<uint8_t> n = Pattern(2048 * 40, 0x55);
        std::fwrite(n.data(), 1, n.size(), f);
        std::fclose(f);
    }
    IsoFs bad;
    CHECK(!bad.Open(noise, &err) && err.find("ISO9660") != std::string::npos, "noise_refused_with_reason");
    std::printf("  %s\n", err.c_str());
    CHECK(!bad.Open(scratch + "/does_not_exist.iso", &err) && err.find("cannot open") != std::string::npos, "missing_refused");
    if (g_fail) { std::printf("test_iso_fs_synthetic: %d FAILURE(S)\n", g_fail); return 1; }
    std::printf("test_iso_fs_synthetic: all checks passed\n");
    return 0;
}
