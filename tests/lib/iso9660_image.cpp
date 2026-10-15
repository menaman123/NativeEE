// Iso9660Image -- see the header. Layout of the image built:
//   sectors 0-15   system area (zeros)
//   sector 16      primary volume descriptor
//   sector 17      volume descriptor set terminator
//   sector 18      type L path table, 19 type M path table (root + dirs)
//   sector 20      root directory (one sector)
//   then           one sector per subdirectory, then the files (sector aligned)
#include "tests/lib/iso9660_image.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

namespace ps2r::testlib {
namespace {

constexpr uint32_t kSector = 2048;

void PutBoth32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF;
    p[4] = (v >> 24) & 0xFF; p[5] = (v >> 16) & 0xFF; p[6] = (v >> 8) & 0xFF; p[7] = v & 0xFF;
}
void PutBoth16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF;
    p[2] = (v >> 8) & 0xFF; p[3] = v & 0xFF;
}
void PutPadded(uint8_t* p, size_t n, const std::string& s) {
    std::memset(p, ' ', n);
    std::memcpy(p, s.data(), std::min(n, s.size()));
}

// One directory record; returns its length (even).
size_t Record(uint8_t* p, uint32_t lba, uint32_t size, bool dir, const std::string& id) {
    const size_t id_len = id.size();
    size_t len = 33 + id_len;
    if (len & 1) len++;
    std::memset(p, 0, len);
    p[0] = static_cast<uint8_t>(len);
    PutBoth32(p + 2, lba);
    PutBoth32(p + 10, size);
    p[18] = 126; p[19] = 1; p[20] = 1;   // 2026-01-01 00:00:00
    p[25] = dir ? 0x02 : 0x00;
    PutBoth16(p + 28, 1);   // volume sequence number
    p[32] = static_cast<uint8_t>(id_len);
    std::memcpy(p + 33, id.data(), id_len);
    return len;
}

std::string Upper(const std::string& s) {
    std::string o = s;
    for (char& c : o) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return o;
}

}  // namespace

void Iso9660Image::AddFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    File f;
    const size_t slash = path.find_first_of("/\\");
    if (slash == std::string::npos) {
        f.name = Upper(path);
    } else {
        f.dir = Upper(path.substr(0, slash));
        f.name = Upper(path.substr(slash + 1));
    }
    f.bytes = bytes;
    files_.push_back(std::move(f));
}

