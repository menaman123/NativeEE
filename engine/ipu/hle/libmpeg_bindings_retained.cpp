#include "engine/ipu/hle/libmpeg_bindings.h"

#include <utility>

#include "engine/ipu/hle/dmac_channel_face.h"
#include "engine/ipu/hle/libipu_api.h"

namespace ps2r::ipu {

LibmpegBindings::LibmpegBindings(MpegApi& api, LibipuApi& ipu, DmacChannelFace& dmac, GuestMemory& memory,
                                 const FmvContextLayout& layout, Hooks hooks)
    : LibmpegBindings(api, memory, layout, std::move(hooks), RetainedInitialization(ipu, dmac)) {}

LibmpegBindings::RetainedInit LibmpegBindings::RetainedInitialization(LibipuApi& ipu, DmacChannelFace& dmac) {
    return [&ipu, &dmac] {
        // Existing model initialization, unchanged. No selected software
        // service requires these references or executes this callback.
        for (uint32_t ch : {kChannelIpuFrom, kChannelIpuTo}) {
            dmac.WriteChcr(ch, dmac.ReadChcr(ch) & ~kChcrStr);
            dmac.WriteQwc(ch, 0);
        }
        ipu.Init();
    };
}

}  // namespace ps2r::ipu
