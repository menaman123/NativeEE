// engine/iop/savestate/iopm_section.h -- IopmSection: the IOPM savestate
// section codec (TAG_IOPM v1): IOP RAM (2 MB) + the shared inline-call
// IopContext + the kernel-facing statics (sysmem heap_ptr, id counters, the CD
// callback address, the thbase system time, the 32-entry thread table and the
// 64-entry semaphore table). The layout is the frozen one every anchor state
// carries; it is identical under both dispatch policies (PHASE4_PLAN 3.1 line
// 8): a state saved under FRAME restores under FRAME or REALTIME and back.
//
// Payload (little-endian):
//   u32 ram_size (= IopMemory::kSize)   u8[ram_size] IOP RAM   u8[140] IopContext
//   u32 heap_ptr   i32 next_thread_id   i32 next_sema_id   u32 cd_callback_addr
//   u64 sys_time   u32 kThreads (32)   32 x { u32 entry, stack_size, priority, arg; u8 flags }
//   u32 kSemas (64)   64 x { i32 count, max_count; u8 active }
//   flags: bit0 active, bit1 started, bit2 sleeping, bit3 terminated.
//
// Restore reconciles a live kernel against the restored PODs (boot-symmetric
// coroutines re-park from the same images): restored sema counts release FIFO
// waiters, a cleared sleeping flag releases a Sleep parkee, terminated threads
// are terminated, queued deferred events are dropped (in-flight hardware ops of
// the pre-load world), and the FRAME clock is rebased forward to the restored
// vsync frame x quantum -- a pure function of the anchor.
//
// NOTE (clock): only the clock's virtual time (NowUs) is rebased. Its frame
// counter (Frames()) keeps counting boundaries since boot; nothing reads it as
// an absolute vsync number (the FRAME ceiling is derived from NowUs), so a
// restored anchor's Frames() is "boundaries since this process booted", not vf.
//
// NOTE (statics consumers): the module plugin's `after_restore` applies the
// rest of the statics -- heap_ptr into the sysmem arena, the CD callback, the
// libsd pending voice-transfer sequences reset -- and re-creates the threads the
// image records as created/started that the loader's boot path never created
// (the saver's title started them through the RPC face after module init):
// each is armed from its record's entry and dispatched to its first park, so a
// worker the image shows waiting on a semaphore waits on that semaphore with
// the image's token count -- and re-arms the hard timers the image's module RAM
// records as started (the layout carries no timer record; the module's own
// descriptor words are the restore-time knowledge). The kernel reconcile above
// owns the live (boot-path) threads; the plugin owns the re-created ones.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/savestate/payload_reader.h"
#include "engine/core/savestate/section_registry.h"
#include "engine/iop/iop_context.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop {

class IopmSection {
public:
    static constexpr int kThreads = IopKernel::kMaxThreads;   // 32
    static constexpr int kSemas = IopKernel::kMaxSemas;       // 64
    static constexpr uint32_t kVersion = 1;

    struct ThreadRec {
        uint32_t entry = 0, stack_size = 0, priority = 0, arg = 0;
        bool active = false, started = false, sleeping = false, terminated = false;
    };
    struct SemaRec {
        int32_t count = 0, max_count = 0;
        bool active = false;
    };
    // The kernel-facing statics the section carries besides RAM.
    struct Statics {
        IopContext ctx{};
        uint32_t heap_ptr = 0;
        int32_t next_thread_id = 0;
        int32_t next_sema_id = 0;
        uint32_t cd_callback_addr = 0;
        uint64_t sys_time = 0;
        ThreadRec threads[kThreads]{};
        SemaRec semas[kSemas]{};
    };

    // Mirror the live kernel into the POD tables (thread active/sleeping from
    // the kernel state, sema counts from the kernel) under the dispatch lock.
    static void MirrorKernel(IopKernel& kernel, Statics& s);
    // Append the section payload.
    static void Serialize(std::vector<uint8_t>& out, const IopMemory& ram, const Statics& s);
    // Parse-only check of a payload (no state touched).
    static bool Validate(const std::vector<uint8_t>& payload);
    // Parse into ram + statics. False on a malformed payload (nothing written).
    static bool Parse(const std::vector<uint8_t>& payload, IopMemory& ram, Statics& s);
    // Reconcile a live kernel against restored statics; `restored_vf` rebases the
    // FRAME clock forward (no-op under REALTIME, never backwards).
    static void ReconcileKernel(IopKernel& kernel, const Statics& s, uint64_t restored_vf);

    // The registry spec (TAG_IOPM, optional: a pre-V2-final file keeps live
    // values). `restored_vf` supplies the CNTR-restored vsync frame.
    struct Deps {
        IopMemory* ram = nullptr;
        Statics* statics = nullptr;
        IopKernel* kernel = nullptr;   // optional
        std::function<uint64_t()> restored_vf;
        std::function<void()> after_restore;   // e.g. IopModuleTable::NotifyHost
    };
    static savestate::SectionSpec MakeSpec(const Deps& deps);

    // Fixed payload size for the layout above.
    static constexpr size_t kPayloadBytes =
        4 + IopMemory::kSize + sizeof(IopContext) + 4 + 4 + 4 + 4 + 8 + 4 +
        static_cast<size_t>(kThreads) * (4 * 4 + 1) + 4 + static_cast<size_t>(kSemas) * (4 + 4 + 1);

private:
    static bool ParseInto(savestate::PayloadReader& r, IopMemory* ram, Statics* s);
};

static_assert(sizeof(IopContext) == 140, "IopContext layout is frozen by the IOPM section");

}  // namespace ps2r::iop
