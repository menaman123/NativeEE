// McservModule implementation; the face is described in the header.
#include "engine/mc/hle/mcserv_module.h"

#include "engine/mc/hle/mc_command_model.h"

void McservModule::Call(const RpcRouter::CallRequest& req) {
    const bool init = req.func == kFnoInit || req.func == kFnoInitLegacy;
    if (init) {
        ++init_calls_;
        init_client_addr_ = req.client_addr;
        model_.SetBound(true);
    } else {
        ++unexpected_calls_;
    }
    if (req.recv == 0 || !write_) return;
    write_(req.recv, 0);   // result
    if (init && req.recv_size >= kReplyBytes) {
        write_(req.recv + 4, kMcservVersion);
        write_(req.recv + 8, kMcmanVersion);
    }
}
