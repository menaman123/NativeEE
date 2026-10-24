// engine/iop/iop_library_table.h -- IopLibraryTable: the IOP kernel's library
// registry (what loadcore keeps behind RegisterLibraryEntries) as the import
// resolver of every hosted module. A library is a name + version + an indexed
// entry list; an entry is either a host body (a stock Sony module's HLE) or a
// guest address (an export of a recompiled module, registered from its own
// 0x41C00000 table at RegisterLibraryEntries time). A recompiled module's
// `iop_stubs::import_call("lib", index, ctx)` resolves here: the body runs, or
// the guest function at the address runs through the bound resolver, or the
// import is UNRESOLVED (counted, reported, and handed to the composition's
// hook -- a park, never a silent 0). No title name or address lives here.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "engine/iop/iop_context.h"

namespace ps2r::iop {

class IopLibraryTable {
public:
    using HleFn = std::function<void(IopContext&)>;

    struct Entry {
        HleFn hle;            // a host body, or
        uint32_t guest = 0;   // a guest code address (0 = none)
        bool Bound() const { return static_cast<bool>(hle) || guest != 0; }
    };
    struct Library {
        std::string name;
        uint32_t version = 0;      // BCD major.minor as the table's version word
        std::string provider;      // who registered it ("hle:<class>" or the module name)
        std::vector<Entry> entries;
    };

    // Registers one host-bodied entry (creates the library on first use; a
    // version of 0 keeps the one already recorded).
    void RegisterHle(std::string_view lib, uint32_t version, uint32_t index, HleFn fn, std::string_view provider);
    // Registers a whole guest export table (index i -> addrs[i]; 0 = no entry).
    // False when a library of that name is already registered (loadcore's
    // KE_LIBRARY_FOUND shape): the table is left as it was.
    bool RegisterGuestTable(std::string_view lib, uint32_t version, const std::vector<uint32_t>& addrs,
                            std::string_view provider);
    // Drops a library (ReleaseLibraryEntries). False when unknown.
    bool Release(std::string_view lib);

    const Library* Find(std::string_view lib) const;
    // Every library whose entries are GUEST export tables (RegisterGuestTable): what a
    // savestate section records and re-registers; the HLE-bodied ones are the boot's.
    std::vector<const Library*> GuestLibraries() const;
    // The bound entry (lib, index), or nullptr (unknown library or index, or an unbound slot).
    const Entry* FindEntry(std::string_view lib, uint32_t index) const;
    size_t LibraryCount() const { return libs_.size(); }

    // The census of imports that resolved to nothing: "lib:idx" first seen, with counts.
    void NoteUnresolved(std::string_view lib, uint32_t index);
    uint64_t UnresolvedCount() const { return unresolved_total_; }
    // run.meta rows: "name:version(n)[,..]" and "lib:idx=count[,..]".
    std::string Census() const;
    std::string UnresolvedCensus() const;

private:
    std::map<std::string, Library, std::less<>> libs_;
    std::map<std::string, uint64_t, std::less<>> unresolved_;
    uint64_t unresolved_total_ = 0;
};

}  // namespace ps2r::iop
