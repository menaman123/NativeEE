#include "engine/ee/savestate/intc_section.h"

#include <algorithm>

#include "engine/core/savestate/payload_reader.h"
#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/intc/intc.h"

using ps2r::savestate::Blob;
using ps2r::savestate::PayloadReader;
using ps2r::savestate::SectionSpec;

namespace {
void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    const uint8_t b[4] = {static_cast<uint8_t>(x), static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x >> 16),
                          static_cast<uint8_t>(x >> 24)};
    v.insert(v.end(), b, b + 4);
}
void PutI32(std::vector<uint8_t>& v, int32_t x) { PutU32(v, static_cast<uint32_t>(x)); }
}  // namespace

IntcSection::IntcSection(Intc& intc, SyscallTable& syscalls) : intc_(intc), syscalls_(syscalls) {}

void IntcSection::Register(ps2r::savestate::SectionRegistry& registry) { registry.Register(Spec()); }

SectionSpec IntcSection::Spec() {
    SectionSpec s;
    s.tag = ps2r::savestate::TAG_INTC;
    // required: stricter than the original's unchecked Find() (see KernelSections).
    s.owner = "IntcSection";
    s.serialize = [this](std::vector<uint8_t>& out) { Serialize(out); };
    s.validate = [](const Blob& b, std::string* err) { return Validate(b.payload, err); };
    s.restore = [this](const Blob& b, std::string* err) { return Restore(b.payload, err); };
    return s;
}

void IntcSection::Serialize(std::vector<uint8_t>& out) const {
    std::vector<SyscallTable::KernelHandler> handlers = syscalls_.IntcHandlers();
    std::stable_sort(handlers.begin(), handlers.end(),
                     [](const SyscallTable::KernelHandler& a, const SyscallTable::KernelHandler& b) {
                         return a.cause < b.cause;
                     });
    PutU32(out, intc_.Stat());
    PutU32(out, intc_.Mask());
    PutI32(out, syscalls_.NextIntcHandlerId());
    PutU32(out, static_cast<uint32_t>(handlers.size()));
    for (const SyscallTable::KernelHandler& h : handlers) {
        PutI32(out, h.id); PutI32(out, h.cause); PutU32(out, h.pc); PutU32(out, h.gp); PutU32(out, h.arg);
        PutI32(out, h.flag);
        out.push_back(h.active ? 1 : 0);
    }
}

bool IntcSection::Validate(const std::vector<uint8_t>& payload, std::string* err) {
    PayloadReader r(payload);
    uint32_t stat = 0, mask = 0, count = 0;
    int32_t next_id = 0;
    if (!r.Get(&stat) || !r.Get(&mask) || !r.Get(&next_id) || !r.Get(&count) || count > kMaxHandlers ||
        r.Remaining() != static_cast<size_t>(count) * kRecordBytes) {
        if (err) *err = "INTC: bad header / payload length";
        return false;
    }
    return true;
}

bool IntcSection::Restore(const std::vector<uint8_t>& payload, std::string* err) {
    if (!Validate(payload, err)) return false;
    PayloadReader r(payload);
    uint32_t stat = 0, mask = 0, count = 0;
    int32_t next_id = 0;
    r.Get(&stat); r.Get(&mask); r.Get(&next_id); r.Get(&count);
    std::vector<SyscallTable::KernelHandler> handlers;
    for (uint32_t i = 0; i < count; i++) {
        SyscallTable::KernelHandler h;
        uint8_t active = 0;
        r.Get(&h.id); r.Get(&h.cause); r.Get(&h.pc); r.Get(&h.gp); r.Get(&h.arg); r.Get(&h.flag); r.Get(&active);
        h.active = active != 0;
        handlers.push_back(h);
    }
    intc_.RestoreRegisters(stat, mask);
    syscalls_.InstallIntcHandlers(handlers, next_id);
    return true;
}
