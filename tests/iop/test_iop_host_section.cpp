// tests/iop/test_iop_host_section.cpp -- IopHostSection: the IOPH savestate section of a
// hosted IOP round-trips into a FRESH composition (the cold --load-state shape) and back into
// the live one (the in-process F8 shape). A synthetic hosted IOP under the FRAME policy: two
// threads (one parked in SleepThread, one in WaitSema -- the RPC loop and a stream worker),
// a semaphore, an event flag, a message box with a queued token, a running hard timer, a
// guest library (RegisterLibraryEntries over an export table in RAM), two module rows past
// the boot rows, an RPC server on a queue (the registrar counted), the cdvdman callback, the
// sysmem heap tail, and RAM bytes. Checks: Validate accepts the payload and refuses a
// truncation; after Restore into fresh objects every table reads back, the threads are
// RE-ARMED from their records (fresh coroutines ran their entries to the recorded parks on
// the recorded stacks/$gp), the server reached the registrar, the timer beats, the clock
// frames rebased; a restore into the LIVE objects resets them first (no duplicate rows).
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "engine/iop/hle/cdvdman/cdvdman_module.h"
#include "engine/iop/hle/libsd/libsd_module.h"
#include "engine/iop/hle/loadcore/loadcore_module.h"
#include "engine/iop/hle/sifcmd/sifcmd_module.h"
#include "engine/iop/hle/sifman/sifman_module.h"
#include "engine/iop/hle/thbase/thbase_module.h"
#include "engine/iop/hle/thevent/thevent_module.h"
#include "engine/iop/hle/thmsgbx/thmsgbx_module.h"
#include "engine/iop/hle/thsemap/thsemap_module.h"
#include "engine/iop/hle/timrman/timrman_module.h"
#include "engine/iop/hle/usbd/usbd_module.h"
#include "engine/iop/iop_library_table.h"
#include "engine/iop/iop_module_table.h"
#include "engine/iop/savestate/iop_host_section.h"
#include "engine/iop/sysmem_heap.h"
#include "engine/platform/host/platform.h"
#include "engine/sound/spu_core.h"

using namespace ps2r::iop;
using namespace ps2r::iop::hle;

static int g_fail = 0;
#define CHECK(c, msg)                                                                          \
    do {                                                                                       \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); g_fail++; }      \
    } while (0)

namespace {

constexpr uint32_t kSleepEntry = 0x8D28, kWaitEntry = 0x8E00, kTimerHandler = 0x136C;
constexpr uint32_t kExportTable = 0x4000, kServerData = 0x7000, kQueue = 0xA000;

// One synthetic hosted IOP: the kernel-facing modules over one table + RAM, the section over them.
struct TestIopHostSection {
    IopModuleTable table{IopClock::Policy::Frame};
    IopMemory mem;
    SysmemHeap heap;
    IopLibraryTable libs;
    ps2r::sound::SpuCore spu;
    ThbaseModule thbase;
    ThsemapModule thsemap{table, mem};
    TheventModule thevent{table, mem};
    ThmsgbxModule thmsgbx{table, mem};
    TimrmanModule timrman;
    SifcmdModule sifcmd{table, mem};
    SifmanModule sifman{mem, nullptr};
    CdvdmanModule cdvdman{mem, nullptr};
    LibsdModule libsd{mem, spu};
    UsbdModule usbd{mem};
    LoadcoreModule loadcore{mem, libs};
    IopHostSection section;
    int sleeps = 0, waits = 0, beats = 0, registrar_calls = 0, after_restore = 0;
    uint32_t seen_sleep_sp = 0, seen_sleep_gp = 0, seen_wait_a0 = 0;
    int wait_sid = 0;

