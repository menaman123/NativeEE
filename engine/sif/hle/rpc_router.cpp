#include "engine/sif/hle/rpc_router.h"

#include <cstdio>

void RpcRouter::RegisterServer(uint32_t server_id, Server& server, const char* name) {
    for (ServerRow& r : servers_) {
        if (r.server_id == server_id) {
            r.server = &server;
            r.name = name;
            return;
        }
    }
    servers_.push_back(ServerRow{server_id, &server, name});
}

const RpcRouter::Server* RpcRouter::FindServer(uint32_t server_id) const {
    for (const ServerRow& r : servers_)
        if (r.server_id == server_id) return r.server;
    return nullptr;
}

const char* RpcRouter::ServerName(uint32_t server_id) const {
    for (const ServerRow& r : servers_)
        if (r.server_id == server_id) return r.name;
    return "unbound";
}

// A re-bind of a known client updates its server id in place; otherwise the
// first free slot is taken (the original's RegisterBinding, sif_hle.cpp:582-602).
int RpcRouter::Bind(uint32_t client_addr, uint32_t server_id) {
    for (int i = 0; i < kMaxBindings; ++i) {
        if (bindings_[i].bound && bindings_[i].client_addr == client_addr) {
            bindings_[i].server_id = server_id;
            return i;
        }
    }
    for (int i = 0; i < kMaxBindings; ++i) {
        if (!bindings_[i].bound) {
            bindings_[i] = Binding{client_addr, server_id, true};
            return i;
        }
    }
    return -1;
}

const RpcRouter::Binding* RpcRouter::FindByClient(uint32_t client_addr) const {
    for (const Binding& b : bindings_)
        if (b.bound && b.client_addr == client_addr) return &b;
    return nullptr;
}

const RpcRouter::Binding* RpcRouter::FindByServer(uint32_t server_id) const {
    for (const Binding& b : bindings_)
        if (b.bound && b.server_id == server_id) return &b;
    return nullptr;
}

int RpcRouter::BoundCount() const {
    int n = 0;
    for (const Binding& b : bindings_) n += b.bound ? 1 : 0;
    return n;
}

bool RpcRouter::Call(const CallRequest& req) {
    ++calls_;
    for (const ServerRow& r : servers_) {
        if (r.server_id == req.server_id) {
            r.server->Call(req);
            return true;
        }
    }
    ++unbound_calls_;
    return false;
}

// Each callback runs on the boundary's context with a0 = end_param and
// ra = the return sentinel; the caller's argument/return registers are
// restored afterwards (the borrow is invisible to the interrupted thread).
int RpcRouter::CompletePending(CpuContext& ctx, const GuestCall& call, const ReadWord& read) {
    int ran = 0;
    while (!pending_.empty()) {
        const uint32_t client = pending_.front();
        pending_.pop_front();
        const uint32_t end_func = read(client + kClientEndFunc);
        const uint32_t end_param = read(client + kClientEndParam);
        if (end_func == 0) continue;
        const auto saved_a0 = ctx.cpuRegs.GPR.r[4];
        const auto saved_v0 = ctx.cpuRegs.GPR.r[2];
        const auto saved_ra = ctx.cpuRegs.GPR.r[31];
        const uint32_t saved_pc = ctx.cpuRegs.pc;
        ctx.cpuRegs.GPR.r[4].UD[0] = end_param;
        ctx.cpuRegs.GPR.r[31].UD[0] = kReturnSentinel;
        call(end_func, ctx);
        ctx.cpuRegs.GPR.r[4] = saved_a0;
        ctx.cpuRegs.GPR.r[2] = saved_v0;
        ctx.cpuRegs.GPR.r[31] = saved_ra;
        ctx.cpuRegs.pc = saved_pc;
        ++ran;
        ++completed_;
    }
    return ran;
}

std::string RpcRouter::BoundCensus() const {
    std::string out;
    for (const Binding& b : bindings_) {
        if (!b.bound) continue;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s@0x%x", ServerName(b.server_id), b.server_id);
        out += (out.empty() ? "" : ",") + std::string(buf);
    }
    return out;
}
