// IsoFs implementation; the ISO9660 layout is described in the header.
#include "engine/cdvd/iso_fs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace {
// 64-bit positioning: the image is ~2 GB, past a 32-bit `long` on Windows.
#if defined(_WIN32)
int SeekImage(FILE* f, int64_t offset, int whence) { return _fseeki64(f, offset, whence); }
int64_t TellImage(FILE* f) { return _ftelli64(f); }
#else
int SeekImage(FILE* f, int64_t offset, int whence) { return fseeko(f, static_cast<off_t>(offset), whence); }
int64_t TellImage(FILE* f) { return static_cast<int64_t>(ftello(f)); }
#endif

constexpr uint32_t kPvdSector = 16;
constexpr uint32_t kRawSectorSize = 2352;
constexpr size_t kRootRecordOffset = 156;
constexpr size_t kVolumeIdOffset = 40;
constexpr size_t kVolumeSpaceOffset = 80;

uint32_t Le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

// Decodes one directory record at p (len = p[0], already checked > 32).
void DecodeRecord(const uint8_t* p, IsoFs::Entry& e) {
    e.lba = Le32(p + 2);
    e.size = Le32(p + 10);
    std::memcpy(e.date, p + 18, 7);
    e.directory = (p[25] & 0x02) != 0;
    const size_t id_len = p[32];
    e.name.assign(reinterpret_cast<const char*>(p + 33), id_len);
}

std::string StripPrefix(const std::string& path) {
    std::string s = path;
    const size_t colon = s.find(':');
    if (colon != std::string::npos && s.compare(0, 5, "cdrom") == 0) s = s.substr(colon + 1);
    return s;
}
}  // namespace

IsoFs::~IsoFs() { Close(); }

void IsoFs::Close() {
    if (file_) std::fclose(file_);
    file_ = nullptr;
    image_bytes_ = 0;
    file_bytes_ = 0;
    sector_stride_ = kSectorSize;
    user_offset_ = 0;
    volume_sectors_ = 0;
    root_ = Entry{};
    volume_id_.clear();
}

bool IsoFs::Open(const std::string& path, std::string* error) {
    Close();
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    SeekImage(file_, 0, SEEK_END);
    const int64_t end = TellImage(file_);
    file_bytes_ = end > 0 ? static_cast<uint64_t>(end) : 0;
    image_bytes_ = file_bytes_;
    // Raw (2352) layout: the first sector starts with the 12-byte sync pattern;
    // byte 15 is the mode (1: user data at +16; 2: Mode 2 Form 1, data at +24).
    {
        static const uint8_t kSync[12] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
        uint8_t head[16];
        SeekImage(file_, 0, SEEK_SET);
        if (file_bytes_ >= kRawSectorSize && std::fread(head, 1, 16, file_) == 16 && std::memcmp(head, kSync, 12) == 0) {
            sector_stride_ = kRawSectorSize;
            user_offset_ = head[15] == 2 ? 24 : 16;
            image_bytes_ = (file_bytes_ / kRawSectorSize) * kSectorSize;
        }
    }
    uint8_t pvd[kSectorSize];
    if (ReadSectors(kPvdSector, 1, pvd) != 1 || pvd[0] != 1 || std::memcmp(pvd + 1, "CD001", 5) != 0) {
        if (error) *error = path + ": no ISO9660 primary volume descriptor at sector 16";
        Close();
        return false;
    }
    volume_sectors_ = Le32(pvd + kVolumeSpaceOffset);
    volume_id_.assign(reinterpret_cast<const char*>(pvd + kVolumeIdOffset), 32);
    while (!volume_id_.empty() && volume_id_.back() == ' ') volume_id_.pop_back();
    const uint8_t* root = pvd + kRootRecordOffset;
    if (root[0] < 33) {
        if (error) *error = path + ": malformed root directory record";
        Close();
        return false;
    }
    DecodeRecord(root, root_);
    root_.name = "/";
    return true;
}

