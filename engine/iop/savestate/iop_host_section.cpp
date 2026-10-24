// engine/iop/savestate/iop_host_section.cpp -- IopHostSection (iop_host_section.h).
#include "engine/iop/savestate/iop_host_section.h"

#include <cstring>
#include <utility>

#include "engine/core/savestate/payload_reader.h"
#include "engine/iop/hle/cdvdman/cdvdman_module.h"
#include "engine/iop/hle/libsd/libsd_module.h"
#include "engine/iop/hle/sifcmd/sifcmd_module.h"
#include "engine/iop/hle/sifman/sifman_module.h"
#include "engine/iop/hle/thbase/thbase_module.h"
#include "engine/iop/hle/thevent/thevent_module.h"
#include "engine/iop/hle/thmsgbx/thmsgbx_module.h"
#include "engine/iop/hle/thsemap/thsemap_module.h"
#include "engine/iop/hle/timrman/timrman_module.h"
#include "engine/iop/hle/usbd/usbd_module.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_library_table.h"
#include "engine/iop/iop_memory.h"
#include "engine/iop/iop_module_table.h"
#include "engine/iop/sysmem_heap.h"

namespace ps2r::iop {

namespace {

constexpr int kThreads = IopKernel::kMaxThreads;
constexpr int kSemas = IopKernel::kMaxSemas;
constexpr int kTimers = hle::TimrmanModule::kMaxTimers;
constexpr uint32_t kMaxTableEntries = 4096;   // a sanity bound on every variable table
constexpr uint8_t kFlagRegistered = 1, kFlagStarted = 2;

void PutStr(std::vector<uint8_t>& out, const std::string& s) {
    const uint16_t n = static_cast<uint16_t>(s.size() > 0xFFFF ? 0xFFFF : s.size());
    savestate::PutPod<uint16_t>(out, n);
    savestate::PutBytes(out, s.data(), n);
}

bool GetStr(savestate::PayloadReader& r, std::string* out) {
    uint16_t n = 0;
    if (!r.Get(&n)) return false;
    out->assign(n, '\0');
    return n == 0 || r.GetBytes(out->data(), n);
}

uint8_t WaitCode(IopKernel::WaitKind k) { return static_cast<uint8_t>(k); }

}  // namespace

// The parsed image: every table as the payload carries it.
struct IopHostSection::Image {
    struct Thread {
        bool registered = false;
        uint32_t initial_priority = 0, priority = 0;
        hle::ThbaseModule::ThreadRecord record;
        uint8_t wait_kind = 0;
        int32_t wakeup_pending = 0;
    };
    struct Sema {
        bool active = false, prio_order = false;
        int32_t count = 0, max_count = 0;
    };
    struct EventFlag {
        bool active = false, multi = false;
        uint32_t value = 0;
    };
    struct Mbx {
        bool active = false;
        std::vector<uint32_t> messages;
    };
    struct Library {
        std::string name, provider;
        uint32_t version = 0;
        std::vector<uint32_t> addrs;
    };
    std::vector<uint8_t> ram;
    uint32_t heap_ptr = 0;
    uint64_t frames = 0, now_us = 0;
    int32_t rpc_tid = 0;
    Thread threads[kThreads];
    Sema semas[kSemas];
    std::vector<EventFlag> event_flags;
    std::vector<Mbx> mbxs;
    hle::TimrmanModule::TimerRecord timers[kTimers + 1];
    uint32_t boot_rows = 0;
    std::vector<IopModuleTable::ModuleEntry> rows;
    std::vector<Library> libraries;
    hle::SifcmdModule::State sifcmd;
    uint32_t cd_callback = 0, next_dma_id = 1;
    std::vector<uint32_t> drivers;
    int32_t next_sid = 0, next_tid = 1, ef_created = 0, mbx_created = 0;
};

IopHostSection::IopHostSection(Deps deps) : d_(std::move(deps)) {}

void IopHostSection::Serialize(std::vector<uint8_t>& out) const {
    using savestate::PutPod;
    IopKernel& k = d_.table->Kernel();
    k.WithDispatchLock([&] {
        PutPod<uint32_t>(out, IopMemory::kSize);
        savestate::PutBytes(out, d_.ram->Data(), IopMemory::kSize);
        PutPod<uint32_t>(out, d_.heap->HeapPtr());
        PutPod<uint64_t>(out, k.Clock().Frames());
        PutPod<uint64_t>(out, k.Clock().NowUs());
        PutPod<int32_t>(out, d_.table->Rpc().RpcTid());
        PutPod<uint32_t>(out, static_cast<uint32_t>(kThreads));
        for (int tid = 0; tid < kThreads; tid++) {
            const hle::ThbaseModule::ThreadRecord rec = d_.thbase->ExportThread(tid);
            const bool registered = k.ThreadRegistered(tid);
            const uint8_t flags = static_cast<uint8_t>((registered ? kFlagRegistered : 0) | (rec.started ? kFlagStarted : 0));
            PutPod<uint8_t>(out, flags);
            PutPod<uint32_t>(out, k.ThreadInitialPriority(tid));
            PutPod<uint32_t>(out, k.ThreadPriority(tid));
            PutPod<uint32_t>(out, rec.entry);
            PutPod<uint32_t>(out, rec.gp);
            PutPod<uint32_t>(out, rec.stack_base);
            PutPod<uint32_t>(out, rec.stack_size);
            PutPod<uint32_t>(out, rec.arg);
            const IopKernel::WaitKind wait = k.ThreadState(tid) == IopKernel::State::Wait ? k.ThreadWaitKind(tid) : IopKernel::WaitKind::None;
            PutPod<uint8_t>(out, WaitCode(wait));
            PutPod<int32_t>(out, k.ThreadWakeupPending(tid));
        }
        PutPod<uint32_t>(out, static_cast<uint32_t>(kSemas));
        for (int sid = 0; sid < kSemas; sid++) {
            PutPod<uint8_t>(out, k.SemaActive(sid) ? 1 : 0);
            PutPod<int32_t>(out, k.SemaCount(sid));
            PutPod<int32_t>(out, k.SemaMaxCount(sid));
            PutPod<uint8_t>(out, k.SemaPrioOrder(sid) ? 1 : 0);
        }
        const int nef = k.EventFlagCount();
        PutPod<uint32_t>(out, static_cast<uint32_t>(nef));
        for (int ef = 0; ef < nef; ef++) {
            PutPod<uint8_t>(out, k.EventFlagActive(ef) ? 1 : 0);
            PutPod<uint8_t>(out, k.EventFlagMulti(ef) ? 1 : 0);
            PutPod<uint32_t>(out, k.EventFlagValue(ef));
        }
        const int nmbx = k.MbxCount();
        PutPod<uint32_t>(out, static_cast<uint32_t>(nmbx));
        for (int mbx = 0; mbx < nmbx; mbx++) {
            PutPod<uint8_t>(out, k.MbxActive(mbx) ? 1 : 0);
            const std::vector<void*> msgs = k.MbxMessages(mbx);
            PutPod<uint32_t>(out, static_cast<uint32_t>(msgs.size()));
            for (void* m : msgs) PutPod<uint32_t>(out, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m)));
        }
        PutPod<uint32_t>(out, static_cast<uint32_t>(kTimers));
        for (int id = 1; id <= kTimers; id++) {
            const hle::TimrmanModule::TimerRecord t = d_.timrman->ExportTimer(id);
            PutPod<uint8_t>(out, t.allocated ? 1 : 0);
            PutPod<uint8_t>(out, t.running ? 1 : 0);
            PutPod<uint32_t>(out, t.handler);
            PutPod<uint32_t>(out, t.arg);
            PutPod<uint32_t>(out, t.compare);
            PutPod<uint32_t>(out, t.gp);
            PutPod<uint64_t>(out, t.remaining_us);
        }
        const std::vector<IopModuleTable::ModuleEntry>& rows = d_.table->Modules();
        const size_t boot = d_.boot_rows < rows.size() ? d_.boot_rows : rows.size();
        PutPod<uint32_t>(out, static_cast<uint32_t>(boot));
        PutPod<uint32_t>(out, static_cast<uint32_t>(rows.size() - boot));
        for (size_t i = boot; i < rows.size(); i++) {
            const IopModuleTable::ModuleEntry& m = rows[i];
            PutStr(out, m.name);
            PutPod<uint32_t>(out, m.version);
            PutPod<int32_t>(out, m.rpc_tid);
            PutPod<uint32_t>(out, m.entry);
            PutPod<uint32_t>(out, m.gp);
            PutPod<uint32_t>(out, m.text_addr);
            PutPod<uint32_t>(out, m.text_size);
            PutPod<uint32_t>(out, m.data_size);
            PutPod<uint32_t>(out, m.bss_size);
        }
        const std::vector<const IopLibraryTable::Library*> libs = d_.libs->GuestLibraries();
        PutPod<uint32_t>(out, static_cast<uint32_t>(libs.size()));
        for (const IopLibraryTable::Library* lib : libs) {
            PutStr(out, lib->name);
            PutPod<uint32_t>(out, lib->version);
            PutStr(out, lib->provider);
            PutPod<uint32_t>(out, static_cast<uint32_t>(lib->entries.size()));
            for (const IopLibraryTable::Entry& e : lib->entries) PutPod<uint32_t>(out, e.guest);
        }
        const hle::SifcmdModule::State sif = d_.sifcmd->ExportState();
        PutPod<int32_t>(out, sif.rpc_queue_tid);
        PutPod<uint32_t>(out, static_cast<uint32_t>(sif.queues.size()));
        for (const auto& q : sif.queues) {
            PutPod<uint32_t>(out, q.first);
            PutPod<int32_t>(out, q.second);
        }
        PutPod<uint32_t>(out, static_cast<uint32_t>(sif.servers.size()));
        for (const hle::SifcmdModule::ServerRecord& s : sif.servers) {
            PutPod<uint32_t>(out, s.server_data);
            PutPod<uint32_t>(out, s.sid);
            PutPod<uint32_t>(out, s.func);
            PutPod<uint32_t>(out, s.buff);
            PutPod<uint32_t>(out, s.cfunc);
            PutPod<uint32_t>(out, s.cbuff);
            PutPod<uint32_t>(out, s.queue);
            PutPod<int32_t>(out, s.tid);
        }
        PutPod<uint32_t>(out, d_.cdvdman->CallbackAddr());
        PutPod<uint32_t>(out, d_.sifman->NextDmaId());
        const std::vector<uint32_t>& drivers = d_.usbd->Drivers();
        PutPod<uint32_t>(out, static_cast<uint32_t>(drivers.size()));
        for (uint32_t a : drivers) PutPod<uint32_t>(out, a);
        PutPod<int32_t>(out, d_.thsemap->NextSid());
        PutPod<int32_t>(out, d_.thbase->NextTid());
        PutPod<int32_t>(out, d_.thevent->Created());
        PutPod<int32_t>(out, d_.thmsgbx->Created());
    });
}