    static IopHostSection::Deps MakeDeps(TestIopHostSection* h) {
        IopHostSection::Deps d;
        d.ram = &h->mem;
        d.table = &h->table;
        d.heap = &h->heap;
        d.libs = &h->libs;
        d.thbase = &h->thbase;
        d.thsemap = &h->thsemap;
        d.thevent = &h->thevent;
        d.thmsgbx = &h->thmsgbx;
        d.timrman = &h->timrman;
        d.sifcmd = &h->sifcmd;
        d.sifman = &h->sifman;
        d.cdvdman = &h->cdvdman;
        d.libsd = &h->libsd;
        d.usbd = &h->usbd;
        d.boot_rows = 2;
        d.after_restore = [h] { ++h->after_restore; };
        return d;
    }
    ThbaseModule::FunctionResolver Resolver() {
        return [this](uint32_t addr) -> ThbaseModule::GuestFunction {
            if (addr == kSleepEntry)
                return [this](IopContext& c) {
                    seen_sleep_sp = c.gpr[29];
                    seen_sleep_gp = c.gpr[28];
                    IopContext k;
                    for (;;) { thbase.SleepThread(k); ++sleeps; }
                };
            if (addr == kWaitEntry)
                return [this](IopContext& c) {
                    seen_wait_a0 = c.gpr[4];
                    IopContext k;
                    k.gpr[4] = static_cast<uint32_t>(wait_sid);
                    for (;;) { thsemap.WaitSema(k); ++waits; }
                };
            if (addr == kTimerHandler) return [this](IopContext& c) { ++beats; c.gpr[2] = 1; };
            return {};
        };
    }
    TestIopHostSection()
        : thbase(table, mem, Resolver(), ThbaseModule::ThreadLayout{0, 0, 0}),
          timrman(table, Resolver(), TimrmanModule::TimerLayout{0x1FFC00, 0}),
          section(MakeDeps(this)) {
        thbase.SetStackAllocator([this](uint32_t size) { return heap.Alloc(size); }, [this](uint32_t addr) { heap.Free(addr); });
        sifcmd.SetRegistrar([this](const SifcmdModule::ServerRecord&) { ++registrar_calls; });
        table.RegisterModule("sysmem", 0x101, 0);   // the boot rows (never dropped)
        table.RegisterModule("thbase", 0x102, 0);
    }
};

// Builds the live state of the saver: what a module's entry + its threads leave behind.
void Populate(TestIopHostSection& h) {
    IopContext ctx;
    // RAM: a recognisable pattern + the export table of a guest library.
    for (uint32_t a = 0x1000; a < 0x1100; a += 4) h.mem.Write<uint32_t>(a, 0xA5000000u | a);
    h.mem.Write<uint32_t>(kExportTable, 0x41C00000u);           // the export magic
    h.mem.Write<uint32_t>(kExportTable + 0x08, 0x0102);          // version
    std::memcpy(h.mem.Pointer(kExportTable + 0x0C, 8), "mylib\0\0\0", 8);
    h.mem.Write<uint32_t>(kExportTable + 0x14, 0x9000);          // entry 0
    h.mem.Write<uint32_t>(kExportTable + 0x18, 0x9010);          // entry 1
    h.mem.Write<uint32_t>(kExportTable + 0x1C, 0);
    ctx.gpr[4] = kExportTable;
    h.loadcore.RegisterLibraryEntries(ctx);
    CHECK(ctx.gpr[2] == 0 && h.libs.Find("mylib") != nullptr, "the guest library registered");
    // Module rows past the boot rows.
    IopModuleTable::ModuleEntry row;
    row.name = "lgAud"; row.version = 0x10A; row.entry = 0x299F4; row.gp = 0x39260; row.text_addr = 0x28000;
    row.text_size = 0x5000; row.data_size = 0x800; row.bss_size = 0x400; row.rpc_tid = 0;
    h.table.RegisterModule(row);
    row.name = "spiop"; row.version = 0x100; row.entry = 0x5A30C; row.rpc_tid = 2;
    h.table.RegisterModule(row);
    // A semaphore (count 0, max 1), an event flag, a message box with one queued token.
    h.mem.Write<uint32_t>(0x3000 + 0x08, 0);
    h.mem.Write<uint32_t>(0x3000 + 0x0C, 1);
    ctx.gpr[4] = 0x3000;
    h.thsemap.CreateSema(ctx);
    h.wait_sid = static_cast<int>(ctx.gpr[2]);
    h.mem.Write<uint32_t>(0x3100 + 0x00, 0);        // attr
    h.mem.Write<uint32_t>(0x3100 + 0x08, 0x5);      // init pattern
    ctx.gpr[4] = 0x3100;
    h.thevent.CreateEventFlag(ctx);
    const int ef = static_cast<int>(ctx.gpr[2]) - 1;   // the guest id is 1-based
    ctx.gpr[4] = 0x3200;
    h.thmsgbx.CreateMbx(ctx);
    const uint32_t mbx = ctx.gpr[2];
    ctx.gpr[4] = mbx; ctx.gpr[5] = 0x7700;
    h.thmsgbx.SendMbx(ctx);
    CHECK(h.table.Kernel().EventFlagValue(ef) == 0x5, "event flag created with its pattern");
    // Two threads: entry + stack from the heap + $gp of the creator; started with an arg.
    auto create = [&](uint32_t entry, uint32_t prio, uint32_t gp) {
        h.mem.Write<uint32_t>(0x3300 + 0x08, entry);
        h.mem.Write<uint32_t>(0x3300 + 0x0C, 0x800);
        h.mem.Write<uint32_t>(0x3300 + 0x10, prio);
        ctx.gpr[4] = 0x3300;
        ctx.gpr[28] = gp;
        h.thbase.CreateThread(ctx);
        return static_cast<int>(ctx.gpr[2]);
    };
    const int t_sleep = create(kSleepEntry, 9, 0x39260);
    const int t_wait = create(kWaitEntry, 60, 0x67680);
    ctx.gpr[4] = static_cast<uint32_t>(t_sleep); ctx.gpr[5] = 0x1234;
    h.thbase.StartThread(ctx);
    ctx.gpr[4] = static_cast<uint32_t>(t_wait); ctx.gpr[5] = 0xBEEF;
    h.thbase.StartThread(ctx);
    // The RPC queue + server on the sleeping thread (its loop is the RPC service loop's shape).
    ctx.gpr[4] = kQueue; ctx.gpr[5] = static_cast<uint32_t>(t_sleep);
    h.sifcmd.sceSifSetRpcQueue(ctx);
    ctx.gpr[4] = kServerData; ctx.gpr[5] = 0x123456; ctx.gpr[6] = 0x3497C; ctx.gpr[7] = 0xD0400;
    ctx.gpr[29] = 0x1F000;
    h.mem.Write<uint32_t>(0x1F000 + 0x10, 0);
    h.mem.Write<uint32_t>(0x1F000 + 0x14, 0);
    h.mem.Write<uint32_t>(0x1F000 + 0x18, kQueue);
    h.sifcmd.sceSifRegisterRpc(ctx);
    // A running hard timer with the creator's $gp, the cdvdman callback, the usbd driver, a heap block.
    h.timrman.AllocHardTimer(ctx);
    ctx.gpr[4] = 1; ctx.gpr[5] = 614400; ctx.gpr[6] = kTimerHandler; ctx.gpr[7] = 0x14530;
    ctx.gpr[28] = 0x5A518;
    h.timrman.SetTimerHandler(ctx);
    ctx.gpr[4] = 1;
    h.timrman.StartHardTimer(ctx);
    h.cdvdman.SetCallbackAddr(0x5B000);
    h.usbd.SetDrivers({0x2A000});
    h.heap.Alloc(0x1000);
    // Boot: the threads run to their first parks; a few boundaries tick the timer and the clock.
    h.table.OnModuleLoaded(t_sleep);
    for (int i = 0; i < 5; ++i) h.table.PumpAtBoundary();
    CHECK(h.table.Kernel().ThreadWaitKind(t_sleep) == IopKernel::WaitKind::Sleep &&
          h.table.Kernel().ThreadWaitKind(t_wait) == IopKernel::WaitKind::Sema, "the saver's threads are parked (Sleep / Sema)");
    CHECK(h.beats >= 4, "the heartbeat beat on the saver");
}

bool SameTables(TestIopHostSection& a, TestIopHostSection& b, const char* tag) {
    bool ok = true;
    auto check = [&](bool c, const char* what) {
        if (!c) { std::printf("FAIL [%s] %s\n", tag, what); ok = false; }
    };
    check(std::memcmp(a.mem.Data(), b.mem.Data(), IopMemory::kSize) == 0, "RAM differs");
    check(a.heap.HeapPtr() == b.heap.HeapPtr(), "heap tail differs");
    check(a.table.ModuleCount() == b.table.ModuleCount() && a.table.ModulesCensus() == b.table.ModulesCensus(), "module rows differ");
    check(b.libs.Find("mylib") && b.libs.Find("mylib")->entries.size() == 2 && b.libs.Find("mylib")->entries[1].guest == 0x9010, "library differs");
    const IopKernel& ka = a.table.Kernel();
    const IopKernel& kb = b.table.Kernel();
    for (int sid = 0; sid < IopKernel::kMaxSemas; ++sid)
        check(ka.SemaActive(sid) == kb.SemaActive(sid) && ka.SemaCount(sid) == kb.SemaCount(sid) && ka.SemaMaxCount(sid) == kb.SemaMaxCount(sid), "sema differs");
    check(ka.EventFlagCount() == kb.EventFlagCount() && kb.EventFlagValue(0) == 0x5 && kb.EventFlagActive(0), "event flag differs");
    check(ka.MbxCount() == kb.MbxCount() && kb.MbxMessages(0).size() == 1 &&
          reinterpret_cast<uintptr_t>(kb.MbxMessages(0)[0]) == 0x7700, "mbx differs");
    for (int tid = 1; tid < IopKernel::kMaxThreads; ++tid) {
        check(ka.ThreadRegistered(tid) == kb.ThreadRegistered(tid), "thread registration differs");
        if (!ka.ThreadRegistered(tid)) continue;
        check(ka.ThreadPriority(tid) == kb.ThreadPriority(tid) && ka.ThreadInitialPriority(tid) == kb.ThreadInitialPriority(tid), "thread priority differs");
        check(ka.ThreadState(tid) == kb.ThreadState(tid) && ka.ThreadWaitKind(tid) == kb.ThreadWaitKind(tid), "thread park differs");
        const ThbaseModule::ThreadRecord ra = a.thbase.ExportThread(tid), rb = b.thbase.ExportThread(tid);
        check(ra.entry == rb.entry && ra.gp == rb.gp && ra.stack_base == rb.stack_base && ra.stack_size == rb.stack_size &&
              ra.arg == rb.arg && ra.started == rb.started, "thread record differs");
    }
    check(b.timrman.Running(1) && a.timrman.ExportTimer(1).gp == b.timrman.ExportTimer(1).gp, "timer differs");
    check(b.sifcmd.FindServer(0x123456) && b.sifcmd.FindServer(0x123456)->tid == a.sifcmd.FindServer(0x123456)->tid &&
          b.sifcmd.QueueTid(kQueue) == a.sifcmd.QueueTid(kQueue), "server record differs");
    check(b.cdvdman.CallbackAddr() == 0x5B000 && b.usbd.Drivers() == a.usbd.Drivers() && b.sifman.NextDmaId() == a.sifman.NextDmaId(), "device statics differ");
    check(b.thsemap.NextSid() == a.thsemap.NextSid() && b.thbase.NextTid() == a.thbase.NextTid(), "id counters differ");
    check(b.table.Kernel().Clock().Frames() == a.table.Kernel().Clock().Frames(), "clock frames differ");
    check(b.table.Rpc().RpcTid() == a.table.Rpc().RpcTid() && b.table.Booted(), "RPC tid / booted differ");
    return ok;
}

}  // namespace

