// tests/ipu/test_libipu_api.cpp -- LibipuApi over the REAL IpuDevice and a
// recording DmacChannelFace: sceIpuInit's RST/BCLR effects, sceIpuSync, and the
// sceIpuStopDMA / sceIpuRestartDMA nine-word snapshot round trip.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "engine/ipu/hle/dmac_channel_face_adapter.h"
#include "engine/ipu/hle/libipu_api.h"
#include "engine/ipu/ipu_device.h"
#include "recording_dmac_channel_face.h"

using RecordingRegs = ps2r::ipu::DmacChannelRegs;
using ps2r::ipu::DmacChannelFaceAdapter;
using ps2r::ipu::IpuCommand;
using ps2r::ipu::IpuDevice;
using ps2r::ipu::LibipuApi;
using ps2r::ipu::LibipuDmaSnapshot;
using ps2r::ipu::RecordingDmacChannelFace;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, what)                                                          \
    do {                                                                           \
        ++g_checks;                                                                \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "FAIL %s:%d: %s -- %s\n", __FILE__, __LINE__, #cond, what); \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

static uint32_t Cmd(IpuCommand c, uint32_t low) { return (static_cast<uint32_t>(c) << 28) | low; }

// Stages a unit with a non-zero bit cursor (BCLR bp = 5, then one CSC macroblock
// advances it by 384 * 8 bits), 8 quadwords of input left past the cursor, one CSC
// macroblock of output, and CTRL counts recomputed (IFC = 8, OFC = 8).
static void StageBusyUnit(IpuDevice& ipu) {
    ipu.WriteCmd(Cmd(IpuCommand::Bclr, 5));  // bp = 5
    std::vector<uint8_t> mb(IpuDevice::kCscInputBytesPerMb + 128, 0x80);
    ipu.FeedInputFifo(mb.data(), mb.size());
    ipu.WriteCmd(Cmd(IpuCommand::Csc, 1));  // 1 macroblock -> 1024 output bytes
    (void)ipu.ReadCtrl();                    // recompute IFC/OFC from the FIFO sizes
}

// ---------------------------------------------------------------------------
// sceIpuInit
// ---------------------------------------------------------------------------
static void TestInit() {
    IpuDevice ipu;
    RecordingDmacChannelFace dmac;
    StageBusyUnit(ipu);
    CHECK(ipu.BitPosition() == 5 + 384 * 8, "staged bp (BCLR 5 + one CSC macroblock)");
    CHECK(ipu.InputSize() > 0 && ipu.OutputSize() == 1024, "staged FIFOs");
    // The staged counts: the next CTRL read reports IFC = min(input/16, 8) and OFC = 8.
    const uint32_t staged_ifc = static_cast<uint32_t>(ipu.InputSize() / 16 > 8 ? 8 : ipu.InputSize() / 16);
    CHECK(staged_ifc == 8, "staged IFC saturates at 8");

    LibipuApi api(ipu, dmac);
    api.Init();

    // RST effects: both FIFOs cleared (BCLR alone would leave the output FIFO), bp = 0, not busy.
    CHECK(ipu.OutputSize() == 0, "RST cleared the output FIFO");
    CHECK(ipu.InputSize() == 0, "input FIFO cleared");
    CHECK(ipu.BitPosition() == 0, "bp reset to 0");
    CHECK(!ipu.IsBusy(), "not busy after init");
    // RST clears both FIFO counts before the first status read.
    const uint32_t ctrl = ipu.ReadCtrl();
    CHECK((ctrl & 0xF) == 0, "first CTRL read: IFC 0 (BCLR after the staged recompute)");
    CHECK(((ctrl >> 4) & 0xF) == 0, "first CTRL read: OFC cleared by RST");
    CHECK((ctrl & IpuDevice::kCtrlBusy) == 0, "BUSY clear");
    const uint32_t ctrl2 = ipu.ReadCtrl();
    CHECK(((ctrl2 >> 4) & 0xF) == 0, "second CTRL read: OFC recomputed to 0");
    CHECK(ipu.ReadBp() == 0, "BP register reads 0");
    CHECK(dmac.Log().empty(), "Init touches no DMAC register");
    // Init is idempotent on a clean unit.
    api.Init();
    CHECK(ipu.InputSize() == 0 && ipu.OutputSize() == 0 && ipu.BitPosition() == 0, "second Init: clean");
    CHECK(ipu.DivergentCommandCount() == 0, "Init issues no divergent command");
    CHECK(api.Count().init == 2 && api.Count().sync == 0 && api.Count().stop == 0 &&
          api.Count().restart == 0, "actual initialization census survives the reset it observes");
}

// ---------------------------------------------------------------------------
// sceIpuSync
// ---------------------------------------------------------------------------
static void TestSync() {
    IpuDevice ipu;
    RecordingDmacChannelFace dmac;
    LibipuApi api(ipu, dmac);
    CHECK(api.Sync(0) == 0, "mode 0 returns 0 (BUSY never lingers)");
    CHECK(api.Sync(1) == 0, "mode 1 returns the BUSY bit: 0");
    CHECK(api.Sync(7) == 0, "other modes return 0");
    StageBusyUnit(ipu);
    const uint32_t staged_bp = ipu.BitPosition();
    CHECK(api.Sync(0) == 0, "mode 0 after commands: 0");
    CHECK(api.Sync(1) == 0, "mode 1 after commands: 0");
    // Sync(0) reads CTRL, which recomputes the counts: the staged FIFOs are intact.
    CHECK(ipu.OutputSize() == 1024 && ipu.BitPosition() == staged_bp, "Sync does not alter the unit");
    CHECK(dmac.Log().empty(), "Sync touches no DMAC register");
    CHECK(api.Count().sync == 5 && api.Count().init == 0, "sync census counts actual entry independently of commands");
}

// ---------------------------------------------------------------------------
// sceIpuStopDMA / sceIpuRestartDMA
// ---------------------------------------------------------------------------
static void TestStopRestart() {
    IpuDevice ipu;
    RecordingDmacChannelFace dmac;
    LibipuApi api(ipu, dmac);

    RecordingRegs& ch3 = dmac.Channel(3);
    RecordingRegs& ch4 = dmac.Channel(4);
    ch3.chcr = 0x00000100;  // FROM: DIR=0 (IPU -> memory), STR
    ch3.madr = 0x00200000;
    ch3.qwc = 0x40;
    ch4.chcr = 0x00000101;  // TO: DIR=1 (memory -> IPU), STR
    ch4.madr = 0x00300100;
    ch4.tadr = 0x00300000;
    ch4.qwc = 0x20;

    // Stage the unit so BP / CTRL carry non-trivial fields: 8 quadwords of input,
    // bp = 5 -> IFC = 8, BP = 5 | 8 << 8.
    ipu.WriteCmd(Cmd(IpuCommand::Bclr, 5));
    std::vector<uint8_t> in(128, 0x11);
    ipu.FeedInputFifo(in.data(), in.size());

    LibipuDmaSnapshot snap;
    dmac.ClearLog();
    api.StopDma(snap);

    CHECK((ch3.chcr & LibipuApi::kChcrStr) == 0, "ch3 STR cleared");
    CHECK((ch4.chcr & LibipuApi::kChcrStr) == 0, "ch4 STR cleared");
    CHECK(snap.ipu_to_madr == 0x00300100 && snap.ipu_to_tadr == 0x00300000 && snap.ipu_to_qwc == 0x20,
          "ch4 MADR/TADR/QWC snapshot");
    CHECK(snap.ipu_to_chcr == 0x00000001, "ch4 CHCR snapshot preserves memory-to-IPU direction");
    CHECK(snap.ipu_from_madr == 0x00200000 && snap.ipu_from_qwc == 0x40, "ch3 MADR/QWC snapshot");
    CHECK(snap.ipu_from_chcr == 0x00000000, "ch3 CHCR snapshot preserves IPU-to-memory direction");
    CHECK(snap.ipu_bp == (5u | (8u << 8)), "BP word: bp 5, IFC 8");
    CHECK((snap.ipu_ctrl & 0xF) == 8, "CTRL word: IFC 8");
    CHECK((snap.ipu_ctrl & IpuDevice::kCtrlBusy) == 0, "CTRL word: not busy");

    // The SDK's access order: STR clears, ch4 words, (drain), ch3 words.
    const std::vector<std::string>& log = dmac.Log();
    const std::vector<std::string> expect = {
        "R3.CHCR", "W3.CHCR=0x00000000", "R4.CHCR", "W4.CHCR=0x00000001",
        "R4.MADR", "R4.TADR", "R4.QWC", "R4.CHCR",
        "R3.MADR", "R3.QWC", "R3.CHCR"};
    CHECK(log == expect, "StopDma access order");
    if (log != expect) {
        for (const std::string& s : log) std::fprintf(stderr, "  log: %s\n", s.c_str());
    }

    // The eight unread input quadwords come from saved BP, independently of
    // CTRL's CBP/IDP fields. BCLR restores the original bit offset.
    CHECK(LibipuApi::RewindQuadwords(snap.ipu_bp) == 8, "rewind from the saved BP word");
    CHECK(LibipuApi::RewindQuadwords(0x00020300u) == 5, "rewind = field 2 + field 3");

    // Scramble the live registers so the restore is observable.
    ch3.madr = ch3.qwc = ch3.chcr = 0xDEADBEEF;
    ch4.madr = ch4.tadr = ch4.qwc = ch4.chcr = 0xDEADBEEF;
    dmac.ClearLog();
    api.RestartDma(snap);

    CHECK(ch3.madr == 0x00200000 && ch3.qwc == 0x40, "ch3 MADR/QWC restored");
    CHECK(ch3.chcr == LibipuApi::kChcrStr, "ch3 CHCR restored with STR and DIR=0");
    CHECK(ch4.madr == 0x00300080u, "ch4 MADR rewound by eight unread quadwords");
    CHECK(ch4.qwc == 0x28u, "ch4 QWC includes eight unread quadwords");
    CHECK(ch4.tadr == 0x00300000, "ch4 TADR restored");
    CHECK(ch4.chcr == (LibipuApi::kChcrStr | 1u), "ch4 CHCR restored with STR and DIR=1");
    const std::vector<std::string> expect_restart = {
        "W3.MADR=0x00200000", "W3.QWC=0x00000040", "W3.CHCR=0x00000100",
        "W4.MADR=0x00300080", "W4.TADR=0x00300000", "W4.QWC=0x00000028", "W4.CHCR=0x00000101"};
    CHECK(dmac.Log() == expect_restart, "RestartDma access order (ch3 before the command, ch4 after)");
    if (dmac.Log() != expect_restart) {
        for (const std::string& s : dmac.Log()) std::fprintf(stderr, "  log: %s\n", s.c_str());
    }
    CHECK(ipu.BitPosition() == 5, "BCLR restores saved BP.BP, not CTRL.IFC");
    CHECK(ipu.InputSize() == 0, "BCLR cleared the input FIFO");

    // FP=2, IFC=3, BP=17, with conflicting CTRL values to detect a word mixup.
    LibipuDmaSnapshot rewound = snap;
    rewound.ipu_bp = 0x00020311u;
    rewound.ipu_ctrl = 0x07810107u;
    dmac.ClearLog();
    api.RestartDma(rewound);
    CHECK(ch4.madr == 0x00300100u - 5u * 16u, "ch4 MADR rewound by 5 quadwords");
    CHECK(ch4.qwc == 0x20u + 5u, "ch4 QWC extended by 5 quadwords");
    CHECK(ch4.chcr == (LibipuApi::kChcrStr | 1u) && ch4.tadr == 0x00300000, "ch4 CHCR/TADR as saved");
    CHECK(ipu.BitPosition() == 17, "BCLR restores BP=17 independently of CTRL");

    // Idle channels (MADR or QWC zero) are left alone by RestartDma.
    LibipuDmaSnapshot idle;
    idle.ipu_ctrl = 0;
    ch3.madr = ch3.qwc = ch3.chcr = 0x55555555;
    ch4.madr = ch4.tadr = ch4.qwc = ch4.chcr = 0x55555555;
    dmac.ClearLog();
    api.RestartDma(idle);
    CHECK(dmac.Log().empty(), "idle snapshot: no channel write");
    CHECK(ch3.madr == 0x55555555 && ch4.madr == 0x55555555, "idle snapshot: registers untouched");
    CHECK(ipu.BitPosition() == 0, "idle snapshot: command word 0 -> BCLR bp 0");
    CHECK(api.Count().stop == 1 && api.Count().restart == 3,
          "actual snapshot/restart operations have their own positive model census");

    // Nine words, in SDK order, is the whole buffer.
    CHECK(LibipuDmaSnapshot::kWords == 9, "nine-word save buffer");
    CHECK(sizeof(LibipuDmaSnapshot) == 9 * sizeof(uint32_t), "POD of exactly nine words");
}

static void TestRealAdapterRestart() {
    ::DmacChannelRegs from, to;
    IpuDevice ipu;
    std::vector<uint8_t> data(128, 0x42);
    ipu.WriteCmd(5);
    ipu.FeedInputFifo(data.data(), data.size());
    std::vector<uint32_t> kicks;
    DmacChannelFaceAdapter face(from, to, [&](uint32_t channel) {
        kicks.push_back(channel);
        if (channel == 3) {
            CHECK(from.madr == 0x2000 && from.qwc == 4 && from.chcr == 0x100,
                  "FROM kick sees restored output registers and DIR=0");
            CHECK(ipu.InputSize() == 128, "output is armed before BCLR discards buffered input");
            // A DMA completion may mutate the aliased registers during the
            // callback. WriteChcr must not overwrite these changes afterwards.
            from.qwc = 0;
            from.chcr &= ~DmacChcr::kStr;
        } else {
            CHECK(channel == 4, "only FROM=3 and TO=4 may be kicked");
            CHECK(to.madr == 0x4000 && to.qwc == 10 && to.tadr == 0x6000 && to.chcr == 0x101,
                  "TO kick sees rewind, source-chain pointer and DIR=1");
            CHECK(ipu.InputSize() == 0 && ipu.BitPosition() == 5, "input starts after BCLR restores BP");
        }
    });
    from.chcr = 0x100;
    from.madr = 0x2000;
    from.qwc = 4;
    to.chcr = 0x101;
    to.madr = 0x4080;
    to.qwc = 2;
    to.tadr = 0x6000;
    LibipuApi api(ipu, face);
    LibipuDmaSnapshot snapshot;
    api.StopDma(snapshot);
    CHECK(kicks.empty(), "clearing STR during StopDma does not start either channel");
    CHECK(snapshot.ipu_from_madr == 0x2000 && snapshot.ipu_to_madr == 0x4080,
          "real adapter does not swap FROM and TO register files");
    from.madr = to.madr = 0;
    from.qwc = to.qwc = 0;
    api.RestartDma(snapshot);
    CHECK(kicks == std::vector<uint32_t>({3, 4}), "restart arms output then starts input");
    CHECK(from.qwc == 0 && from.chcr == 0, "adapter preserves synchronous callback completion");
    CHECK(face.ReadMadr(3) == 0x2000 && face.ReadMadr(4) == 0x4000, "readback uses correct channel");
    const size_t kicks_before = kicks.size();
    face.WriteChcr(2, DmacChcr::kStr);
    CHECK(kicks.size() == kicks_before && face.OutOfRange() == 1, "invalid channel cannot trigger a kick");
    face.WriteChcr(4, 1);
    CHECK(kicks.size() == kicks_before && to.chcr == 1, "clearing input STR does not restart it");
}

int main() {
    TestInit();
    TestSync();
    TestStopRestart();
    TestRealAdapterRestart();
    std::printf("test_libipu_api: %d/%d checks passed\n", g_checks - g_failures, g_checks);
    if (g_failures) {
        std::printf("test_libipu_api: FAIL\n");
        return 1;
    }
    std::printf("test_libipu_api: PASS (init RST/BCLR effects, sync, stop/restart snapshot round trip)\n");
    return 0;
}
