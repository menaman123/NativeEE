// tests/sif/test_sce_sif_dma.cpp -- SceSifDma: SIF DMA is a byte copy between
// the two RAMs, in both directions, with the original runtime's return rule.
//
//   EE -> IOP: sceSifSetDma(SifDmaTransfer* sdt, int count) (syscall 119) walks
//   the 16-byte entries {src (EE), dest (IOP), size, attr} and moves every
//   entry's bytes through SyscallTable's bound copy into IopMemory; the id it
//   returns is 0 when no entry carries the command attribute (attr & 4), else
//   the next transfer id (1, 2, ...).
//   IOP -> EE: sifman's sceSifSetDma lands in the EE's main RAM through
//   MainMemoryEeRamSink::WriteEeRam (physical addresses, the kernel-segment
//   bits masked as the DMAC does; a range outside the 32 MB is refused).
//
// Byte-exactness is asserted over a list of entries of odd sizes and offsets.
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/ee/hle/syscall_table.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/iop/iop_memory.h"
#include "engine/sif/hle/main_memory_ee_ram_sink.h"

using ps2r::iop::IopMemory;

static int g_fail = 0;
#define CHECK(c, msg)                                                                          \
    do {                                                                                       \
        if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); g_fail++; }      \
    } while (0)

namespace {
struct Entry {
    uint32_t src, dest, size, attr;
};
constexpr uint32_t kList = 0x00280000;  // the SifDmaTransfer array in EE RAM
constexpr int kSifSetDma = 119;

uint8_t Pattern(uint32_t seed, uint32_t i) { return static_cast<uint8_t>((seed * 31u + i * 7u + (i >> 3)) & 0xFFu); }

void WriteList(const std::vector<Entry>& list) {
    for (size_t i = 0; i < list.size(); ++i) {
        const uint32_t e = kList + static_cast<uint32_t>(i) * 16u;
        memory::write<uint32_t>(e + 0x0, list[i].src);
        memory::write<uint32_t>(e + 0x4, list[i].dest);
        memory::write<uint32_t>(e + 0x8, list[i].size);
        memory::write<uint32_t>(e + 0xC, list[i].attr);
    }
}

uint32_t SetDma(SyscallTable& st, const std::vector<Entry>& list) {
    WriteList(list);
    CpuContext c{};
    c.cpuRegs.GPR.r[29].UL[0] = 0x01FFFF00;
    c.cpuRegs.GPR.r[4].UL[0] = kList;
    c.cpuRegs.GPR.r[5].SL[0] = static_cast<int32_t>(list.size());
    st.Dispatch(kSifSetDma, c);
    return c.cpuRegs.GPR.r[2].UL[0];
}

bool IopMatchesEe(const IopMemory& iop, const Entry& e) {
    for (uint32_t i = 0; i < e.size; ++i)
        if (iop.Read<uint8_t>(e.dest + i) != memory::read<uint8_t>(e.src + i)) return false;
    return true;
}
}  // namespace

