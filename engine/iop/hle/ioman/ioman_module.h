// engine/iop/hle/ioman/ioman_module.h -- IomanModule: the IOP ioman library
// as STREAM.IRX sees it: open (idx 4), close (5), read (6), lseek (8), each
// forwarded to the bound CdvdReader with the path/buffer resolved in IOP RAM.
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

private:
    IopMemory& mem_;
    CdvdReader* reader_;
};

}  // namespace ps2r::iop::hle
