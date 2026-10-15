// CdvdmanModule over the ReadEngine (the IOP side of the disc):
//   * without a reader: sceCdRead -> 0, sceCdSearchFile -> 0 with the record zeroed;
//   * with the ISO (argv[1]): sceCdSearchFile("\DATA\SYS\STREAM.IRX;1") fills the
//     sceCdlFILE {lsn, size, name} in IOP RAM (guard intact); a miss zeroes it;
//     sceCdRead(lba, 2, buf) lands 4096 bytes in IOP RAM byte-exact with the file's
//     first two sectors (guard intact), reads/sectors census; a buffer past IOP RAM
//     or a range off the image -> 0 and no census; no kernel -> no callback fired.
//   * one drive (a FRAME kernel + a DeliveryMeter under poll visibility): unbound,
//     a read's completion is due kCompletionUs after the issue; bound to the meter,
//     a module read issued while the drive is busy with the EE's host-served read
//     completes at the drive's end for it (seek + transfer after the EE's read), the
//     meter's timeline charged; a boundary-mode meter charges nothing (now returned).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/cdvd/delivery_meter.h"
#include "engine/cdvd/iso_fs.h"
#include "engine/cdvd/read_engine.h"
#include "engine/iop/hle/cdvdman/cdvdman_module.h"
#include "engine/iop/iop_kernel.h"
#include "engine/iop/iop_memory.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { if (cond) std::printf("PASS %s\n", name); else { std::printf("FAIL %s\n", name); g_fail++; } } while (0)

using ps2r::iop::IopContext;
using ps2r::iop::IopMemory;
using ps2r::iop::hle::CdvdmanModule;

static uint32_t Call(CdvdmanModule& m, void (CdvdmanModule::*fn)(IopContext&), uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0,
                     uint32_t a3 = 0) {
    IopContext ctx;
    ctx.gpr[4] = a0; ctx.gpr[5] = a1; ctx.gpr[6] = a2; ctx.gpr[7] = a3;
    ctx.gpr[2] = 0xDEADBEEFu;
    (m.*fn)(ctx);
    return ctx.gpr[2];
}

static void PutString(IopMemory& mem, uint32_t addr, const char* s) {
    for (uint32_t i = 0; i <= std::strlen(s); i++) mem.Write<uint8_t>(addr + i, static_cast<uint8_t>(s[i]));
}

