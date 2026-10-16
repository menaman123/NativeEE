// engine/iop/iop_library_table.cpp -- IopLibraryTable implementation.
#include "engine/iop/iop_library_table.h"

#include <utility>

namespace ps2r::iop {

void IopLibraryTable::RegisterHle(std::string_view lib, uint32_t version, uint32_t index, HleFn fn,
                                  std::string_view provider) {
    auto it = libs_.find(lib);
    if (it == libs_.end()) {
        Library l;
        l.name = std::string(lib);
        l.version = version;
        l.provider = std::string(provider);
        it = libs_.emplace(l.name, std::move(l)).first;
    } else if (it->second.version == 0) {
        it->second.version = version;
    }
    Library& l = it->second;
    if (l.entries.size() <= index) l.entries.resize(static_cast<size_t>(index) + 1);
    l.entries[index].hle = std::move(fn);
    l.entries[index].guest = 0;
}

bool IopLibraryTable::RegisterGuestTable(std::string_view lib, uint32_t version, const std::vector<uint32_t>& addrs,
                                         std::string_view provider) {
    if (libs_.find(lib) != libs_.end()) return false;
    Library l;
    l.name = std::string(lib);
    l.version = version;
    l.provider = std::string(provider);
    l.entries.resize(addrs.size());
    for (size_t i = 0; i < addrs.size(); ++i) l.entries[i].guest = addrs[i];
    libs_.emplace(l.name, std::move(l));
    return true;
}

bool IopLibraryTable::Release(std::string_view lib) {
    auto it = libs_.find(lib);
    if (it == libs_.end()) return false;
    libs_.erase(it);
    return true;
}

const IopLibraryTable::Library* IopLibraryTable::Find(std::string_view lib) const {
    auto it = libs_.find(lib);
    return it == libs_.end() ? nullptr : &it->second;
}

const IopLibraryTable::Entry* IopLibraryTable::FindEntry(std::string_view lib, uint32_t index) const {
    const Library* l = Find(lib);
    if (!l || index >= l->entries.size()) return nullptr;
    const Entry& e = l->entries[index];
    return e.Bound() ? &e : nullptr;
}

void IopLibraryTable::NoteUnresolved(std::string_view lib, uint32_t index) {
    std::string key(lib);
    key += ':';
    key += std::to_string(index);
    unresolved_[key]++;
    unresolved_total_++;
}

std::string IopLibraryTable::Census() const {
    std::string out;
    for (const auto& [name, l] : libs_) {
        if (!out.empty()) out += ',';
        size_t bound = 0;
        for (const Entry& e : l.entries) bound += e.Bound() ? 1 : 0;
        out += name + ":" + std::to_string(l.version) + "(" + std::to_string(bound) + ")";
    }
    return out;
}

std::string IopLibraryTable::UnresolvedCensus() const {
    std::string out;
    for (const auto& [key, n] : unresolved_) {
        if (!out.empty()) out += ',';
        out += key + "=" + std::to_string(n);
    }
    return out;
}

}  // namespace ps2r::iop