int main() {
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);

    IopMemory iop;
    auto& st = SyscallTable::Instance();
    st.ResetForTest();
    // The composition root's copy: byte for byte, EE RAM -> IOP RAM.
    st.SetSifDmaCopy([&iop](uint32_t ee_src, uint32_t iop_dst, uint32_t size) {
        for (uint32_t i = 0; i < size; ++i) iop.Write<uint8_t>(iop_dst + i, memory::read<uint8_t>(ee_src + i));
    });

    // ---- EE -> IOP -------------------------------------------------------
    const std::vector<Entry> list = {
        {0x00300000, 0x00010000, 0x40, 0},   // aligned block, no command attribute
        {0x00301003, 0x00020011, 0x13, 4},   // odd source/dest/size, the command entry
        {0x00302000, 0x00030000, 0x00, 0},   // zero size: nothing moves
        {0x00303008, 0x0004FFF8, 0x101, 0},  // crosses an IOP 64 KB boundary
    };
    uint64_t expected_bytes = 0;
    for (size_t n = 0; n < list.size(); ++n) {
        for (uint32_t i = 0; i < list[n].size; ++i) memory::write<uint8_t>(list[n].src + i, Pattern(static_cast<uint32_t>(n) + 1, i));
        expected_bytes += list[n].size;
    }
    // A guard byte on each side of every destination proves the copy's extent.
    for (const Entry& e : list) {
        iop.Write<uint8_t>(e.dest - 1, 0xEE);
        iop.Write<uint8_t>(e.dest + e.size, 0xEE);
    }
    const uint32_t id1 = SetDma(st, list);
    CHECK(id1 == 1, "a list with a command entry (attr & 4) returns transfer id 1");
    for (size_t n = 0; n < list.size(); ++n) CHECK(IopMatchesEe(iop, list[n]), "entry bytes arrive in IOP RAM byte for byte");
    for (const Entry& e : list) {
        CHECK(iop.Read<uint8_t>(e.dest - 1) == 0xEE && iop.Read<uint8_t>(e.dest + e.size) == 0xEE,
              "the copy neither under- nor over-runs its entry");
    }
    CHECK(iop.Read<uint8_t>(list[2].dest) == 0xEE, "a zero-size entry moves nothing");
    CHECK(st.SifDmaBytes() == expected_bytes, "sif_dma_bytes counts every byte moved EE -> IOP");
    CHECK(iop.Read<uint8_t>(0x00050000) == memory::read<uint8_t>(0x00303008 + 8),
          "the 64 KB-crossing entry continues past the boundary");

    // The return rule: no command entry -> 0 (and the bytes still move); the id
    // counter advances only on accepted command lists; count 0 -> 0.
    const std::vector<Entry> plain = {{0x00304000, 0x00060000, 0x20, 0}, {0x00304100, 0x00060100, 0x08, 1}};
    for (uint32_t i = 0; i < 0x20; ++i) memory::write<uint8_t>(0x00304000 + i, Pattern(9, i));
    for (uint32_t i = 0; i < 0x08; ++i) memory::write<uint8_t>(0x00304100 + i, Pattern(10, i));
    CHECK(SetDma(st, plain) == 0, "a list without the command attribute returns 0");
    CHECK(IopMatchesEe(iop, plain[0]) && IopMatchesEe(iop, plain[1]), "...but its bytes still move");
    CHECK(SetDma(st, {}) == 0, "count 0 returns 0");
    const std::vector<Entry> cmd_only = {{0x00305000, 0x00070000, 0x10, 0x4}};
    for (uint32_t i = 0; i < 0x10; ++i) memory::write<uint8_t>(0x00305000 + i, Pattern(11, i));
    CHECK(SetDma(st, cmd_only) == 2, "the next command list returns id 2");
    CHECK(SetDma(st, {{0x00305000, 0x00070100, 0x10, 0x7}}) == 3, "attr 0x7 carries the command bit: id 3");
    CHECK(st.SifDmaBytes() == expected_bytes + 0x28 + 0x10 + 0x10, "the byte census follows every list");

    // ---- IOP -> EE -------------------------------------------------------
    MainMemoryEeRamSink ee_sink(ram);
    std::vector<uint8_t> payload(0x233);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = Pattern(21, static_cast<uint32_t>(i));
    memory::write<uint8_t>(0x00400000 - 1, 0xEE);
    memory::write<uint8_t>(0x00400000 + static_cast<uint32_t>(payload.size()), 0xEE);
    CHECK(ee_sink.WriteEeRam(0x00400000, payload.data(), payload.size()), "IOP -> EE: a physical EE address is accepted");
    bool same = true;
    for (size_t i = 0; i < payload.size(); ++i) same = same && memory::read<uint8_t>(0x00400000 + static_cast<uint32_t>(i)) == payload[i];
    CHECK(same, "the payload lands in EE RAM byte for byte (visible through memory::read)");
    CHECK(memory::read<uint8_t>(0x00400000 - 1) == 0xEE && memory::read<uint8_t>(0x00400000 + static_cast<uint32_t>(payload.size())) == 0xEE,
          "the EE-side copy neither under- nor over-runs");
    CHECK(ee_sink.WriteEeRam(0x80401001, payload.data(), 5), "a KSEG0 address is masked to its physical word");
    same = true;
    for (uint32_t i = 0; i < 5; ++i) same = same && memory::read<uint8_t>(0x00401001 + i) == payload[i];
    CHECK(same, "...and the bytes land at the physical address");
    CHECK(ee_sink.WriteEeRam(0xA0401010, payload.data(), 3), "a KSEG1 address is masked too");
    CHECK(memory::read<uint8_t>(0x00401012) == payload[2], "...bytes at the physical address");
    CHECK(!ee_sink.WriteEeRam(0x01FFFFF0, payload.data(), 0x20), "a range past the 32 MB is refused");
    CHECK(!ee_sink.WriteEeRam(0x02000000, payload.data(), 1), "an address at the end of RAM is refused");
    CHECK(ee_sink.Bytes() == payload.size() + 5 + 3, "the IOP -> EE byte census counts only accepted writes");

    std::printf("sif dma: ee->iop %llu bytes (ids 1,0,0,2,3); iop->ee %llu bytes\n",
                (unsigned long long)st.SifDmaBytes(), (unsigned long long)ee_sink.Bytes());
    std::printf("%s: %d failure(s)\n", g_fail == 0 ? "ALL PASS" : "FAILED", g_fail);
    return g_fail == 0 ? 0 : 1;
}
