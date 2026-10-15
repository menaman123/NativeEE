// engine/ipu/hle/libipu_api.cpp -- LibipuApi bodies (see the header).
#include "engine/ipu/hle/libipu_api.h"

namespace ps2r::ipu {

void LibipuApi::Init() {
    ++operations_.init;
    // The SDK's init brackets its table seeding with RST + BCLR on both ends;
    // both brackets are issued so a partially initialised unit is fully cleared.
    ipu_.WriteCtrl(IpuDevice::kCtrlRst);  // RST: clear FIFOs, bp = 0, BUSY = 0
    ipu_.WriteCmd(0x00000000u);           // BCLR: clear the input FIFO, bp = 0
    ipu_.WriteCtrl(IpuDevice::kCtrlRst);  // RST (the closing bracket)
    ipu_.WriteCmd(0x00000000u);           // BCLR
}

int32_t LibipuApi::Sync(int32_t mode) {
    ++operations_.sync;
    if (mode == 0) {
        for (uint32_t i = 0; i < kMaxBusyPolls; ++i) {
            if ((ipu_.ReadCtrl() & IpuDevice::kCtrlBusy) == 0) break;
        }
        return 0;
    }
    if (mode == 1) return static_cast<int32_t>(ipu_.ReadCtrl() >> 31);
    return 0;
}

void LibipuApi::StopDma(LibipuDmaSnapshot& out) {
    ++operations_.stop;
    // Steps 1 + 4: halt both IPU channels.
    dmac_.WriteChcr(kChannelIpuFrom, dmac_.ReadChcr(kChannelIpuFrom) & ~kChcrStr);
    dmac_.WriteChcr(kChannelIpuTo, dmac_.ReadChcr(kChannelIpuTo) & ~kChcrStr);

    // Step 2: the IPU_TO (ch4) registers.
    out.ipu_to_madr = dmac_.ReadMadr(kChannelIpuTo);
    out.ipu_to_tadr = dmac_.ReadTadr(kChannelIpuTo);
    out.ipu_to_qwc = dmac_.ReadQwc(kChannelIpuTo);
    out.ipu_to_chcr = dmac_.ReadChcr(kChannelIpuTo);

    // Step 3: wait for the pipeline to drain (one CTRL read; BUSY never lingers).
    (void)ipu_.ReadCtrl();

    // Step 5: the IPU_FROM (ch3) registers.
    out.ipu_from_madr = dmac_.ReadMadr(kChannelIpuFrom);
    out.ipu_from_qwc = dmac_.ReadQwc(kChannelIpuFrom);
    out.ipu_from_chcr = dmac_.ReadChcr(kChannelIpuFrom);

    // Step 6: the unit's bit pointer and control word.
    out.ipu_bp = ipu_.ReadBp();
    out.ipu_ctrl = ipu_.ReadCtrl();

    // Input is buffered beyond the hardware's eight-qword IFC. Represent
    // that excess as source data still pending, so BCLR + rewind reloads it.
    // The nine guest words carry the complete resume state.
    const size_t supplied = ipu_.InputSize() / 16;
    const size_t consumed = ipu_.BitPosition() / 128;
    const size_t unread = supplied > consumed ? supplied - consumed : 0;
    const uint32_t reported = RewindQuadwords(out.ipu_bp);
    if (out.ipu_to_qwc > kMaxDmaQwc || unread > kMaxDmaQwc - out.ipu_to_qwc ||
        unread > out.ipu_to_madr / 16 || (ipu_.InputSize() % 16) != 0) {
        // A deliberately invalid QWC makes rejection survive guest storage.
        // Keep the FIFO intact; RestartDma rejects before BCLR or any kick.
        out.ipu_to_qwc = kMaxDmaQwc + 1;
        ++dma_snapshot_errors_;
    } else if (unread > reported) {
        const uint32_t excess = static_cast<uint32_t>(unread - reported);
        out.ipu_to_madr -= excess * 16;
        out.ipu_to_qwc += excess;
    }
}

uint32_t LibipuApi::RewindQuadwords(uint32_t saved_bp) {
    const uint32_t fp = (saved_bp >> 16) & 0x3u;
    const uint32_t ifc = (saved_bp >> 8) & 0xFu;
    return fp + ifc;
}

void LibipuApi::RestartDma(const LibipuDmaSnapshot& snap) {
    ++operations_.restart;
    const uint32_t delta = RewindQuadwords(snap.ipu_bp);
    if (snap.ipu_to_qwc > kMaxDmaQwc || delta > kMaxDmaQwc - snap.ipu_to_qwc ||
        delta > snap.ipu_to_madr / 16) {
        ++dma_snapshot_errors_;
        return;
    }

    // IPU_FROM first, only when it had pending work.
    if (snap.ipu_from_qwc != 0) {
        dmac_.WriteMadr(kChannelIpuFrom, snap.ipu_from_madr);
        dmac_.WriteQwc(kChannelIpuFrom, snap.ipu_from_qwc);
        dmac_.WriteChcr(kChannelIpuFrom, snap.ipu_from_chcr | kChcrStr);
    }

    // Wait for BUSY to clear, re-issue the saved command, wait again.
    (void)ipu_.ReadCtrl();
    ipu_.WriteCmd(snap.ipu_bp & 0x7Fu);
    (void)ipu_.ReadCtrl();

    // IPU_TO rewound by the quadwords the unit still held, only when it had work.
    const uint32_t madr = snap.ipu_to_madr - delta * 16u;
    const uint32_t qwc = snap.ipu_to_qwc + delta;
    if (qwc != 0) {
        dmac_.WriteMadr(kChannelIpuTo, madr);
        dmac_.WriteTadr(kChannelIpuTo, snap.ipu_to_tadr);
        dmac_.WriteQwc(kChannelIpuTo, qwc);
        dmac_.WriteChcr(kChannelIpuTo, snap.ipu_to_chcr | kChcrStr);
    }
}

}  // namespace ps2r::ipu
