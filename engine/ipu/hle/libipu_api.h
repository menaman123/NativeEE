// engine/ipu/hle/libipu_api.h -- LibipuApi: the libipu.a SDK entry points as pure
// register sequences over the IpuDevice and the two IPU DMA channels.
//
// libipu's public functions only poke the IPU registers and the DMAC channels
// 3 (IPU_FROM) and 4 (IPU_TO): sceIpuInit resets the unit and clears its input
// FIFO, sceIpuSync waits for CTRL.BUSY to clear, sceIpuStopDMA halts both
// channels and snapshots their registers plus the IPU bit pointer / control word
// into a nine-word save buffer, sceIpuRestartDMA replays that snapshot with the
// IPU_TO channel rewound by the bytes the unit still had buffered. This class
// performs those sequences; copying the snapshot to/from guest memory and binding
// the entry points to the SDK manifest are the integration's (README table).
#pragma once

#include <cstdint>

#include "engine/ipu/hle/dmac_channel_face.h"
#include "engine/ipu/ipu_device.h"

namespace ps2r::ipu {

// The sceIpuStopDMA save buffer: nine 32-bit words in the SDK's order.
// For the buffered device, TO-MADR/QWC include unread input beyond capped IFC.
// An unrepresentable snapshot has TO-QWC = kMaxDmaQwc + 1 and cannot restart.
struct LibipuDmaSnapshot {
    static constexpr uint32_t kWords = 9;
    uint32_t ipu_to_madr = 0;     // [0] ch4 MADR
    uint32_t ipu_to_tadr = 0;     // [1] ch4 TADR
    uint32_t ipu_to_qwc = 0;      // [2] ch4 QWC
    uint32_t ipu_to_chcr = 0;     // [3] ch4 CHCR (STR already cleared)
    uint32_t ipu_from_madr = 0;   // [4] ch3 MADR
    uint32_t ipu_from_qwc = 0;    // [5] ch3 QWC
    uint32_t ipu_from_chcr = 0;   // [6] ch3 CHCR (STR already cleared)
    uint32_t ipu_bp = 0;          // [7] IPU_BP
    uint32_t ipu_ctrl = 0;        // [8] IPU_CTRL
};

class LibipuApi {
public:
    static constexpr uint32_t kChannelIpuFrom = 3;
    static constexpr uint32_t kChannelIpuTo = 4;
    static constexpr uint32_t kChcrStr = 1u << 8;
    static constexpr uint32_t kMaxDmaQwc = 0xffffu;
    // sceIpuSync mode 0 spins on CTRL.BUSY; the device completes every command
    // inside WriteCmd, so this bound is never reached and only guards the loop.
    static constexpr uint32_t kMaxBusyPolls = 1024;

    LibipuApi(IpuDevice& ipu, DmacChannelFace& dmac) : ipu_(ipu), dmac_(dmac) {}

    // sceIpuInit: RST, BCLR, RST, BCLR -- the bookend resets of the SDK's init
    // sequence.
    // diverges: the quantiser-table seeding between the brackets (SETIQ pushes
    // through the FIFO port) is not performed -- the MPEG path decodes outside
    // the IPU and nothing reads the seeded matrices.
    void Init();

    // sceIpuSync(mode): mode 0 polls CTRL.BUSY (bit 31) until clear and returns
    // 0; mode 1 returns the current BUSY bit; other modes return 0.
    int32_t Sync(int32_t mode);

    // sceIpuStopDMA: clear CHCR.STR on ch3 and ch4, snapshot ch4, read CTRL
    // (the drain wait), snapshot ch3, then BP and CTRL.
    // Fold excess software-buffered input into TO-MADR/QWC, without side state.
    void StopDma(LibipuDmaSnapshot& out);

    // sceIpuRestartDMA: restore ch3 when it had work, issue BCLR with saved
    // BP.BP to IPU_CMD, restore ch4 rewound by (BP.FP + BP.IFC) quadwords.
    void RestartDma(const LibipuDmaSnapshot& snap);

    // Lifetime observer only; rejected Stop/Restart operations, preserved by Init.
    uint64_t DmaSnapshotErrorCount() const { return dma_snapshot_errors_; }
    // Count actual model entry, including calls made directly by libmpeg.
    // Observers never drive behavior and survive Init/reset/restore.
    struct Operations { uint64_t init = 0, sync = 0, stop = 0, restart = 0; };
    Operations Count() const { return operations_; }

    // EE User's Manual 8.3 IPU_BP: rewind data already transferred but unread.
    // Both fields come from snapshot word [7] (IPU_BP), not [8] (IPU_CTRL).
    static uint32_t RewindQuadwords(uint32_t saved_bp);

private:
    IpuDevice& ipu_;
    DmacChannelFace& dmac_;
    uint64_t dma_snapshot_errors_ = 0;
    Operations operations_;
};

}  // namespace ps2r::ipu
