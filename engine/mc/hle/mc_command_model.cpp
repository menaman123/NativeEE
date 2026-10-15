// McCommandModel implementation; the contract is described in the header.
#include "engine/mc/hle/mc_command_model.h"

void McCommandModel::DeriveBound(uint32_t serve_word) {
    // libmc's `cd.serve`: sceSifBindRpc's bound-server handle, 0 until sceMcInit ran.
    bound_ = serve_word != 0;
    bound_derived_ = true;
}

void McCommandModel::Issue(int32_t cmd, int32_t result) {
    if (pending_cmd_ != kCmdNone && pending_cmd_ != cmd) {
        if (cmd == kCmdGetInfo) {
            ++dropped_getinfo_;   // the watchdog re-kicks every frame; dropping one is lossless
            return;
        }
        ++clobbers_;
    }
    pending_cmd_ = cmd;
    pending_result_ = result;
    busy_polls_ = latency_polls_;
}

McCommandModel::SyncResult McCommandModel::Sync(uint32_t mode) {
    SyncResult r;
    if (pending_cmd_ == kCmdNone) {
        r.ret = -1;
        return r;
    }
    if (mode != 0 && pending_cmd_ == kCmdGetInfo && busy_polls_ > 0) {
        --busy_polls_;
        r.ret = 0;
        r.cmd = pending_cmd_;
        r.result = 0;
        return r;
    }
    r.ret = 1;
    r.cmd = pending_cmd_;
    r.result = pending_result_;
    pending_cmd_ = kCmdNone;
    pending_result_ = 0;
    busy_polls_ = 0;
    return r;
}

McCommandModel::GetInfoResult McCommandModel::GetInfo(uint32_t port, uint32_t slot) {
    GetInfoResult g;
    if (port < kPorts && slot < kSlots) {
        if (!seen_[port][slot]) {
            seen_[port][slot] = true;
            g.result = kResChangedFormatted;
        }
    }
    return g;
}

void McCommandModel::SetSeen(uint32_t port, uint32_t slot, bool v) {
    if (port < kPorts && slot < kSlots) seen_[port][slot] = v;
}

void McCommandModel::SetPending(int32_t cmd, int32_t result) {
    pending_cmd_ = cmd;
    pending_result_ = result;
    busy_polls_ = 0;
}
