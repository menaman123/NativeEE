#pragma once
// SdkBindingManifest (ARCHITECTURE 4.9, HOOK_RECLASSIFICATION 5): the ONE
// declarative table of SDK-library HLE rows and the ONE installer.
//
// Engine rows are keyed by (library, function name[, signature id]) and carry
// a handler -- NEVER an address. The address column is TITLE DATA:
// games/<title>/hle/sdk_bindings.tsv (`library<TAB>name<TAB>address<TAB>sig`)
// lists every signature match the sce-lib finder produced for that ELF; the
// resolver joins the two on (library, name) and the installer binds each
// resolved row through the injected install primitive (DispatchTable::Install
// in the composition root; a recording lambda in tests). A title row with no
// engine handler resolves to a TRAP body that parks (`park=sdk_unimplemented`)
// -- OLD installed a hard-error stub for the same case.
//
// Kinds: SdkBind replaces the generated body; SdkWrap receives the original
// body (the guarded-libc shape: the wrapper may fall back to it); Recompiled
// keeps the generated body AS the implementation (pure computation with no
// hardware or kernel behind it -- libkernl's printf engine), counted like any
// other row so `sdk_rows_hit_list` shows it ran. A Recompiled registration is
// the recorded decision that the row needs no HLE; an unregistered title row
// still traps.
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine/ee/cpu/cpu_state.h"

class SdkBindingManifest {
public:
    using Handler = void (*)(CpuContext&);
    using Body = std::function<void(CpuContext&, uint32_t)>;
    // Wrap handlers get the original body; return true when fully handled.
    using WrapHandler = bool (*)(CpuContext&);
    using Installer = std::function<bool(uint32_t addr, Body body)>;
    // Returns the body currently installed at addr (for SdkWrap capture); may be empty.
    using Capture = std::function<Body(uint32_t addr)>;

    enum class Kind { SdkBind, SdkWrap, Recompiled };

    struct EngineRow {
        std::string library;
        std::string name;
        Kind kind = Kind::SdkBind;
        Handler handler = nullptr;
        WrapHandler wrap = nullptr;
        bool set_pc_on_wrap = false;  // the wrapped registration simulated jr $ra
        // A WAIT body (SdkBind only): the handler parks the calling guest
        // thread in this kernel wait (TSW_*) and returns when woken, nothing
        // else. Installed with a continuation proof (entry = its start,
        // restart_wait = this) and a shadow frame, so a thread captured
        // asleep inside it is restartable: the relauncher re-enters the body,
        // which waits again. 0 = opaque body (no guest frame; a thread parked
        // inside it refuses capture by name).
        int restart_wait = 0;
    };
    struct TitleRow {
        std::string library;
        std::string name;
        uint32_t address = 0;
        std::string signature;  // reserved for the sce-lib finder's signature id
    };
    struct Resolved {
        TitleRow title;
        const EngineRow* engine = nullptr;  // null = trap row
    };

    static SdkBindingManifest& Instance();

    void Register(const std::string& library, const std::string& name, Handler h);
    void RegisterWrap(const std::string& library, const std::string& name, WrapHandler h,
                      bool set_pc_on_wrap);
    void RegisterRecompiled(const std::string& library, const std::string& name);
    // Replaces the handler of an already-registered SdkBind row (a later
    // phase's real body displacing an earlier phase's park owner, e.g. the
    // Phase-4 sceSif bodies over KernlSifRpc's rows -- D-SEQ-2 forbids
    // editing the park owner). False when no such row exists (nothing added).
    bool Rebind(const std::string& library, const std::string& name, Handler h);
    // Declares an SdkBind row a wait body (see EngineRow::restart_wait). False
    // when no such row exists.
    bool MarkWaitBody(const std::string& library, const std::string& name, int wait_type);
    // Every engine-side family registers its rows here (kernl_*, libscf, libdev, libc).
    void RegisterBuiltins();
    const std::vector<EngineRow>& EngineRows() const { return rows_; }
    const EngineRow* Find(const std::string& library, const std::string& name) const;

    // Title manifest: parse; comment lines start with '#'.
    static bool LoadTitleManifest(const std::string& path, std::vector<TitleRow>& out,
                                  std::string* error = nullptr);
    std::vector<Resolved> Resolve(const std::vector<TitleRow>& title) const;

    // Installs every resolved row in title-file order (OLD: the registry ran
    // last and won). Returns the number installed; `unresolved` counts rows the
    // installer rejected (address not a generated function start).
    // Wait bodies (EngineRow::restart_wait != 0) are handed to `wait_wrap` with
    // their wait type so the composition root can attach the dispatch proof
    // and the guest frame; without a wrapper they install as opaque bodies.
    using WaitWrapper = std::function<Body(uint32_t addr, Body body, int wait_type)>;
    int Install(const std::vector<Resolved>& rows, const Installer& install, const Capture& capture,
                int* trap_rows = nullptr, int* unresolved = nullptr, const WaitWrapper& wait_wrap = {}) const;

    void ResetForTest();

    // Evidence of installed rows being reached (run.meta `sdk_rows_hit*`):
    // total hits across every installed SdkBind/SdkWrap body since Install,
    // and the rows hit as "name@addr:count" in title-file order.
    uint64_t HitTotal() const;
    std::string HitList() const;

private:
    struct HitCell {
        std::string name;
        uint32_t address = 0;
        std::shared_ptr<std::atomic<uint64_t>> count;
    };
    SdkBindingManifest() = default;
    std::vector<EngineRow> rows_;
    mutable std::vector<HitCell> hits_;
};

// Single-TU row contributors (header = this file; one free function each).
void RegisterLibscfBindings(SdkBindingManifest& m);
// The RTC the libscf row reads (DerivedClocks::Rtc); null = the fixed epoch.
class DerivedClocks;
void SetLibscfClocks(const DerivedClocks* clocks);
void RegisterLibdevBindings(SdkBindingManifest& m);