size_t IsoFs::ReadBytes(uint64_t offset, size_t size, uint8_t* dst) const {
    if (!file_ || size == 0) return 0;
    if (offset >= image_bytes_) return 0;
    const uint64_t avail = image_bytes_ - offset;
    const size_t n = avail < size ? static_cast<size_t>(avail) : size;
    if (!RawSectors()) {
        if (SeekImage(file_, static_cast<int64_t>(offset), SEEK_SET) != 0) return 0;
        return std::fread(dst, 1, n, file_);
    }
    // Raw: one positioned read per logical sector, skipping each sector's
    // header and trailer.
    size_t done = 0;
    while (done < n) {
        const uint64_t logical = offset + done;
        const uint64_t lba = logical / kSectorSize;
        const size_t in_sector = static_cast<size_t>(logical % kSectorSize);
        const size_t chunk = std::min(n - done, static_cast<size_t>(kSectorSize) - in_sector);
        const int64_t raw = static_cast<int64_t>(lba * sector_stride_ + user_offset_ + in_sector);
        if (SeekImage(file_, raw, SEEK_SET) != 0) break;
        const size_t got = std::fread(dst + done, 1, chunk, file_);
        done += got;
        if (got != chunk) break;
    }
    return done;
}

uint32_t IsoFs::ReadSectors(uint32_t lba, uint32_t count, uint8_t* dst) const {
    // Byte math in size_t: a sector count near 2^21 would overflow a 32-bit product.
    const size_t bytes = ReadBytes(static_cast<uint64_t>(lba) * kSectorSize, static_cast<size_t>(count) * kSectorSize, dst);
    return static_cast<uint32_t>(bytes / kSectorSize);
}

bool IsoFs::IdentifierMatches(const std::string& recorded, const std::string& wanted) {
    // Version suffix (";1") optional on the wanted side; case-insensitive.
    std::string rec = recorded;
    const size_t semi = rec.find(';');
    std::string rec_base = semi == std::string::npos ? rec : rec.substr(0, semi);
    auto ieq = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); i++)
            if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i]))) return false;
        return true;
    };
    return ieq(rec, wanted) || ieq(rec_base, wanted);
}

bool IsoFs::FindInDirectory(const Entry& dir, const std::string& component, Entry& out) const {
    if (!file_ || dir.size == 0) return false;
    const uint32_t sectors = (dir.size + kSectorSize - 1) / kSectorSize;
    std::vector<uint8_t> buf(static_cast<size_t>(sectors) * kSectorSize);
    if (ReadSectors(dir.lba, sectors, buf.data()) != sectors) return false;
    size_t pos = 0;
    const size_t end = dir.size;
    while (pos < end) {
        const uint8_t len = buf[pos];
        if (len == 0) {  // padding to the next sector
            pos = (pos / kSectorSize + 1) * kSectorSize;
            continue;
        }
        if (len < 33 || pos + len > buf.size()) return false;
        const uint8_t* p = buf.data() + pos;
        const size_t id_len = p[32];
        // "\0" and "\1" are the . and .. records.
        if (!(id_len == 1 && (p[33] == 0 || p[33] == 1))) {
            const std::string id(reinterpret_cast<const char*>(p + 33), id_len);
            if (IdentifierMatches(id, component)) {
                DecodeRecord(p, out);
                return true;
            }
        }
        pos += len;
    }
    return false;
}

bool IsoFs::List(const Entry& dir, std::vector<Entry>& out) const {
    out.clear();
    if (!file_ || dir.size == 0 || !dir.directory) return false;
    const uint32_t sectors = (dir.size + kSectorSize - 1) / kSectorSize;
    std::vector<uint8_t> buf(static_cast<size_t>(sectors) * kSectorSize);
    if (ReadSectors(dir.lba, sectors, buf.data()) != sectors) return false;
    size_t pos = 0;
    while (pos < dir.size) {
        const uint8_t len = buf[pos];
        if (len == 0) {
            pos = (pos / kSectorSize + 1) * kSectorSize;
            continue;
        }
        if (len < 33 || pos + len > buf.size()) return false;
        const uint8_t* p = buf.data() + pos;
        const size_t id_len = p[32];
        if (!(id_len == 1 && (p[33] == 0 || p[33] == 1))) {
            Entry e;
            DecodeRecord(p, e);
            out.push_back(e);
        }
        pos += len;
    }
    return true;
}

bool IsoFs::Find(const std::string& path, Entry& out) const {
    if (!file_) return false;
    const std::string s = StripPrefix(path);
    Entry cur = root_;
    size_t i = 0;
    bool any = false;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == '/' || s[i] == '\\')) i++;
        if (i >= s.size()) break;
        size_t j = i;
        while (j < s.size() && s[j] != '/' && s[j] != '\\') j++;
        const std::string component = s.substr(i, j - i);
        if (!cur.directory) return false;
        Entry next;
        if (!FindInDirectory(cur, component, next)) return false;
        cur = next;
        any = true;
        i = j;
    }
    if (!any) return false;
    out = cur;
    return true;
}
