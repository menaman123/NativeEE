// engine/cdvd/cdvd_reader.h -- CdvdReader: the disc as a reader interface. Behind the IOP
// modules -- the ioman file calls (open/close/read/lseek on "cdrom0:" paths as
// STREAM.IRX issues them) and the cdvdman sector/directory calls (sceCdRead,
// sceCdSearchFile). The disc image reader (ReadEngine) binds it; without one
// the ioman calls fail with -1 and the cdvdman calls report no disc (0).
#pragma once

#include <cstdint>

namespace ps2r::cdvd {

class CdvdReader {
public:
    virtual ~CdvdReader() = default;
    virtual int32_t Open(const char* path, uint32_t flags) = 0;              // fd >= 0 or -1
    virtual int32_t Close(int32_t fd) = 0;                                    // 0 or -1
    virtual int32_t Read(int32_t fd, uint8_t* dst, uint32_t size) = 0;       // bytes or -1
    virtual int32_t Seek(int32_t fd, int32_t offset, uint32_t whence) = 0;   // new position or -1
    // Whole sectors from lba into dst: sectors read, short past the image end.
    virtual uint32_t ReadSectors(uint32_t lba, uint32_t count, uint8_t* dst) = 0;
    // Directory lookup of a "cdrom0:" / "\\DIR\\FILE;1" path: LBA + byte size on a hit.
    virtual bool FindFile(const char* path, uint32_t* lba, uint32_t* size) const = 0;
};

}  // namespace ps2r::cdvd
