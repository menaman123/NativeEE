#include "engine/ee/hle/sdk_binding_manifest.h"

#include <cstdio>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "engine/ee/cpu/guest_park.h"
#include "engine/ee/hle/kernl_boot.h"
#include "engine/ee/hle/kernl_file_io.h"
#include "engine/ee/hle/kernl_sbrk.h"
#include "engine/ee/hle/kernl_sif_rpc.h"
#include "engine/ee/hle/kernl_timer.h"
#include "engine/ee/hle/libc.h"

SdkBindingManifest& SdkBindingManifest::Instance() {
    static SdkBindingManifest inst;
    return inst;
}

void SdkBindingManifest::Register(const std::string& library, const std::string& name, Handler h) {
    EngineRow r;
    r.library = library;
    r.name = name;
    r.kind = Kind::SdkBind;
    r.handler = h;
    rows_.push_back(std::move(r));
}

void SdkBindingManifest::RegisterWrap(const std::string& library, const std::string& name,
                                      WrapHandler h, bool set_pc_on_wrap) {
    EngineRow r;
    r.library = library;
    r.name = name;
    r.kind = Kind::SdkWrap;
    r.wrap = h;
    r.set_pc_on_wrap = set_pc_on_wrap;
    rows_.push_back(std::move(r));
}

void SdkBindingManifest::RegisterBuiltins() {
    KernlBoot::Register(*this);
    KernlTimer::Register(*this);
    KernlFileIo::Register(*this);
    KernlSifRpc::Register(*this);
    KernlSbrk::Register(*this);  // rebinds the sbrk row the park owner registered above
    RegisterLibscfBindings(*this);
    RegisterLibdevBindings(*this);
    // libc (HOOK 3.4): the wrapped HLE registrations simulated jr $ra; strlen's
    // registration was the plain recompiled body (pc untouched).
    RegisterWrap("libc.a", "memcpy", &Libc::Memcpy, true);
    RegisterWrap("libc.a", "memmove", &Libc::Memmove, true);
    RegisterWrap("libc.a", "memset", &Libc::Memset, true);
    RegisterWrap("libc.a", "strlen", &Libc::Strlen, false);
    // libkernl's printf engine (sceSnprintf -> _sceSnprintf_pf -> _sceSnprintf /
    // snprintf_char, with printfloat / ftoi): pure string formatting over EE
    // RAM, no hardware or kernel state -- the generated bodies are the
    // implementation. The original runtime ran these as recompiled (one
    // hand-restitched) bodies too; the title's save-path formatting reaches
    // them right after the memory-card check.
    for (const char* name : {"_sceSnprintf", "snprintf_char", "_sceSnprintf_pf", "sceSnprintf", "printfloat", "ftoi"})
        RegisterRecompiled("libkernl.a", name);
    // printf's character sink: kputchar -> deci2Putchar (a 126-byte line buffer
    // in libkernl's .data, flushed on '\n' or when full) -> kputs, which hands
    // the line to the kernel through a syscall the table already serves with
    // its retail result. Pure EE-RAM bookkeeping plus a syscall: the generated
    // bodies are the implementation (on a retail console the text goes nowhere).
    for (const char* name : {"kputchar", "deci2Putchar", "kputs"}) RegisterRecompiled("libkernl.a", name);
}

void SdkBindingManifest::RegisterRecompiled(const std::string& library, const std::string& name) {
    EngineRow r;
    r.library = library;
    r.name = name;
    r.kind = Kind::Recompiled;
    rows_.push_back(std::move(r));
}

bool SdkBindingManifest::MarkWaitBody(const std::string& library, const std::string& name, int wait_type) {
    for (EngineRow& r : rows_) {
        if (r.name == name && r.library == library && r.kind == Kind::SdkBind) {
            r.restart_wait = wait_type;
            return true;
        }
    }
    return false;
}

bool SdkBindingManifest::Rebind(const std::string& library, const std::string& name, Handler h) {
    for (EngineRow& r : rows_) {
        if (r.name == name && r.library == library && r.kind == Kind::SdkBind) {
            r.handler = h;
            return true;
        }
    }
    return false;
}

const SdkBindingManifest::EngineRow* SdkBindingManifest::Find(const std::string& library,
                                                             const std::string& name) const {
    for (const EngineRow& r : rows_)
        if (r.name == name && (r.library.empty() || r.library == library)) return &r;
    return nullptr;
}

