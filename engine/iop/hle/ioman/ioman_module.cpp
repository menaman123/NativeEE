// engine/iop/hle/ioman/ioman_module.cpp -- IomanModule: ioman over a CdvdReader.
#include "engine/iop/hle/ioman/ioman_module.h"

namespace ps2r::iop::hle {

namespace {
constexpr uint32_t kErr = 0xFFFFFFFFu;
}  // namespace

void IomanModule::iop_open(IopContext& ctx) {
    const uint32_t path_addr = ctx.gpr[4];
    const uint32_t flags = ctx.gpr[5];
    char path[kMaxPathBytes];
    uint32_t n = 0;
    for (; n + 1 < kMaxPathBytes; ++n) {
        const uint8_t c = mem_.Read<uint8_t>(path_addr + n);
        path[n] = static_cast<char>(c);
        if (!c) break;
    }
    path[n] = '\0';
    ctx.gpr[2] = reader_ ? static_cast<uint32_t>(reader_->Open(path, flags)) : kErr;
}

void IomanModule::iop_close(IopContext& ctx) {
    const int32_t fd = static_cast<int32_t>(ctx.gpr[4]);
    ctx.gpr[2] = reader_ ? static_cast<uint32_t>(reader_->Close(fd)) : kErr;
}

void IomanModule::iop_read(IopContext& ctx) {
    const int32_t fd = static_cast<int32_t>(ctx.gpr[4]);
    const uint32_t buf = ctx.gpr[5];
    const uint32_t size = ctx.gpr[6];
    uint8_t* dst = mem_.Pointer(buf, size);
    ctx.gpr[2] = (reader_ && dst) ? static_cast<uint32_t>(reader_->Read(fd, dst, size)) : kErr;
}

void IomanModule::iop_lseek(IopContext& ctx) {
    const int32_t fd = static_cast<int32_t>(ctx.gpr[4]);
    const int32_t offset = static_cast<int32_t>(ctx.gpr[5]);
    const uint32_t whence = ctx.gpr[6];
    ctx.gpr[2] = reader_ ? static_cast<uint32_t>(reader_->Seek(fd, offset, whence)) : kErr;
}

}  // namespace ps2r::iop::hle