bool IopHostSection::Parse(const std::vector<uint8_t>& payload, Image* img, std::string* err) {
    auto fail = [&](const char* what) {
        if (err) *err = std::string("IOPH: ") + what;
        return false;
    };
    savestate::PayloadReader r(payload);
    uint32_t ram_size = 0;
    if (!r.Get(&ram_size) || ram_size != IopMemory::kSize) return fail("RAM size");
    img->ram.resize(IopMemory::kSize);
    if (!r.GetBytes(img->ram.data(), IopMemory::kSize)) return fail("RAM bytes");
    if (!r.Get(&img->heap_ptr) || !r.Get(&img->frames) || !r.Get(&img->now_us) || !r.Get(&img->rpc_tid)) return fail("heap/clock");
    uint32_t n = 0;
    if (!r.Get(&n) || n != static_cast<uint32_t>(kThreads)) return fail("thread table size");
    for (int tid = 0; tid < kThreads; tid++) {
        Image::Thread& t = img->threads[tid];
        uint8_t flags = 0;
        if (!r.Get(&flags) || !r.Get(&t.initial_priority) || !r.Get(&t.priority) || !r.Get(&t.record.entry) ||
            !r.Get(&t.record.gp) || !r.Get(&t.record.stack_base) || !r.Get(&t.record.stack_size) || !r.Get(&t.record.arg) ||
            !r.Get(&t.wait_kind) || !r.Get(&t.wakeup_pending))
            return fail("thread record");
        t.registered = (flags & kFlagRegistered) != 0;
        t.record.started = (flags & kFlagStarted) != 0;
    }
    if (!r.Get(&n) || n != static_cast<uint32_t>(kSemas)) return fail("sema table size");
    for (int sid = 0; sid < kSemas; sid++) {
        Image::Sema& s = img->semas[sid];
        uint8_t active = 0, prio = 0;
        if (!r.Get(&active) || !r.Get(&s.count) || !r.Get(&s.max_count) || !r.Get(&prio)) return fail("sema record");
        s.active = active != 0;
        s.prio_order = prio != 0;
    }
    if (!r.Get(&n) || n > kMaxTableEntries) return fail("event flag table");
    img->event_flags.resize(n);
    for (Image::EventFlag& ef : img->event_flags) {
        uint8_t active = 0, multi = 0;
        if (!r.Get(&active) || !r.Get(&multi) || !r.Get(&ef.value)) return fail("event flag record");
        ef.active = active != 0;
        ef.multi = multi != 0;
    }
    if (!r.Get(&n) || n > kMaxTableEntries) return fail("mbx table");
    img->mbxs.resize(n);
    for (Image::Mbx& m : img->mbxs) {
        uint8_t active = 0;
        uint32_t nmsg = 0;
        if (!r.Get(&active) || !r.Get(&nmsg) || nmsg > kMaxTableEntries) return fail("mbx record");
        m.active = active != 0;
        m.messages.resize(nmsg);
        for (uint32_t& msg : m.messages)
            if (!r.Get(&msg)) return fail("mbx message");
    }
    if (!r.Get(&n) || n != static_cast<uint32_t>(kTimers)) return fail("timer table size");
    for (int id = 1; id <= kTimers; id++) {
        hle::TimrmanModule::TimerRecord& t = img->timers[id];
        uint8_t allocated = 0, running = 0;
        if (!r.Get(&allocated) || !r.Get(&running) || !r.Get(&t.handler) || !r.Get(&t.arg) || !r.Get(&t.compare) || !r.Get(&t.gp) ||
            !r.Get(&t.remaining_us))
            return fail("timer record");
        t.allocated = allocated != 0;
        t.running = running != 0;
    }
    if (!r.Get(&img->boot_rows) || !r.Get(&n) || n > kMaxTableEntries) return fail("module rows");
    img->rows.resize(n);
    for (IopModuleTable::ModuleEntry& m : img->rows) {
        if (!GetStr(r, &m.name) || !r.Get(&m.version) || !r.Get(&m.rpc_tid) || !r.Get(&m.entry) || !r.Get(&m.gp) ||
            !r.Get(&m.text_addr) || !r.Get(&m.text_size) || !r.Get(&m.data_size) || !r.Get(&m.bss_size))
            return fail("module row");
    }
    if (!r.Get(&n) || n > kMaxTableEntries) return fail("library table");
    img->libraries.resize(n);
    for (Image::Library& lib : img->libraries) {
        uint32_t ne = 0;
        if (!GetStr(r, &lib.name) || !r.Get(&lib.version) || !GetStr(r, &lib.provider) || !r.Get(&ne) || ne > kMaxTableEntries)
            return fail("library record");
        lib.addrs.resize(ne);
        for (uint32_t& a : lib.addrs)
            if (!r.Get(&a)) return fail("library entry");
    }
    if (!r.Get(&img->sifcmd.rpc_queue_tid) || !r.Get(&n) || n > kMaxTableEntries) return fail("rpc queues");
    img->sifcmd.queues.resize(n);
    for (auto& q : img->sifcmd.queues)
        if (!r.Get(&q.first) || !r.Get(&q.second)) return fail("rpc queue record");
    if (!r.Get(&n) || n > kMaxTableEntries) return fail("rpc servers");
    img->sifcmd.servers.resize(n);
    for (hle::SifcmdModule::ServerRecord& s : img->sifcmd.servers) {
        if (!r.Get(&s.server_data) || !r.Get(&s.sid) || !r.Get(&s.func) || !r.Get(&s.buff) || !r.Get(&s.cfunc) ||
            !r.Get(&s.cbuff) || !r.Get(&s.queue) || !r.Get(&s.tid))
            return fail("rpc server record");
    }
    if (!r.Get(&img->cd_callback) || !r.Get(&img->next_dma_id)) return fail("cdvdman/sifman");
    if (!r.Get(&n) || n > kMaxTableEntries) return fail("usbd drivers");
    img->drivers.resize(n);
    for (uint32_t& a : img->drivers)
        if (!r.Get(&a)) return fail("usbd driver");
    if (!r.Get(&img->next_sid) || !r.Get(&img->next_tid) || !r.Get(&img->ef_created) || !r.Get(&img->mbx_created))
        return fail("id counters");
    if (r.Remaining() != 0) return fail("trailing bytes");
    return true;
}