int main(int argc, char** argv) {
    static IopMemory mem;
    const uint32_t fp = 0x00010000, path = 0x00010100, buf = 0x00020000;

    CdvdmanModule none(mem, nullptr);
    for (uint32_t i = 0; i < 36; i++) mem.Write<uint8_t>(fp + i, 0xBB);
    PutString(mem, path, "\\DATA\\SYS\\STREAM.IRX;1");
    CHECK(Call(none, &CdvdmanModule::sceCdRead, 318, 1, buf, 0) == 0, "no_reader_read_is_0");
    CHECK(Call(none, &CdvdmanModule::sceCdSearchFile, fp, path) == 0 && mem.Read<uint32_t>(fp) == 0 && mem.Read<uint8_t>(fp + 32) == 0xBB,
          "no_reader_search_is_0_record_zeroed_guard_intact");

    IsoFs fs;
    if (argc > 1 && fs.Open(argv[1])) {
        ReadEngine engine(fs);
        CdvdmanModule m(mem, &engine);
        IsoFs::Entry e;
        CHECK(fs.Find("DATA/SYS/STREAM.IRX", e), "iso_has_stream_irx");
        for (uint32_t i = 0; i < 36; i++) mem.Write<uint8_t>(fp + i, 0xBB);
        CHECK(Call(m, &CdvdmanModule::sceCdSearchFile, fp, path) == 1, "search_file_hit");
        char nm[17] = {};
        for (int i = 0; i < 16; i++) nm[i] = static_cast<char>(mem.Read<uint8_t>(fp + 8 + i));
        CHECK(mem.Read<uint32_t>(fp) == e.lba && mem.Read<uint32_t>(fp + 4) == e.size && std::string(nm) == "STREAM.IRX;1" &&
                  mem.Read<uint8_t>(fp + 32) == 0xBB,
              "search_file_record_lsn_size_name_guard");
        std::printf("  sceCdlFILE: lsn %u size %u name '%s'\n", mem.Read<uint32_t>(fp), mem.Read<uint32_t>(fp + 4), nm);
        PutString(mem, path, "\\DATA\\SYS\\NOPE.IRX;1");
        CHECK(Call(m, &CdvdmanModule::sceCdSearchFile, fp, path) == 0 && mem.Read<uint32_t>(fp) == 0 && mem.Read<uint32_t>(fp + 4) == 0,
              "search_file_miss_zeroes_record");
        CHECK(m.SearchHits() == 1, "search_census");

        for (uint32_t i = 0; i < 2 * CdvdmanModule::kSectorBytes + 4; i++) mem.Write<uint8_t>(buf + i, 0xCC);
        CHECK(Call(m, &CdvdmanModule::sceCdRead, e.lba, 2, buf, 0) == 1, "read_two_sectors_returns_1");
        std::vector<uint8_t> want(2 * CdvdmanModule::kSectorBytes);
        const int32_t fd = engine.Open("DATA/SYS/STREAM.IRX", 0);
        engine.Read(fd, want.data(), static_cast<uint32_t>(want.size()));
        engine.Close(fd);
        bool same = mem.Read<uint8_t>(buf + 2 * CdvdmanModule::kSectorBytes) == 0xCC;
        for (uint32_t i = 0; i < want.size() && same; i++) same = mem.Read<uint8_t>(buf + i) == want[i];
        CHECK(same, "read_bytes_land_in_iop_ram_byte_exact_guard_intact");
        CHECK(m.ReadsIssued() == 1 && m.SectorsRead() == 2, "read_census");
        CHECK(Call(m, &CdvdmanModule::sceCdRead, e.lba, 2, IopMemory::kSize - 2048, 0) == 0 && m.ReadsIssued() == 1,
              "read_buffer_past_iop_ram_is_0_uncounted");
        CHECK(Call(m, &CdvdmanModule::sceCdRead, 0x7FFFFFF0u, 1, buf, 0) == 0 && Call(m, &CdvdmanModule::sceCdRead, e.lba, 0, buf, 0) == 0,
              "read_off_image_or_zero_sectors_is_0");
        CHECK(m.CallbacksFired() == 0, "no_kernel_no_callback");

        {   // one drive: the module's read waits for the drive the EE's host-served read holds
            ps2r::iop::IopKernel kernel;
            kernel.Clock().SetPolicy(ps2r::iop::IopClock::Policy::Frame);
            CdvdmanModule d(mem, &engine);
            d.SetCompletion(&kernel, [](uint32_t, IopContext&) {});
            const uint64_t now = kernel.Clock().NowUs();
            CHECK(Call(d, &CdvdmanModule::sceCdRead, e.lba, 2, buf, 0) == 1 && d.LastCompletionDueUs() == now + CdvdmanModule::kCompletionUs,
                  "unbound_completion_is_the_fixed_latency");
            ps2r::CdvdConfig poll;
            poll.latency_model = ps2r::CdvdLatencyModel::Modelled;
            poll.delivery_visibility = ps2r::CdvdDeliveryVisibility::Poll;
            poll.poll_latency_scale = 1.0;
            DeliveryMeter meter(poll);
            // The EE's host-served read first: 40 sectors at the disc's start issued at tick 0 hold the drive ~37 ms.
            meter.Schedule(0, 0, 40, [] {});
            const double ee_end_ms = meter.BusyUntilMs();
            CHECK(ee_end_ms > 30.0, "ee_read_holds_the_drive_past_30_ms");
            d.SetCompletionDue([&meter](uint32_t lba, uint32_t sectors, uint64_t now_us) {
                return static_cast<uint64_t>(meter.ChargeReadAtMs(static_cast<double>(now_us) / 1000.0, lba, sectors) * 1000.0);
            });
            CHECK(Call(d, &CdvdmanModule::sceCdRead, e.lba, 2, buf, 0) == 1, "module_read_issued_on_the_busy_drive");
            const uint64_t due = d.LastCompletionDueUs();
            // The module's read starts when the EE's is done, seeks from head 40 to e.lba, then transfers 2 sectors.
            const uint32_t delta = e.lba > 40 ? e.lba - 40 : 40 - e.lba;
            const double seek_ms = delta <= DeliveryMeter::kContiguousSectors ? 0.0
                                   : delta < DeliveryMeter::kShortSeekSectors ? DeliveryMeter::kShortSeekMs : DeliveryMeter::kLongSeekMs;
            const uint64_t want = static_cast<uint64_t>((ee_end_ms + seek_ms + 2.0 / DeliveryMeter::kSectorsPerMs) * 1000.0);
            CHECK(due == want && due > now + CdvdmanModule::kCompletionUs, "module_read_completes_at_the_drive_end_after_the_ee_read");
            CHECK(meter.BusyUntilMs() > ee_end_ms && meter.SeeksContiguous() + meter.SeeksShort() + meter.SeeksLong() == 2,
                  "drive_timeline_charged_for_both_clients");
            // Boundary mode: the meter charges nothing and answers the issue instant (the module's own latency applies).
            ps2r::CdvdConfig boundary;
            boundary.delivery_visibility = ps2r::CdvdDeliveryVisibility::Boundary;   // this case tests the boundary model (the default is poll since gate S4)
            DeliveryMeter inert(boundary);
            CHECK(inert.ChargeReadAtMs(7.5, e.lba, 2) == 7.5 && inert.BusyUntilMs() == 0.0, "boundary_meter_charges_nothing");
            // A drive already idle: the completion is never before the issue.
            const uint64_t before = kernel.Clock().NowUs();
            CHECK(Call(d, &CdvdmanModule::sceCdRead, e.lba, 1, buf, 0) == 1 && d.LastCompletionDueUs() >= before, "completion_never_before_the_issue");
        }
    } else {
        std::printf("  (no disc image given: ISO-backed cases skipped)\n");
    }
    std::printf(g_fail ? "FAILURES: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
