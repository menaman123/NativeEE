// engine/iop/hle/cdvdman/cdvdman_module.cpp -- the drive's answers over the CdvdReader.
#include "engine/iop/hle/cdvdman/cdvdman_module.h"

#include <string>
#include <utility>

#include "engine/cdvd/cdvd_reader.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop::hle {

namespace {
constexpr uint32_t kCdReadComplete = 1;   // SCECdFuncRead: the callback's reason argument
}

void CdvdmanModule::SetCompletion(IopKernel* kernel, CallbackRunner run) {
    kernel_ = kernel;
    run_callback_ = std::move(run);
}

void CdvdmanModule::sceCdInit(IopContext& ctx) { ctx.gpr[2] = 1; }

// sceCdRead(lba, sectors, buf, mode): the sectors land in IOP RAM at buf inside
// the call; the completion (the registered callback, reason SCECdFuncRead) is a
// deferred kernel event at the drive timeline's end for this read when one is
// bound (one drive: seek + transfer from the head the EE's reads left), else
// kCompletionUs later. 0 = no disc, buffer outside IOP RAM, or the range off
// the image.
void CdvdmanModule::sceCdRead(IopContext& ctx) {
    const uint32_t lba = ctx.gpr[4], sectors = ctx.gpr[5], buf = ctx.gpr[6];
    if (!reader_ || sectors == 0) {
        ctx.gpr[2] = 0;
        return;
    }
    uint8_t* dst = mem_.Pointer(buf & IopMemory::kMask, static_cast<size_t>(sectors) * kSectorBytes);
    if (!dst) {
        ctx.gpr[2] = 0;
        return;
    }
    const uint32_t got = reader_->ReadSectors(lba, sectors, dst);
    if (got != sectors) {
        ctx.gpr[2] = 0;
        return;
    }
    reads_++;
    sectors_read_ += got;
    if (kernel_) {
        const uint64_t now = kernel_->Clock().NowUs();
        uint64_t due = now + kCompletionUs;
        if (completion_due_) {
            const uint64_t drive_end = completion_due_(lba, sectors, now);
            due = drive_end > now ? drive_end : now;   // the drive is never done before the issue
        }
        last_due_us_ = due;
        kernel_->ScheduleEventUs(due, [this] { FireCallback(kCdReadComplete); });
    }
    ctx.gpr[2] = 1;
}

void CdvdmanModule::FireCallback(uint32_t reason) {
    if (callback_addr_ == 0 || !run_callback_) return;
    IopContext cb;
    cb.gpr[4] = reason;
    callbacks_fired_++;
    run_callback_(callback_addr_, cb);
}

void CdvdmanModule::sceCdGetError(IopContext& ctx) { ctx.gpr[2] = 0; }

// sceCdSearchFile(file, path): sceCdlFILE {u32 lsn, u32 size, char name[16],
// u8 date[8]} at $a0 from the directory; the record is zeroed first so a miss
// never leaves stale fields. name = the last path component as recorded.
void CdvdmanModule::sceCdSearchFile(IopContext& ctx) {
    const uint32_t fp = ctx.gpr[4], path_addr = ctx.gpr[5];
    uint8_t* rec = mem_.Pointer(fp & IopMemory::kMask, kFileRecordBytes);
    if (rec) for (uint32_t i = 0; i < kFileRecordBytes; i++) rec[i] = 0;
    if (!reader_ || !rec) {
        ctx.gpr[2] = 0;
        return;
    }
    std::string path;
    for (uint32_t i = 0; i < 256; i++) {
        const char c = static_cast<char>(mem_.Read<uint8_t>((path_addr + i) & IopMemory::kMask));
        if (c == 0) break;
        path.push_back(c);
    }
    uint32_t lba = 0, size = 0;
    if (!reader_->FindFile(path.c_str(), &lba, &size)) {
        ctx.gpr[2] = 0;
        return;
    }
    search_hits_++;
    for (uint32_t i = 0; i < 4; i++) rec[i] = static_cast<uint8_t>(lba >> (8 * i));
    for (uint32_t i = 0; i < 4; i++) rec[4 + i] = static_cast<uint8_t>(size >> (8 * i));
    const size_t slash = path.find_last_of("\\/");
    const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
    for (uint32_t i = 0; i < 16; i++) rec[8 + i] = i < name.size() ? static_cast<uint8_t>(name[i]) : 0;
    ctx.gpr[2] = 1;
}

void CdvdmanModule::sceCdSync(IopContext& ctx) { ctx.gpr[2] = 0; }

void CdvdmanModule::sceCdGetDiskType(IopContext& ctx) { ctx.gpr[2] = kDiskTypePs2Dvd; }

void CdvdmanModule::sceCdDiskReady(IopContext& ctx) { ctx.gpr[2] = kDiskReadyComplete; }

void CdvdmanModule::sceCdCallback(IopContext& ctx) {
    const uint32_t previous = callback_addr_;
    callback_addr_ = ctx.gpr[4];
    ctx.gpr[2] = previous;
}

void CdvdmanModule::sceCdMmode(IopContext& ctx) { ctx.gpr[2] = 1; }

}  // namespace ps2r::iop::hle
