// engine/iop/savestate/iop_host_section.h -- IopHostSection: the IOPH savestate
// section (TAG_IOPH v1) of a HOSTED IOP -- recompiled modules the title brought
// up at RUN time on the cooperative kernel (loaded by the EE's
// sceSifLoadModuleBuffer, so no boot path of the restoring process re-creates
// them). Where the IOPM section (iopm_section.h) carries one boot-time module's
// RAM + kernel PODs and leans on the loader's boot to re-park its coroutines,
// IOPH carries everything a fresh process needs to stand the hosted IOP back up:
//
//   IOP RAM (2 MB) + the sysmem heap's bump tail + the kernel clock's frame
//   count; the kernel tables -- 32 threads (registered / started, initial +
//   effective priority, the thbase record: entry, $gp, heap stack, $a0, the
//   parked wait kind, banked wake tokens), 64 semaphores (count / max / order),
//   the event flags, the message boxes with their queued tokens; the timrman
//   hard timers; the module rows past the boot rows (the ids the EE holds);
//   the GUEST libraries (RegisterLibraryEntries tables); the sifcmd state (RPC
//   queue bindings + server records); the cdvdman callback; the sifman dma id;
//   the usbd drivers; the thsemap / thbase / thevent / thmsgbx id counters.
//
// Restore (the Crash contract, applied to a whole hosted IOP): the live kernel
// is reset (every coroutine destroyed, every object dropped), RAM and the heap
// tail land, the rows / libraries / kernel objects are rebuilt from the
// records, every started thread is RE-ARMED from its record on its recorded
// stack and $gp -- a fresh host coroutine runs the module's thread entry to its
// FIRST PARK on the restored RAM (the host stack of the saved coroutine cannot
// be rebuilt; the module's entry code reaching its wait is the re-park) -- the
// sifcmd records go to the composition's registrar (the EE-facing servers),
// the hard timers re-arm, the clock rebases to the restored frame, one dispatch
// pass runs the threads to their parks, then the reconcile: a thread the image
// shows awake is released from a Sleep park, banked tokens land, semaphore
// counts release FIFO waiters. The section is title-neutral: the title's IOP
// composition hands it the kernel-facing modules it built (Deps).
//
// Payload (little-endian): u32 ram_size, u8[ram_size]; u32 heap_ptr; u64 frames;
// u64 now_us; i32 rpc_tid; u32 nthreads(32) x {u8 flags, u32 initial_priority,
// u32 priority, u32 entry, u32 gp, u32 stack_base, u32 stack_size, u32 arg,
// u8 wait_kind, i32 wakeup_pending}; u32 nsemas(64) x {u8 active, i32 count,
// i32 max, u8 prio_order}; u32 nef x {u8 active, u8 multi, u32 value}; u32 nmbx x
// {u8 active, u32 nmsg, u32 msg[]}; u32 ntimers(4) x {u8 allocated, u8 running,
// u32 handler, arg, compare, gp, u64 remaining_us (the beat's phase)}; u32 boot_rows, u32 nrows x {str name, u32 version,
// i32 rpc_tid, u32 entry, gp, text_addr, text_size, data_size, bss_size}; u32 nlibs x
// {str name, u32 version, str provider, u32 n, u32 addr[]}; i32 rpc_queue_tid,
// u32 nqueues x {u32 queue, i32 tid}, u32 nservers x {u32 server_data, sid, func,
// buff, cfunc, cbuff, queue, i32 tid}; u32 cd_callback; u32 next_dma_id; u32
// ndrivers x u32; i32 next_sid, next_tid, ef_created, mbx_created.
// flags: bit0 registered, bit1 started. str = u16 length + bytes.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/core/savestate/section_registry.h"

namespace ps2r::iop {

class IopMemory;
class IopModuleTable;
class SysmemHeap;
namespace hle {
class ThbaseModule;
class ThsemapModule;
class TheventModule;
class ThmsgbxModule;
class TimrmanModule;
class SifcmdModule;
class SifmanModule;
class CdvdmanModule;
class LibsdModule;
class UsbdModule;
}  // namespace hle
class IopLibraryTable;

class IopHostSection {
public:
    static constexpr uint32_t kVersion = 1;

    struct Deps {
        IopMemory* ram = nullptr;
        IopModuleTable* table = nullptr;
        SysmemHeap* heap = nullptr;
        IopLibraryTable* libs = nullptr;
        hle::ThbaseModule* thbase = nullptr;
        hle::ThsemapModule* thsemap = nullptr;
        hle::TheventModule* thevent = nullptr;
        hle::ThmsgbxModule* thmsgbx = nullptr;
        hle::TimrmanModule* timrman = nullptr;
        hle::SifcmdModule* sifcmd = nullptr;
        hle::SifmanModule* sifman = nullptr;
        hle::CdvdmanModule* cdvdman = nullptr;
        hle::LibsdModule* libsd = nullptr;
        hle::UsbdModule* usbd = nullptr;
        // The module rows the composition registered at boot (never dropped by a restore).
        size_t boot_rows = 0;
        // The vsync frame of the image being restored (the clock's rebase target).
        std::function<uint64_t()> restored_vf;
        // After the kernel objects, threads and records landed, before the first
        // dispatch (the composition re-derives what it keeps beside the table).
        std::function<void()> after_restore;
    };

    explicit IopHostSection(Deps deps);
    IopHostSection(const IopHostSection&) = delete;
    IopHostSection& operator=(const IopHostSection&) = delete;

    savestate::SectionSpec MakeSpec();
    bool Register(savestate::SectionRegistry& registry) { return registry.Register(MakeSpec()); }

    void Serialize(std::vector<uint8_t>& out) const;
    // Parse-only check (no state touched).
    static bool Validate(const std::vector<uint8_t>& payload, std::string* err);
    bool Restore(const std::vector<uint8_t>& payload, std::string* err);

    // The last restore's census: threads re-armed, servers handed to the registrar, timers armed.
    struct RestoreCensus {
        uint32_t threads_armed = 0, threads_recorded = 0, servers = 0, timers_armed = 0, rows = 0, libraries = 0;
        uint32_t rpc_tid = 0;
        uint64_t frames = 0;
    };
    const RestoreCensus& LastRestore() const { return census_; }
    unsigned RestoredCount() const { return restored_; }

private:
    struct Image;
    static bool Parse(const std::vector<uint8_t>& payload, Image* image, std::string* err);
    bool Apply(const Image& image, std::string* err);

    Deps d_;
    RestoreCensus census_;
    unsigned restored_ = 0;
};

}  // namespace ps2r::iop
