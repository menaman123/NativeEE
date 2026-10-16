// engine/iop/hle/ioman/ioman_module.h -- IomanModule: the IOP ioman library
// as a hosted module sees it: open (idx 4), close (5), read (6), lseek (8), each
// forwarded to the bound CdvdReader with the path/buffer resolved in IOP RAM;
// write (7) and the extended entries answer -1 (no writable device, no such
// operation).
// With no reader bound every call returns -1 (the 4D disc reader binds one).
#pragma once

#include <cstdint>

#include "engine/cdvd/cdvd_reader.h"
#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop::hle {
using ps2r::cdvd::CdvdReader;

class IomanModule {
public:
    static constexpr uint32_t kMaxPathBytes = 256;

    IomanModule(IopMemory& mem, CdvdReader* reader) : mem_(mem), reader_(reader) {}

    void SetReader(CdvdReader* reader) { reader_ = reader; }
    bool HasReader() const { return reader_ != nullptr; }

    void iop_open(IopContext& ctx);    // int open(const char* path, int flags) -> fd or -1
    void iop_close(IopContext& ctx);   // int close(int fd) -> 0 or -1
    void iop_read(IopContext& ctx);    // int read(int fd, void* buf, u32 size) -> bytes or -1
    void iop_lseek(IopContext& ctx);   // int lseek(int fd, int offset, int whence) -> position or -1
    void iop_write(IopContext& ctx);   // int write(int fd, const void* buf, u32 size) -> -1 (no writable device)
    // The ioman 1.04 entries no device here serves (ioctl 9, remove 10 .. format 18, AddDrv 20,
    // DelDrv 21, rename 25 .. ioctl2 34, devctl 31 among them): the "no such operation" result -1,
    // never a success the module did not earn.
    void iop_unsupported(IopContext& ctx);
    uint64_t UnsupportedCalls() const { return unsupported_calls_; }

private:
    IopMemory& mem_;
    CdvdReader* reader_;
    uint64_t unsupported_calls_ = 0;
};

}  // namespace ps2r::iop::hle
