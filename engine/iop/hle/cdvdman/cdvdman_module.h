// engine/iop/hle/cdvdman/cdvdman_module.h -- CdvdmanModule: the 9 cdvdman
// entries STREAM.IRX imports, answered with the idle drive's results (a PS2 DVD
// is in the tray, no error, every sync complete). sceCdRead moves the sectors
// into IOP RAM inside the call through the CdvdReader and schedules the
// registered callback as a deferred kernel event kCompletionUs later (the
// drive completes asynchronously: issue -> return -> IRQ; the module never
// touches the buffer before the callback) -- or, when the owner binds a
// CompletionDue, at the instant that drive timeline answers (one DVD drive
// serves the EE's host-served reads and the module's own: the completion of a
// module read waits for the drive, seek and transfer from the same head
// position, instead of a fixed latency); sceCdSearchFile fills the sceCdlFILE
// record {lsn, size, name[16], date[8]} from the directory. Without a reader
// both report the original's no-disc results (0). sceCdCallback keeps the
// registered callback address (the IOPM section's cd_callback_addr).
#pragma once

#include <cstdint>
#include <functional>

#include "engine/iop/iop_context.h"

namespace ps2r::iop {
class IopMemory;
class IopKernel;
}

namespace ps2r::iop::hle {

}  // namespace ps2r::iop::hle
namespace ps2r::cdvd { class CdvdReader; }
namespace ps2r::iop::hle {
using ps2r::cdvd::CdvdReader;

class CdvdmanModule {
public:
    static constexpr uint32_t kDiskTypePs2Dvd = 0x14;   // SCECdPS2DVD
    static constexpr uint32_t kDiskReadyComplete = 2;   // SCECdComplete
    static constexpr uint32_t kSectorBytes = 2048;
    static constexpr uint32_t kFileRecordBytes = 32;    // sceCdlFILE {lsn, size, name[16], date[8]}
    static constexpr uint64_t kCompletionUs = 3000;     // issue -> completion callback
    // Runs the registered callback body (resolved from its guest address) with a context.
    using CallbackRunner = std::function<void(uint32_t addr, IopContext& ctx)>;
    // The drive timeline's answer for a read of `sectors` at `lba` issued at
    // IOP-clock `now_us`: the absolute IOP-clock microsecond the drive is done.
    using CompletionDue = std::function<uint64_t(uint32_t lba, uint32_t sectors, uint64_t now_us)>;

    CdvdmanModule(IopMemory& mem, CdvdReader* reader) : mem_(mem), reader_(reader) {}
    // The completion path: the kernel that schedules the deferred callback and the
    // runner that invokes it. Without both, a completed read fires no callback.
    void SetCompletion(IopKernel* kernel, CallbackRunner run);
    // Binds the one drive timeline (unbound: the fixed kCompletionUs latency).
    void SetCompletionDue(CompletionDue due) { completion_due_ = std::move(due); }
    uint64_t LastCompletionDueUs() const { return last_due_us_; }   // the last read's scheduled completion

    void sceCdInit(IopContext& ctx);         // (mode) -> 1
    void sceCdRead(IopContext& ctx);         // (lba, sectors, buf, mode) -> 1 issued / 0 no disc
    void sceCdGetError(IopContext& ctx);     // () -> 0 no error
    void sceCdSearchFile(IopContext& ctx);   // (file, path) -> 1 hit / 0 miss (no disc = 0)
    void sceCdSync(IopContext& ctx);         // (mode) -> 0 complete
    void sceCdGetDiskType(IopContext& ctx);  // () -> 0x14
    void sceCdDiskReady(IopContext& ctx);    // (mode) -> 2
    void sceCdCallback(IopContext& ctx);     // (fn) -> previous fn
    void sceCdMmode(IopContext& ctx);        // (mode) -> 1

    uint32_t CallbackAddr() const { return callback_addr_; }
    void SetCallbackAddr(uint32_t addr) { callback_addr_ = addr; }   // savestate restore
    uint32_t ReadsIssued() const { return reads_; }
    uint64_t SectorsRead() const { return sectors_read_; }
    uint32_t CallbacksFired() const { return callbacks_fired_; }
    uint32_t SearchHits() const { return search_hits_; }

private:
    void FireCallback(uint32_t reason);

    IopMemory& mem_;
    CdvdReader* reader_;
    IopKernel* kernel_ = nullptr;
    CallbackRunner run_callback_;
    CompletionDue completion_due_;
    uint64_t last_due_us_ = 0;
    uint32_t callback_addr_ = 0;
    uint32_t reads_ = 0;
    uint64_t sectors_read_ = 0;
    uint32_t callbacks_fired_ = 0;
    uint32_t search_hits_ = 0;
};

}  // namespace ps2r::iop::hle