int main() {
    ps2r::platform::host::RegisterSelf();   // the coroutines' fiber backend
    TestIopHostSection saver;
    Populate(saver);
    std::vector<uint8_t> payload;
    saver.section.Serialize(payload);
    CHECK(payload.size() > IopMemory::kSize, "the payload carries RAM + the tables");
    std::string err;
    CHECK(IopHostSection::Validate(payload, &err), ("Validate accepts the payload: " + err).c_str());
    {
        std::vector<uint8_t> cut(payload.begin(), payload.end() - 7);
        CHECK(!IopHostSection::Validate(cut, &err) && err.rfind("IOPH:", 0) == 0, "a truncated payload is refused by name");
        std::vector<uint8_t> grown = payload;
        grown.push_back(0);
        CHECK(!IopHostSection::Validate(grown, &err), "trailing bytes are refused");
    }
    // A section spec: version pinned, required, owner iop.
    const ps2r::savestate::SectionSpec spec = saver.section.MakeSpec();
    CHECK(spec.tag == ps2r::savestate::TAG_IOPH && spec.required && std::string(spec.owner) == "iop", "spec shape");

    // The cold shape: a FRESH composition (its boot rows only), the image restored before anything ran.
    TestIopHostSection fresh;
    CHECK(fresh.table.ModuleCount() == 2 && !fresh.table.Booted(), "the fresh host has its boot rows only");
    CHECK(fresh.section.Restore(payload, &err), ("Restore into the fresh host: " + err).c_str());
    CHECK(fresh.after_restore == 1 && fresh.registrar_calls == 1, "after_restore ran once; the server record reached the registrar once");
    const IopHostSection::RestoreCensus& c = fresh.section.LastRestore();
    CHECK(c.rows == 2 && c.threads_recorded == 2 && c.threads_armed == 2 && c.servers == 1 && c.timers_armed == 1 && c.libraries == 1,
          "restore census: 2 rows, 2/2 threads, 1 server, 1 timer, 1 library");
    CHECK(SameTables(saver, fresh, "cold"), "the fresh host's tables equal the saver's");
    // The heartbeat's phase: the restored beat is due the recorded remaining time after the restore.
    CHECK(saver.timrman.ExportTimer(1).remaining_us > 0 &&
          fresh.timrman.ExportTimer(1).remaining_us == saver.timrman.ExportTimer(1).remaining_us,
          "the restored timer keeps the recorded phase (remaining_us)");
    // The re-armed threads ran their entries on the RECORDED stacks / $gp with the recorded $a0.
    CHECK(fresh.seen_sleep_gp == 0x39260 && fresh.seen_wait_a0 == 0xBEEF, "re-armed threads: the recorded $gp and $a0");
    CHECK(fresh.seen_sleep_sp == saver.seen_sleep_sp, "re-armed thread: the recorded heap stack ($sp)");
    CHECK(fresh.sleeps == 0 && fresh.waits == 0, "re-armed threads parked at their first wait (no pass completed)");
    // The restored heartbeat beats on the following boundaries; a signal releases the restored waiter.
    const int beats0 = fresh.beats;
    for (int i = 0; i < 3; ++i) fresh.table.PumpAtBoundary();
    CHECK(fresh.beats > beats0, "the restored hard timer beats");
    IopContext ctx;
    ctx.gpr[4] = static_cast<uint32_t>(fresh.wait_sid);
    fresh.thsemap.iSignalSema(ctx);
    fresh.table.PumpAtBoundary();
    CHECK(fresh.waits == 1, "the restored WaitSema parkee is released by a signal");

    // The live shape: the same host restored again over its own live state (F8): no duplicate
    // rows / servers, the parks re-established, the second registrar call replaces the first.
    CHECK(fresh.section.Restore(payload, &err), ("Restore into the live host: " + err).c_str());
    CHECK(fresh.table.ModuleCount() == 4 && fresh.registrar_calls == 2, "a live restore resets the rows and re-registers the server once more");
    CHECK(fresh.table.Kernel().ThreadWaitKind(1) == IopKernel::WaitKind::Sleep &&
          fresh.table.Kernel().ThreadWaitKind(2) == IopKernel::WaitKind::Sema, "the live restore re-parked both threads");
    CHECK(fresh.section.RestoredCount() == 2, "two restores counted");
    fresh.table.Shutdown();
    saver.table.Shutdown();
    if (g_fail) { std::printf("IopHostSection: %d failure(s)\n", g_fail); return 1; }
    std::printf("IopHostSection: PASS\n");
    return 0;
}
