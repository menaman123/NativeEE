#pragma once
// RpcRouter: the EE side of SIF RPC as a table, not a packet stream.
//
//   Bind   -- sceSifBindRpc records (client-data address <-> server id); the
//             SDK client record's `server` word is what the game polls, the
//             router's table is what routes the later calls (the original's
//             sif_bind_rpc tracker, 32 slots, re-bind updates in place).
//   Call   -- sceSifCallRpc resolves the client's server id to the registered
//             Server face (an IOP HLE module, or the recompiled STREAM.IRX
//             through the SifRpcBridge for its server id) and runs it; a call
//             to an unregistered server is the leg's park (`sif_server_unbound`)
//             -- the caller decides, the router only reports `false`.
//   End callbacks -- a call with an end function queues its client record;
//             CompletePending (boundary slot 4, Step::MaintDuties) runs each
//             `end_func(end_param)` as a guest call in queue order, as the
//             original's scheduler pass did (gs_hle.cpp:702-716); the DMAC
//             channel-5 handlers the game registered with AddDmacHandler run
//             from the same duty (PHASE4_PLAN 2.6), never from a D_STAT write.
//
//   Callback context invariant: CompletePending runs each end callback on the
//   boundary thread's LIVE context, saving and restoring only a0/v0/ra/pc (the
//   original ran them on a fresh zeroed context). This is sound because a
//   boundary is reached only from an HLE call site (GsSync / the syscall
//   faces), where the generated caller has already spilled every register it
//   keeps live across the call per the MIPS ABI -- a callback's clobber of
//   the other caller-saved registers is therefore unobservable to the
//   interrupted guest. Should a boundary ever be reached from inside a
//   generated function, the full caller-saved set must be saved here instead.
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "engine/ee/cpu/cpu_state.h"

class RpcRouter {
public:
    static constexpr int kMaxBindings = 32;

    struct Binding {
        uint32_t client_addr = 0;
        uint32_t server_id = 0;
        bool bound = false;
    };
    struct CallRequest {
        uint32_t server_id = 0;
        uint32_t func = 0;
        uint32_t send = 0;
        uint32_t send_size = 0;
        uint32_t recv = 0;
        uint32_t recv_size = 0;
        uint32_t client_addr = 0;
    };
    // The server face of one RPC endpoint (IOP HLE module or the IRX bridge).
    class Server {
    public:
        virtual ~Server() = default;
        virtual void Call(const CallRequest& req) = 0;
    };
    // Runs a guest function with the context prepared (a0 loaded, ra = sentinel).
    using GuestCall = std::function<void(uint32_t addr, CpuContext& ctx)>;
    // Reads the client record's end function / parameter words (EE RAM access
    // is injected so the router carries no memory dependency).
    using ReadWord = std::function<uint32_t(uint32_t addr)>;

    void RegisterServer(uint32_t server_id, Server& server, const char* name);
    const Server* FindServer(uint32_t server_id) const;
    const char* ServerName(uint32_t server_id) const;
    // Every registered server id, registration order (a composition resolving a
    // client's handle word back to its id after a restore).
    std::vector<uint32_t> ServerIds() const;

    int Bind(uint32_t client_addr, uint32_t server_id);   // slot index, -1 when full
    const Binding* FindByClient(uint32_t client_addr) const;
    const Binding* FindByServer(uint32_t server_id) const;
    int BoundCount() const;

    // False when no server is registered for req.server_id (nothing ran).
    bool Call(const CallRequest& req);

    void QueueEndCallback(uint32_t client_addr) { pending_.push_back(client_addr); }
    size_t PendingCount() const { return pending_.size(); }
    // Slot-4 duty: delivers every queued end callback in order; returns the count run.
    int CompletePending(CpuContext& ctx, const GuestCall& call, const ReadWord& read);

    // run.meta `sif_bound=`: "name@sid" per bound client in slot order.
    std::string BoundCensus() const;
    uint64_t Calls() const { return calls_; }
    uint64_t UnboundCalls() const { return unbound_calls_; }
    uint64_t Completed() const { return completed_; }

    // SDK sceSifClientData layout (libkernl 3.0.1): the end-function word and
    // its parameter, written by sceSifCallRpc, read by the completion duty.
    static constexpr uint32_t kClientEndFunc = 0x1C;
    static constexpr uint32_t kClientEndParam = 0x20;
    static constexpr uint32_t kClientServer = 0x24;
    // The return sentinel a delivered callback jumps to.
    static constexpr uint32_t kReturnSentinel = 0x80001000u;

private:
    struct ServerRow {
        uint32_t server_id;
        Server* server;
        const char* name;
    };
    std::vector<ServerRow> servers_;
    Binding bindings_[kMaxBindings];
    std::deque<uint32_t> pending_;
    uint64_t calls_ = 0;
    uint64_t unbound_calls_ = 0;
    uint64_t completed_ = 0;
};
