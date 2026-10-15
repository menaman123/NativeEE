#include "engine/dmac/dmac_handler_registry.h"

#include <algorithm>

// Index just past the last record of `cause` (records are grouped by cause
// ascending, so this is also where a smaller-or-equal cause's group ends).
size_t DmacHandlerRegistry::GroupEnd(int cause) const {
    size_t i = 0;
    while (i < table_.size() && table_[i].cause <= cause) ++i;
    return i;
}

int DmacHandlerRegistry::Add(int cause, uint32_t pc, uint32_t gp, uint32_t arg, int flag, int next) {
    if (cause < 0 || cause > kMaxCause) return -1;
    if (pc == 0) return -1;
    DmacHandler h;
    h.id = next_id_++;
    h.cause = cause;
    h.pc = pc;
    h.gp = gp;
    h.arg = arg;
    h.flag = flag;
    h.active = true;

    const size_t end = GroupEnd(cause);
    size_t begin = end;
    while (begin > 0 && table_[begin - 1].cause == cause) --begin;

    size_t at = end;
    if (next == 0) {
        at = begin;
    } else if (next != -1) {
        for (size_t i = begin; i < end; ++i) {
            if (table_[i].id == next) {
                at = i;
                break;
            }
        }
    }
    table_.insert(table_.begin() + static_cast<std::ptrdiff_t>(at), h);
    return h.id;
}

int DmacHandlerRegistry::Remove(int cause, int id) {
    if (cause < 0 || cause >= kDispatchCauses) return -1;
    const size_t before = table_.size();
    table_.erase(std::remove_if(table_.begin(), table_.end(),
                                [cause, id](const DmacHandler& h) { return h.cause == cause && h.id == id; }),
                 table_.end());
    if (table_.size() == before) return -1;
    int remaining = 0;
    for (const DmacHandler& h : table_)
        if (h.cause == cause) ++remaining;
    return remaining;
}

bool DmacHandlerRegistry::SetActive(int id, bool active) {
    for (DmacHandler& h : table_) {
        if (h.id == id) {
            h.active = active;
            return true;
        }
    }
    return false;
}

std::vector<DmacHandler> DmacHandlerRegistry::ForCause(int cause) const {
    std::vector<DmacHandler> out;
    for (const DmacHandler& h : table_)
        if (h.cause == cause) out.push_back(h);
    return out;
}

void DmacHandlerRegistry::Install(const std::vector<DmacHandler>& records, int next_id) {
    table_ = records;
    std::stable_sort(table_.begin(), table_.end(),
                     [](const DmacHandler& a, const DmacHandler& b) { return a.cause < b.cause; });
    next_id_ = next_id;
}

void DmacHandlerRegistry::Clear() {
    table_.clear();
    next_id_ = 1;
}