std::vector<uint8_t> Iso9660Image::Build() const {
    // Directories (sorted, one sector each) and their LBAs.
    std::vector<std::string> dirs;
    for (const File& f : files_)
        if (!f.dir.empty() && std::find(dirs.begin(), dirs.end(), f.dir) == dirs.end()) dirs.push_back(f.dir);
    std::sort(dirs.begin(), dirs.end());
    const uint32_t root_lba = 20;
    std::map<std::string, uint32_t> dir_lba;
    uint32_t next = root_lba + 1;
    for (const std::string& d : dirs) dir_lba[d] = next++;
    // Files: sector-aligned extents after the directories.
    std::vector<uint32_t> file_lba(files_.size());
    for (size_t i = 0; i < files_.size(); i++) {
        file_lba[i] = next;
        next += static_cast<uint32_t>((files_[i].bytes.size() + kSector - 1) / kSector);
        if (files_[i].bytes.empty()) next++;   // an empty file still owns a sector
    }
    const uint32_t total_sectors = next;
    std::vector<uint8_t> img(static_cast<size_t>(total_sectors) * kSector, 0);

    // PVD.
    uint8_t* pvd = img.data() + 16 * kSector;
    pvd[0] = 1;
    std::memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    PutPadded(pvd + 8, 32, "PLAYSTATION");
    PutPadded(pvd + 40, 32, volume_id_);
    PutBoth32(pvd + 80, total_sectors);
    PutBoth16(pvd + 120, 1);        // volume set size
    PutBoth16(pvd + 124, 1);        // volume sequence number
    PutBoth16(pvd + 128, kSector);  // logical block size
    // Path tables: a tiny L table at 18 and M table at 19 (root + dirs).
    std::vector<uint8_t> ptl, ptm;
    auto path_record = [](std::vector<uint8_t>& out, uint32_t lba, uint16_t parent, const std::string& id, bool msb) {
        const size_t id_len = id.empty() ? 1 : id.size();
        out.push_back(static_cast<uint8_t>(id_len));
        out.push_back(0);
        if (msb) { out.push_back(lba >> 24); out.push_back(lba >> 16); out.push_back(lba >> 8); out.push_back(lba); }
        else { out.push_back(lba); out.push_back(lba >> 8); out.push_back(lba >> 16); out.push_back(lba >> 24); }
        if (msb) { out.push_back(parent >> 8); out.push_back(parent); }
        else { out.push_back(parent); out.push_back(parent >> 8); }
        if (id.empty()) out.push_back(0); else out.insert(out.end(), id.begin(), id.end());
        if (out.size() & 1) out.push_back(0);
    };
    path_record(ptl, root_lba, 1, "", false);
    path_record(ptm, root_lba, 1, "", true);
    for (const std::string& d : dirs) { path_record(ptl, dir_lba[d], 1, d, false); path_record(ptm, dir_lba[d], 1, d, true); }
    PutBoth32(pvd + 132, static_cast<uint32_t>(ptl.size()));
    pvd[140] = 18; pvd[148] = 0; pvd[149] = 0; pvd[150] = 0; pvd[151] = 19;   // L table LBA (LE), M table LBA (BE)
    std::memcpy(img.data() + 18 * kSector, ptl.data(), ptl.size());
    std::memcpy(img.data() + 19 * kSector, ptm.data(), ptm.size());
    // Root record in the PVD (34 bytes at 156).
    Record(pvd + 156, root_lba, kSector, true, std::string(1, '\0'));
    PutPadded(pvd + 190, 128, "SYNTHETIC");
    PutPadded(pvd + 318, 128, "NATIVEEE TESTS");
    PutPadded(pvd + 446, 128, "");
    PutPadded(pvd + 574, 128, "");
    // Terminator.
    uint8_t* term = img.data() + 17 * kSector;
    term[0] = 255;
    std::memcpy(term + 1, "CD001", 5);
    term[6] = 1;

    // Root directory: ".", "..", the subdirectories, the root files (sorted).
    auto fill_dir = [&](uint32_t lba, uint32_t parent_lba, const std::string& dir) {
        uint8_t* p = img.data() + static_cast<size_t>(lba) * kSector;
        size_t pos = 0;
        pos += Record(p + pos, lba, kSector, true, std::string(1, '\0'));
        pos += Record(p + pos, parent_lba, kSector, true, std::string(1, '\1'));
        std::vector<size_t> order;
        if (dir.empty())
            for (const std::string& d : dirs) pos += Record(p + pos, dir_lba[d], kSector, true, d);
        for (size_t i = 0; i < files_.size(); i++)
            if (files_[i].dir == dir) order.push_back(i);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return files_[a].name < files_[b].name; });
        for (size_t i : order)
            pos += Record(p + pos, file_lba[i], static_cast<uint32_t>(files_[i].bytes.size()), false, files_[i].name + ";1");
    };
    fill_dir(root_lba, root_lba, "");
    for (const std::string& d : dirs) fill_dir(dir_lba[d], root_lba, d);
    for (size_t i = 0; i < files_.size(); i++)
        std::memcpy(img.data() + static_cast<size_t>(file_lba[i]) * kSector, files_[i].bytes.data(), files_[i].bytes.size());
    return img;
}

uint64_t Iso9660Image::LogicalBytes() const { return Build().size(); }

bool Iso9660Image::Write(const std::string& out_path, bool raw2352, std::string* error) const {
    const std::vector<uint8_t> img = Build();
    std::FILE* f = std::fopen(out_path.c_str(), "wb");
    if (!f) { if (error) *error = "cannot write " + out_path; return false; }
    bool ok = true;
    if (!raw2352) {
        ok = std::fwrite(img.data(), 1, img.size(), f) == img.size();
    } else {
        static const uint8_t kSync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
        uint8_t raw[2352];
        const uint32_t sectors = static_cast<uint32_t>(img.size() / kSector);
        for (uint32_t s = 0; s < sectors && ok; s++) {
            std::memset(raw, 0, sizeof raw);
            std::memcpy(raw, kSync, 12);
            const uint32_t frame = s + 150;   // MSF of LBA 0 is 00:02:00
            const uint32_t mm = frame / (60 * 75), ss = (frame / 75) % 60, ff = frame % 75;
            auto bcd = [](uint32_t v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); };
            raw[12] = bcd(mm); raw[13] = bcd(ss); raw[14] = bcd(ff);
            raw[15] = 1;   // Mode 1
            std::memcpy(raw + 16, img.data() + static_cast<size_t>(s) * kSector, kSector);
            ok = std::fwrite(raw, 1, sizeof raw, f) == sizeof raw;
        }
    }
    if (std::fclose(f) != 0) ok = false;
    if (!ok && error) *error = "short write to " + out_path;
    return ok;
}

}  // namespace ps2r::testlib
