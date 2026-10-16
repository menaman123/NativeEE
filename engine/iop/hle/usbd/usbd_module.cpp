// engine/iop/hle/usbd/usbd_module.cpp -- UsbdModule: the idle USB bus.
#include "engine/iop/hle/usbd/usbd_module.h"

namespace ps2r::iop::hle {

void UsbdModule::sceUsbdRegisterLdd(IopContext& ctx) {
    if (ctx.gpr[4]) drivers_.push_back(ctx.gpr[4]);
    ctx.gpr[2] = 0;
}

void UsbdModule::sceUsbdUnregisterLdd(IopContext& ctx) {
    for (size_t i = 0; i < drivers_.size(); ++i) {
        if (drivers_[i] == ctx.gpr[4]) {
            drivers_.erase(drivers_.begin() + static_cast<long>(i));
            break;
        }
    }
    ctx.gpr[2] = 0;
}

void UsbdModule::sceUsbdScanStaticDescriptor(IopContext& ctx) { ctx.gpr[2] = 0; }
void UsbdModule::sceUsbdSetPrivateData(IopContext& ctx) { ctx.gpr[2] = static_cast<uint32_t>(kNoDevice); }
void UsbdModule::sceUsbdGetPrivateData(IopContext& ctx) { ctx.gpr[2] = 0; }
void UsbdModule::sceUsbdOpenPipe(IopContext& ctx) { ctx.gpr[2] = static_cast<uint32_t>(kNoDevice); }
void UsbdModule::sceUsbdClosePipe(IopContext& ctx) { ctx.gpr[2] = static_cast<uint32_t>(kNoDevice); }
void UsbdModule::sceUsbdTransferPipe(IopContext& ctx) { ctx.gpr[2] = static_cast<uint32_t>(kNoDevice); }
void UsbdModule::sceUsbdOpenPipeAligned(IopContext& ctx) { ctx.gpr[2] = static_cast<uint32_t>(kNoDevice); }
void UsbdModule::sceUsbdGetDeviceLocation(IopContext& ctx) {
    if (ctx.gpr[5] && mem_) mem_->Write<uint8_t>(ctx.gpr[5], 0);   // no device: an empty location
    ctx.gpr[2] = static_cast<uint32_t>(kNoDevice);
}
void UsbdModule::sceUsbdMultiIsochronousTransfer(IopContext& ctx) { ctx.gpr[2] = static_cast<uint32_t>(kNoDevice); }

}  // namespace ps2r::iop::hle
