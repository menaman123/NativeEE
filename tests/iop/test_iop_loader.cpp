// tests/iop/test_iop_loader.cpp -- IopLoader: the ELF32 section walk over OLD's
// game_files/STREAM.IRX (read-only; the disc reader is Phase 4D so the bytes
// come from the frozen tree's extracted copy), the header checks, the entry
// run through a resolver, and the reboot/LOADFILE faces. Skips (exit 0 with a
// SKIP line) when the OLD tree is absent.
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "engine/iop/iop_clock.h"
#include "engine/iop/iop_loader.h"
#include "engine/iop/iop_memory.h"
#include "engine/iop/iop_module_table.h"
#include "engine/platform/host/platform.h"

using namespace ps2r::iop;

static int g_fail = 0;
#define CHECK(c, msg)                                                          \
    do {                                                                       \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); g_fail++; } \
    } while (0)

static void TestSectionsFromStreamIrx(const std::vector<uint8_t>& irx) {
    IopMemory mem;
    IopLoader loader(mem);
    const IopLoader::SectionReport r = loader.LoadSections(irx.data(), irx.size());
    std::printf("STREAM.IRX: %zu bytes; text %u data %u rodata %u bss %u; %s\n", irx.size(), r.text_bytes, r.data_bytes,
                r.rodata_bytes, r.bss_bytes, r.ok ? "ok" : r.error.c_str());
    CHECK(r.ok && r.found_text, "STREAM.IRX loads: .text found");
    CHECK(r.text_bytes > 0 && r.data_bytes > 0 && r.bss_bytes > 0, "text/data/bss all present");
    // .text at 0: the first word of the entry is code, not zero.
    CHECK(mem.Read<uint32_t>(0) != 0, "the .text bytes landed at address 0");
    CHECK(mem.UnroutedCount() == 0, "no section fell outside IOP RAM");
}

static void TestHeaderChecks() {
    IopMemory mem;
    IopLoader loader(mem);
    std::vector<uint8_t> bad(64, 0);
    CHECK(!loader.LoadSections(bad.data(), bad.size()).ok, "a non-ELF image is rejected");
    bad[0] = 0x7F; bad[1] = 'E'; bad[2] = 'L'; bad[3] = 'F'; bad[4] = 2;   // ELFCLASS64
    CHECK(loader.LoadSections(bad.data(), bad.size()).error == "not ELFCLASS32", "ELFCLASS64 is rejected");
    bad[4] = 1; bad[18] = 3;   // e_machine = EM_386
    CHECK(loader.LoadSections(bad.data(), bad.size()).error == "not EM_MIPS", "a non-MIPS image is rejected");
    CHECK(!loader.LoadSections(nullptr, 0).ok, "an empty image is rejected");
}

static void TestEntryAndFaces() {
    IopMemory mem;
    IopLoader loader(mem);
    IopModuleTable table(IopClock::Policy::Frame);
    IopLoader::ModuleLayout layout;
    layout.gp = 0x4000;
    layout.entry = 0x100;
    layout.stack_top = 0x20000;
    layout.rpc_tid_addr = 0x4000;
    mem.Write<uint32_t>(0x4000, 3);   // the entry leaves the RPC tid in the $gp word
    uint32_t seen_sp = 0, seen_gp = 0, seen_pc = 0;
    auto resolve = [&](uint32_t addr) -> IopLoader::GuestFunction {
        if (addr != 0x100) return {};
        return [&](IopContext& c) { seen_sp = c.gpr[29]; seen_gp = c.gpr[28]; seen_pc = c.pc; };
    };
    table.Kernel().RegisterThread(3, 30);
    CHECK(loader.RunEntry(layout, resolve, table), "the entry runs when the resolver knows it");
    CHECK(seen_sp == 0x20000 - 0x10 && seen_gp == 0x4000 && seen_pc == 0x100, "entry context: $sp = stack_top - 0x10, $gp, pc");
    CHECK(loader.LastRpcTid() == 3 && table.Booted(), "OnModuleLoaded(rpc_tid = [rpc_tid_addr]) booted the table");
    IopLoader::ModuleLayout unknown = layout;
    unknown.entry = 0x200;
    CHECK(!loader.RunEntry(unknown, resolve, table), "an unresolved entry is reported, not run");

    const IopLoader::RebootReport rb = loader.RebootHandshake("rom0:IOPRP300.IMG");
    CHECK(rb.accepted && rb.image == "rom0:IOPRP300.IMG", "the reboot handshake is accepted (pre-reboot set skipped)");
    CHECK(!loader.RebootHandshake("").accepted, "an empty image path is not a handshake");
    CHECK(loader.LoadFileRequest("cdrom0:\\DATASYS\\STREAM.IRX;1") == 1, "LOADFILE: first request -> module id 1");
    CHECK(loader.LoadFileRequest("cdrom0:\\DATASYS\\LIBSD.IRX;1") == 2 && loader.LoadRequests().size() == 2, "LOADFILE ids count up");
    CHECK(loader.LoadFileRequest("") == 0, "LOADFILE: an empty path is rejected");
    CHECK(IopLoader::kLoadFileRpcId == 0x80000006u, "the LOADFILE RPC server id");
    table.Shutdown();
}

int main(int argc, char** argv) {
    ps2r::platform::host::RegisterSelf();
    TestHeaderChecks();
    TestEntryAndFaces();
    if (argc >= 2) {
        const std::string path = std::string(argv[1]) + "/game_files/STREAM.IRX";
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::vector<uint8_t> irx((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            TestSectionsFromStreamIrx(irx);
        } else {
            std::printf("SKIP: %s not readable (OLD tree absent)\n", path.c_str());
        }
    }
    std::printf("%s: %d failure(s)\n", g_fail == 0 ? "ALL PASS" : "FAILED", g_fail);
    return g_fail == 0 ? 0 : 1;
}
