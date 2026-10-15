#pragma once
// IsoFs: an ISO9660 volume opened read-only from a disc image file.
//
// The PS2 disc (ECMA-119 / ISO9660 level 1, 2048-byte logical sectors): the
// Primary Volume Descriptor is at sector 16 (type 1, id "CD001"); its root
// directory record (offset 156) gives the root extent LBA and size; every
// directory is a run of directory records (length, extended-attr length,
// extent LBA both-endian at 2, data length both-endian at 10, recording date
// 7 bytes at 18, flags at 25, identifier length at 32, identifier at 33; a
// zero length byte skips to the next sector). Identifiers carry the ";1"
// version suffix; a lookup accepts the path with or without it, case
// insensitively, with '/' or '\' separators and an optional "cdrom0:" prefix.
//
// Sector reads address the image directly (byte offset = LBA * 2048) when the
// image is a plain 2048-bytes-per-sector ISO. A RAW image (.bin, 2352 bytes
// per sector: 12-byte sync 00 FF*10 00, 3-byte MSF address, mode byte, then
// the 2048 user bytes at +16 for Mode 1 or +24 for Mode 2 Form 1, then EDC/ECC)
// is recognised by its first sector's sync pattern; every logical read is
// then remapped per sector, so callers see one 2048-byte-per-sector volume
// whichever layout the file has (RawSectors() tells which).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class IsoFs {
public:
    static constexpr uint32_t kSectorSize = 2048;

    struct Entry {
        uint32_t lba = 0;
        uint32_t size = 0;
        bool directory = false;
        uint8_t date[7] = {};   // ISO9660 recording date (y-1900, m, d, h, mi, s, gmt)
        std::string name;       // identifier as recorded (with ";1")
    };

    IsoFs() = default;
    ~IsoFs();
    IsoFs(const IsoFs&) = delete;
    IsoFs& operator=(const IsoFs&) = delete;

    // Opens the image and parses the PVD. False (with *error) when the file is
    // missing or is not an ISO9660 volume.
    bool Open(const std::string& path, std::string* error = nullptr);
    bool IsOpen() const { return file_ != nullptr; }
    void Close();

    // Path lookup from the root, in any of the shapes a title uses:
    // "DATA/SYS/MODULE.IRX", "\SLUS_123.45;1", "cdrom0:\DATA\SYS\CDVDSTM.IRX".
    // False when any component is missing.
    bool Find(const std::string& path, Entry& out) const;
    // Every record of a directory except "." and ".." (names as recorded, with
    // ";1"). False when the directory does not read whole.
    bool List(const Entry& dir, std::vector<Entry>& out) const;
    Entry Root() const { return root_; }

    // Whole sectors: count sectors from lba into dst (count * 2048 bytes).
    // Returns the number of sectors read (short at the end of the image).
    uint32_t ReadSectors(uint32_t lba, uint32_t count, uint8_t* dst) const;
    // Bytes at an absolute image byte offset (positioned read within a file
    // = ReadBytes(entry.lba * 2048 + pos, ...)). Returns bytes read.
    size_t ReadBytes(uint64_t offset, size_t size, uint8_t* dst) const;

    uint32_t RootLba() const { return root_.lba; }
    uint32_t RootSize() const { return root_.size; }
    uint32_t VolumeSectors() const { return volume_sectors_; }
    // Logical image bytes (sectors * 2048): the size the volume presents, not
    // the file's byte count when the file is raw.
    uint64_t ImageBytes() const { return image_bytes_; }
    // True when the file is a raw 2352-byte-per-sector image (.bin).
    bool RawSectors() const { return sector_stride_ != kSectorSize; }
    uint32_t SectorStride() const { return sector_stride_; }
    std::string VolumeId() const { return volume_id_; }

private:
    bool FindInDirectory(const Entry& dir, const std::string& component, Entry& out) const;
    static bool IdentifierMatches(const std::string& recorded, const std::string& wanted);

    FILE* file_ = nullptr;
    uint64_t image_bytes_ = 0;
    uint64_t file_bytes_ = 0;
    uint32_t sector_stride_ = kSectorSize;   // 2048 (plain) or 2352 (raw)
    uint32_t user_offset_ = 0;               // raw: 16 (Mode 1) or 24 (Mode 2 Form 1)
    uint32_t volume_sectors_ = 0;
    Entry root_;
    std::string volume_id_;
};
