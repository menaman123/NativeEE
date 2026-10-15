#pragma once
// ReadEngine: the disc's sector/file read service over an IsoFs. It is the
// CdvdReader the IOP ioman/cdvdman modules park on (open/close/read/lseek on
// "cdrom0:" paths through a small fd table) and the sector reader behind the
// libcdvd rows (sceCdRead: LBA + sector count into a caller buffer;
// sceCdSearchFile: path -> LBA/size).
//
// Reads complete synchronously here: the completion-visibility model (when a
// completed read becomes visible to the EE -- boundary slot 4 in tick order,
// instant or latency-modelled) is the DeliveryMeter's, layered above this
// class; this class only moves bytes.
#include <cstdint>
#include <string>

#include "engine/cdvd/iso_fs.h"
#include "engine/cdvd/cdvd_reader.h"

class ReadEngine : public ps2r::cdvd::CdvdReader {
public:
    static constexpr int kMaxFiles = 16;
    enum Whence : uint32_t { kSeekSet = 0, kSeekCur = 1, kSeekEnd = 2 };

    explicit ReadEngine(const IsoFs& fs) : fs_(fs) {}

    // CdvdReader (ioman): fds are 1-based handles into the table.
    int32_t Open(const char* path, uint32_t flags) override;
    int32_t Close(int32_t fd) override;
    int32_t Read(int32_t fd, uint8_t* dst, uint32_t size) override;
    int32_t Seek(int32_t fd, int32_t offset, uint32_t whence) override;

    // Sector reads (sceCdRead, both sides): sectors read, short past the image end.
    uint32_t ReadSectors(uint32_t lba, uint32_t count, uint8_t* dst) override;
    // Path -> directory entry (sceCdSearchFile).
    bool Find(const std::string& path, IsoFs::Entry& out) const { return fs_.Find(path, out); }
    bool FindFile(const char* path, uint32_t* lba, uint32_t* size) const override;

    // Census.
    uint32_t OpenFiles() const;
    uint64_t SectorsRead() const { return sectors_read_; }
    uint64_t FileBytesRead() const { return file_bytes_read_; }

private:
    struct OpenFile {
        bool used = false;
        IsoFs::Entry entry;
        uint32_t pos = 0;
    };
    OpenFile* Slot(int32_t fd);

    const IsoFs& fs_;
    OpenFile files_[kMaxFiles];
    uint64_t sectors_read_ = 0;
    uint64_t file_bytes_read_ = 0;
};