bool IopHostSection::Validate(const std::vector<uint8_t>& payload, std::string* err) {
    Image img;
    return Parse(payload, &img, err);
}

bool IopHostSection::Apply(const Image& img, std::string* err) {
    IopModuleTable& table = *d_.table;
    IopKernel& k = table.Kernel();
    RestoreCensus census;
    // 1. The live kernel goes: every coroutine, object, timer, event and queued body.
    k.ResetForRestore();
    d_.thbase->ResetRecords();
    table.Rpc().ResetQueues();
    d_.libsd->ResetPendingSequence();
    // 2. Memory: RAM + the heap's bump tail (the free list is transient).
    std::memcpy(d_.ram->Data(), img.ram.data(), IopMemory::kSize);
    d_.heap->RestoreHeapPtr(img.heap_ptr);
    // 3. The module rows past the boot rows (the ids the EE holds are row indices).
    table.TruncateModules(d_.boot_rows);
    for (const IopModuleTable::ModuleEntry& m : img.rows) table.RegisterModule(m);
    census.rows = static_cast<uint32_t>(img.rows.size());
    // 4. The guest libraries (export tables in the restored RAM).
    for (const Image::Library& lib : img.libraries) {
        d_.libs->Release(lib.name);
        if (d_.libs->RegisterGuestTable(lib.name, lib.version, lib.addrs, lib.provider)) ++census.libraries;
    }
    // 5. Kernel objects, then the threads re-armed from their records (first park on the restored RAM).
    k.WithDispatchLock([&] {
        for (int sid = 0; sid < kSemas; sid++) {
            const Image::Sema& s = img.semas[sid];
            if (!s.active) continue;
            const int32_t max = s.max_count > 0 ? s.max_count : (s.count > 0 ? s.count : 1);
            const int32_t count = s.count < 0 ? 0 : (s.count > max ? max : s.count);
            k.RegisterSema(sid, count, max, s.prio_order);
        }
        for (size_t ef = 0; ef < img.event_flags.size(); ef++)
            k.RestoreEventFlag(static_cast<int>(ef), img.event_flags[ef].active, img.event_flags[ef].multi, img.event_flags[ef].value);
        for (size_t mbx = 0; mbx < img.mbxs.size(); mbx++) {
            std::vector<void*> tokens;
            for (uint32_t m : img.mbxs[mbx].messages) tokens.push_back(reinterpret_cast<void*>(static_cast<uintptr_t>(m)));
            k.RestoreMbx(static_cast<int>(mbx), img.mbxs[mbx].active, tokens);
        }
        for (int tid = 1; tid < kThreads; tid++) {
            const Image::Thread& t = img.threads[tid];
            if (!t.registered) continue;
            ++census.threads_recorded;
            if (d_.thbase->RestoreThreadRecord(tid, t.record, t.initial_priority, t.priority)) ++census.threads_armed;
        }
        d_.thbase->SetNextTid(img.next_tid);
        d_.thsemap->SetNextSid(img.next_sid);
        d_.thevent->SetCreated(img.ef_created);
        d_.thmsgbx->SetCreated(img.mbx_created);
    });
    // 6. The RPC registration state -> the composition's registrar (the EE-facing servers).
    d_.sifcmd->ImportState(img.sifcmd, /*notify=*/true);
    census.servers = static_cast<uint32_t>(img.sifcmd.servers.size());
    // 7. The device statics, then the hard timers (armed under the restored-timer due rule).
    d_.cdvdman->SetCallbackAddr(img.cd_callback);
    d_.sifman->SetNextDmaId(img.next_dma_id);
    d_.usbd->SetDrivers(img.drivers);
    // 8. The clock: the image's frame count is this clock's (forward only).
    k.Clock().RebaseToFrame(img.frames);
    for (int id = 1; id <= kTimers; id++)
        if (d_.timrman->RestoreTimerRecord(id, img.timers[id])) ++census.timers_armed;
    census.frames = img.frames;
    census.rpc_tid = static_cast<uint32_t>(img.rpc_tid);
    census_ = census;
    ++restored_;
    if (d_.after_restore) d_.after_restore();
    // 9. The first dispatch: every re-armed thread runs its entry to its first park.
    table.Rpc().SetRpcTid(img.rpc_tid);
    if (!table.Booted()) table.OnModuleLoaded(img.rpc_tid);
    else k.Dispatch();
    // 10. Reconcile the parks against the image: a thread the image shows awake is
    //     released from its Sleep park; banked wake tokens land; sema counts -> waiters.
    k.WithDispatchLock([&] {
        for (int tid = 1; tid < kThreads; tid++) {
            const Image::Thread& t = img.threads[tid];
            if (!t.registered || !t.record.started) continue;
            const bool sleeping = t.wait_kind == WaitCode(IopKernel::WaitKind::Sleep);
            k.ReconcileThreadFromFlags(tid, sleeping, /*terminated=*/false);
            if (t.wakeup_pending > 0) k.SetThreadWakeupPending(tid, t.wakeup_pending);
        }
        k.ReconcileSemaWaiters();
    });
    if (err) err->clear();
    return true;
}

bool IopHostSection::Restore(const std::vector<uint8_t>& payload, std::string* err) {
    Image img;
    if (!Parse(payload, &img, err)) return false;
    return Apply(img, err);
}

savestate::SectionSpec IopHostSection::MakeSpec() {
    savestate::SectionSpec spec;
    spec.tag = savestate::TAG_IOPH;
    spec.version = kVersion;
    spec.required = true;
    spec.owner = "iop";
    IopHostSection* self = this;
    spec.serialize = [self](std::vector<uint8_t>& out) { self->Serialize(out); };
    spec.validate = [](const savestate::Blob& b, std::string* err) {
        if (b.version != kVersion) {
            if (err) *err = "IOPH: unsupported version";
            return false;
        }
        return Validate(b.payload, err);
    };
    spec.restore = [self](const savestate::Blob& b, std::string* err) { return self->Restore(b.payload, err); };
    return spec;
}

}  // namespace ps2r::iop