bool SdkBindingManifest::LoadTitleManifest(const std::string& path, std::vector<TitleRow>& out,
                                           std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        ++lineno;
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols;
        std::string col;
        std::istringstream ls(line);
        while (std::getline(ls, col, '\t')) cols.push_back(col);
        if (cols.size() < 3) {
            if (error) *error = path + ":" + std::to_string(lineno) + ": expected library<TAB>name<TAB>address";
            return false;
        }
        TitleRow r;
        r.library = cols[0];
        r.name = cols[1];
        r.address = static_cast<uint32_t>(std::strtoul(cols[2].c_str(), nullptr, 0));
        if (cols.size() > 3) r.signature = cols[3];
        if (r.address == 0) {
            if (error) *error = path + ":" + std::to_string(lineno) + ": bad address";
            return false;
        }
        out.push_back(std::move(r));
    }
    return true;
}

std::vector<SdkBindingManifest::Resolved> SdkBindingManifest::Resolve(
    const std::vector<TitleRow>& title) const {
    std::vector<Resolved> out;
    out.reserve(title.size());
    for (const TitleRow& t : title) out.push_back(Resolved{t, Find(t.library, t.name)});
    return out;
}

int SdkBindingManifest::Install(const std::vector<Resolved>& rows, const Installer& install,
                                const Capture& capture, int* trap_rows, int* unresolved,
                                const WaitWrapper& wait_wrap) const {
    int installed = 0, traps = 0, missing = 0;
    for (const Resolved& r : rows) {
        Body body;
        if (r.engine == nullptr) {
            // Unimplemented SDK function reached: terminal, like OLD's hard trap.
            // The park records the INVOKED address (the dispatcher's `a`, which
            // is the row's address -- ctx.pc may already point past the jump)
            // and the row name, so run.meta names the next body to write.
            const std::string name = r.title.library + " " + r.title.name;
            body = [name](CpuContext&, uint32_t a) {
                GuestPark::Forever("sdk_unimplemented", a, name.c_str());
            };
            ++traps;
        } else if (r.engine->kind == Kind::Recompiled) {
            Body original = capture ? capture(r.title.address) : Body();
            if (!original) {
                ++missing;   // no generated body to keep: nothing to install
                continue;
            }
            auto hit = std::make_shared<std::atomic<uint64_t>>(0);
            hits_.push_back({r.title.name, r.title.address, hit});
            const uint32_t addr = r.title.address;
            body = [original, addr, hit](CpuContext& c, uint32_t a) {
                if (a == addr) hit->fetch_add(1, std::memory_order_relaxed);
                original(c, a);
            };
        } else if (r.engine->kind == Kind::SdkBind) {
            Handler h = r.engine->handler;
            auto hit = std::make_shared<std::atomic<uint64_t>>(0);
            hits_.push_back({r.title.name, r.title.address, hit});
            body = [h, hit](CpuContext& ctx, uint32_t) {
                hit->fetch_add(1, std::memory_order_relaxed);
                h(ctx);
            };
            // A wait body: the root attaches the guest frame and the restart proof.
            if (r.engine->restart_wait && wait_wrap) body = wait_wrap(r.title.address, std::move(body), r.engine->restart_wait);
        } else {
            Body original = capture ? capture(r.title.address) : Body();
            WrapHandler w = r.engine->wrap;
            const bool set_pc = r.engine->set_pc_on_wrap;
            const uint32_t addr = r.title.address;
            auto hit = std::make_shared<std::atomic<uint64_t>>(0);
            hits_.push_back({r.title.name, r.title.address, hit});
            body = [w, original, set_pc, addr, hit](CpuContext& c, uint32_t a) {
                if (a == addr) hit->fetch_add(1, std::memory_order_relaxed);
                if (a == addr && w(c)) {
                    if (set_pc) c.cpuRegs.pc = c.cpuRegs.GPR.r[31].UL[0];
                    return;
                }
                if (original) original(c, a);
            };
        }
        if (install(r.title.address, body)) ++installed;
        else ++missing;
    }
    if (trap_rows) *trap_rows = traps;
    if (unresolved) *unresolved = missing;
    return installed;
}

void SdkBindingManifest::ResetForTest() {
    rows_.clear();
    hits_.clear();
}

uint64_t SdkBindingManifest::HitTotal() const {
    uint64_t total = 0;
    for (const HitCell& c : hits_) total += c.count->load(std::memory_order_relaxed);
    return total;
}

std::string SdkBindingManifest::HitList() const {
    std::string out;
    char buf[32];
    for (const HitCell& c : hits_) {
        const uint64_t n = c.count->load(std::memory_order_relaxed);
        if (!n) continue;
        std::snprintf(buf, sizeof(buf), "@0x%x:%llu", c.address, static_cast<unsigned long long>(n));
        out += (out.empty() ? "" : ",") + c.name + buf;
    }
    return out;
}
