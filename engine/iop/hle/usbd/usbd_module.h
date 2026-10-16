// engine/iop/hle/usbd/usbd_module.h -- UsbdModule: Sony's USB driver library
// (usbd) as a hosted module sees it, over a bus with NO device attached: a
// driver registers (sceUsbdRegisterLdd, idx 4) and is kept, its probe/connect
// callbacks never fire because nothing is plugged in; unregister (5) drops it;
// the descriptor / endpoint / transfer entries (6-13, 18) answer the no-device
// results (null descriptor, no private data, no endpoint, transfer refused) --
// a driver only reaches them from its connect callback, which never fires. Title-neutral: the
// stock module's idle face, no title name or address.
#pragma once

#include <cstdint>
#include <vector>

#include "engine/iop/iop_context.h"
#include "engine/iop/iop_memory.h"

namespace ps2r::iop::hle {

class UsbdModule {
public:
    static constexpr int kNoDevice = -1;                // USB_RC_BADDEV family: nothing attached

    explicit UsbdModule(IopMemory& mem) : mem_(&mem) {}

    void sceUsbdRegisterLdd(IopContext& ctx);          // int (UsbdLddOps*) -> 0
    void sceUsbdUnregisterLdd(IopContext& ctx);        // int (UsbdLddOps*) -> 0
    void sceUsbdScanStaticDescriptor(IopContext& ctx); // void* (devid, data, type) -> 0
    void sceUsbdSetPrivateData(IopContext& ctx);       // int (devid, data) -> kNoDevice
    void sceUsbdGetPrivateData(IopContext& ctx);       // void* (devid) -> 0
    void sceUsbdOpenPipe(IopContext& ctx);             // int (devid, desc) -> kNoDevice
    void sceUsbdClosePipe(IopContext& ctx);            // int (pipe) -> kNoDevice
    void sceUsbdTransferPipe(IopContext& ctx);         // int (pipe, data, len, option, cb, arg) -> kNoDevice
    void sceUsbdOpenPipeAligned(IopContext& ctx);      // int (devid, desc) -> kNoDevice (idx 12)
    void sceUsbdGetDeviceLocation(IopContext& ctx);    // int (devid, u8* location) -> kNoDevice (idx 13)
    void sceUsbdMultiIsochronousTransfer(IopContext& ctx);   // int (pipe, request, cb, arg) -> kNoDevice (idx 18)

    const std::vector<uint32_t>& Drivers() const { return drivers_; }

private:
    IopMemory* mem_;   // the bus's RAM (a device's descriptors would be written here)
    std::vector<uint32_t> drivers_;
};

}  // namespace ps2r::iop::hle
