#pragma once
// McservModule: the mcserv IOP module's EE-facing RPC face (server id
// 0x80000400) over McCommandModel. The title's recompiled sceMcInit binds
// this server and issues the init command; every other libmc command is an
// EE-side row (LibmcBindings), so the face serves only what reaches it:
//
//   init (fno 0xFE, the SDK 3.0.1 mcserv id; 0x00 accepted) -> the 12-byte
//        reply {result 0, mcserv version, mcman version} and the model's Bound
//        flag raised -- the observable of "the memory card subsystem is up".
//   any other fno -> result 0 in the reply's first word (the original
//        runtime's stub table), counted as `unexpected` so a leg that reaches
//        one is visible.
//
// The module-table row (RegisterLibraryEntries "mcserv" 2.01) stays the
// plugin's IdleModule row; this class is the RPC endpoint only.
#include <cstdint>
#include <functional>

#include "engine/sif/hle/rpc_router.h"

class McCommandModel;

class McservModule : public RpcRouter::Server {
public:
    static constexpr uint32_t kServerId = 0x80000400;
    static constexpr uint32_t kFnoInit = 0xFE, kFnoInitLegacy = 0x00;
    static constexpr uint32_t kReplyBytes = 12;
    // The init reply's version words are the modules' protocol versions, NOT
    // their RegisterLibraryEntries versions (mcserv 2.01 / mcman 2.07): the disc's
    // MCSERV.IRX carries mcserv_version 0x210 and MCMAN.IRX mcman_version 0x226
    // in .data right after their "PsIImcserv  3010" / "PsIImcman   3010" stamps.
    // libmc 3.0.1's sceMcInit refuses a reply below 0x20A / 0x20E ("libmc: too
    // old release of mcserv.irx" / "... mcman.irx") -- these are what the retail
    // disc answers and what the title accepts.
    static constexpr uint32_t kMcservVersion = 0x210;
    static constexpr uint32_t kMcmanVersion = 0x226;

    using WriteWord = std::function<void(uint32_t addr, uint32_t value)>;

    McservModule(McCommandModel& model, WriteWord write) : model_(model), write_(std::move(write)) {}

    void Call(const RpcRouter::CallRequest& req) override;

    uint64_t InitCalls() const { return init_calls_; }
    uint64_t UnexpectedCalls() const { return unexpected_calls_; }
    // The sceSifClientData the init command arrived on: libmc's own client
    // record, whose bound-server word is the Bound predicate (0 before init).
    uint32_t InitClientAddr() const { return init_client_addr_; }

private:
    McCommandModel& model_;
    WriteWord write_;
    uint64_t init_calls_ = 0, unexpected_calls_ = 0;
    uint32_t init_client_addr_ = 0;
};
