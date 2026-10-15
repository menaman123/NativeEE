// engine/iop/savestate/iopm_section.cpp -- IopmSection implementation.
#include "engine/iop/savestate/iopm_section.h"

#include <cstring>

namespace ps2r::iop {

void IopmSection::MirrorKernel(IopKernel& kernel, Statics& s) {
    kernel.WithDispatchLock([&] {
        for (int i = 1; i < kThreads; i++) {
            const IopKernel::State st = kernel.ThreadState(i);
            const IopKernel::WaitKind wk = kernel.ThreadWaitKind(i);
            if (kernel.ThreadRegistered(i)) s.threads[i].priority = kernel.ThreadPriority(i);
            s.threads[i].active = (st != IopKernel::State::Dormant) && s.threads[i].started;
            s.threads[i].sleeping = (st == IopKernel::State::Wait && wk == IopKernel::WaitKind::Sleep);
        }
        for (int i = 0; i < kSemas; i++) {
            if (kernel.SemaActive(i)) s.semas[i].count = kernel.SemaCount(i);
        }
    });
}

void IopmSection::Serialize(std::vector<uint8_t>& out, const IopMemory& ram, const Statics& s) {
    using savestate::PutBytes;
    using savestate::PutPod;
    PutPod<uint32_t>(out, IopMemory::kSize);
    PutBytes(out, ram.Data(), IopMemory::kSize);
    PutBytes(out, &s.ctx, sizeof(IopContext));
    PutPod<uint32_t>(out, s.heap_ptr);
    PutPod<int32_t>(out, s.next_thread_id);
    PutPod<int32_t>(out, s.next_sema_id);
    PutPod<uint32_t>(out, s.cd_callback_addr);
    PutPod<uint64_t>(out, s.sys_time);
    PutPod<uint32_t>(out, static_cast<uint32_t>(kThreads));
    for (int i = 0; i < kThreads; i++) {
        const ThreadRec& t = s.threads[i];
        PutPod<uint32_t>(out, t.entry);
        PutPod<uint32_t>(out, t.stack_size);
        PutPod<uint32_t>(out, t.priority);
        PutPod<uint32_t>(out, t.arg);
        const uint8_t flags = static_cast<uint8_t>((t.active ? 1 : 0) | (t.started ? 2 : 0) |
                                                   (t.sleeping ? 4 : 0) | (t.terminated ? 8 : 0));
        PutPod<uint8_t>(out, flags);
    }
    PutPod<uint32_t>(out, static_cast<uint32_t>(kSemas));
    for (int i = 0; i < kSemas; i++) {
        const SemaRec& sm = s.semas[i];
        PutPod<int32_t>(out, sm.count);
        PutPod<int32_t>(out, sm.max_count);
        PutPod<uint8_t>(out, sm.active ? 1 : 0);
    }
}

bool IopmSection::ParseInto(savestate::PayloadReader& r, IopMemory* ram, Statics* s) {
    // One parse body for validate and restore: zero drift between the two.
    uint32_t ram_size = 0;
    if (!r.Get(&ram_size) || ram_size != IopMemory::kSize) return false;
    std::vector<uint8_t> ram_tmp;
    if (ram) {
        ram_tmp.resize(IopMemory::kSize);
        if (!r.GetBytes(ram_tmp.data(), IopMemory::kSize)) return false;
    } else if (!r.Skip(IopMemory::kSize)) {
        return false;
    }
    Statics tmp{};
    if (!r.GetBytes(&tmp.ctx, sizeof(IopContext))) return false;
    if (!r.Get(&tmp.heap_ptr) || !r.Get(&tmp.next_thread_id) || !r.Get(&tmp.next_sema_id) ||
        !r.Get(&tmp.cd_callback_addr) || !r.Get(&tmp.sys_time))
        return false;
    uint32_t nthreads = 0, nsemas = 0;
    if (!r.Get(&nthreads) || nthreads != static_cast<uint32_t>(kThreads)) return false;
    for (uint32_t i = 0; i < nthreads; i++) {
        ThreadRec& t = tmp.threads[i];
        uint8_t flags = 0;
        if (!r.Get(&t.entry) || !r.Get(&t.stack_size) || !r.Get(&t.priority) || !r.Get(&t.arg) ||
            !r.Get(&flags))
            return false;
        t.active = (flags & 1) != 0;
        t.started = (flags & 2) != 0;
        t.sleeping = (flags & 4) != 0;
        t.terminated = (flags & 8) != 0;
    }
    if (!r.Get(&nsemas) || nsemas != static_cast<uint32_t>(kSemas)) return false;
    for (uint32_t i = 0; i < nsemas; i++) {
        SemaRec& sm = tmp.semas[i];
        uint8_t active = 0;
        if (!r.Get(&sm.count) || !r.Get(&sm.max_count) || !r.Get(&active)) return false;
        sm.active = active != 0;
    }
    if (r.Remaining() != 0) return false;   // trailing garbage
    if (ram) std::memcpy(ram->Data(), ram_tmp.data(), IopMemory::kSize);
    if (s) *s = tmp;
    return true;
}

bool IopmSection::Validate(const std::vector<uint8_t>& payload) {
    savestate::PayloadReader r(payload);
    return ParseInto(r, nullptr, nullptr);
}

bool IopmSection::Parse(const std::vector<uint8_t>& payload, IopMemory& ram, Statics& s) {
    savestate::PayloadReader r(payload);
    return ParseInto(r, &ram, &s);
}

void IopmSection::ReconcileKernel(IopKernel& kernel, const Statics& s, uint64_t restored_vf) {
    kernel.WithDispatchLock([&] {
        for (int i = 0; i < kSemas; i++) {
            const SemaRec& sm = s.semas[i];
            if (sm.active) {
                if (!kernel.SemaActive(i)) kernel.RegisterSema(i, sm.count, sm.max_count);
                else kernel.SetSemaCount(i, sm.count);
            } else if (kernel.SemaActive(i)) {
                kernel.DeleteSema(i);
            }
        }
        for (int i = 1; i < kThreads; i++) {
            const uint32_t priority = s.threads[i].priority;
            // The existing IOPM priority word now carries the effective
            // priority. Requeue live threads before releasing restored waits.
            if (priority > 0 && priority <= 126 && kernel.ThreadRegistered(i) &&
                kernel.ThreadPriority(i) != priority)
                kernel.ChangePriority(i, priority);
            kernel.ReconcileThreadFromFlags(i, s.threads[i].sleeping, s.threads[i].terminated);
        }
        kernel.ReconcileSemaWaiters();
        kernel.DropPendingEvents();
        // FRAME clock rebase: the restored anchor's frame count becomes the clock's
        // frame base (a pure function of the anchor), forward-only in time; stale
        // pre-load dues expire at once and the next boundary advances the clock, so
        // a hard timer re-armed at the restored now (the module's heartbeat) beats.
        kernel.Clock().RebaseToFrame(restored_vf);
    });
}

savestate::SectionSpec IopmSection::MakeSpec(const Deps& deps) {
    savestate::SectionSpec spec;
    spec.tag = savestate::TAG_IOPM;
    spec.version = kVersion;
    spec.required = false;
    spec.owner = "iop";
    IopMemory* ram = deps.ram;
    Statics* statics = deps.statics;
    IopKernel* kernel = deps.kernel;
    std::function<uint64_t()> restored_vf = deps.restored_vf;
    std::function<void()> after_restore = deps.after_restore;
    spec.serialize = [ram, statics, kernel](std::vector<uint8_t>& out) {
        if (kernel) MirrorKernel(*kernel, *statics);
        Serialize(out, *ram, *statics);
    };
    spec.validate = [](const savestate::Blob& b, std::string* err) {
        if (b.version != kVersion) {
            if (err) *err = "IOPM: unsupported version";
            return false;
        }
        if (!Validate(b.payload)) {
            if (err) *err = "IOPM: malformed payload";
            return false;
        }
        return true;
    };
    spec.restore = [ram, statics, kernel, restored_vf, after_restore](const savestate::Blob& b,
                                                                       std::string* err) {
        Statics parsed{};
        if (!Parse(b.payload, *ram, parsed)) {
            if (err) *err = "IOPM: restore failed";
            return false;
        }
        // Never move the system-time clock backwards: an older loader keeps its own.
        if (parsed.sys_time < statics->sys_time) parsed.sys_time = statics->sys_time;
        *statics = parsed;
        if (kernel) ReconcileKernel(*kernel, *statics, restored_vf ? restored_vf() : 0);
        if (after_restore) after_restore();
        return true;
    };
    return spec;
}

}  // namespace ps2r::iop
