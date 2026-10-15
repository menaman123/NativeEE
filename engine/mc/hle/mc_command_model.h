#pragma once
// McCommandModel: the mcserv command state machine the libmc rows drive.
//
// libmc is asynchronous: every sceMc* command is a kick (returns 0 when the
// RPC was accepted) and sceMcSync delivers its completion later as the pair
// (cmd code, result). The model keeps exactly that pair:
//
//   Issue(cmd, result)      the kick; a kick while a DIFFERENT command's
//                           completion is still undelivered replaces it,
//                           except the stateless GetInfo watchdog kick, which
//                           is dropped so it never destroys a save-flow
//                           completion (its result is recomputed every kick).
//   Sync(mode, ...)         1 = the pending command completed (pair delivered,
//                           pending cleared), 0 = still executing, -1 = idle.
//                           Only a POLL (mode != 0) of a pending GetInfo is
//                           held "executing" for `latency_polls` polls
//                           (cdvd.mc_latency_polls; 0 under the deterministic
//                           profile): a blocking Sync (mode 0) delivers at once,
//                           and save-flow commands are never held (the SDK
//                           contract: the blocking caller trusts the result;
//                           holding Open/GetDir starved the title's save
//                           state machine). Documented divergence: real MCSERV
//                           completes across frames; this is a poll count.
//   GetInfo(port, slot)     the card-state-transition result (libmc contract):
//                           the FIRST probe of a (port, slot) reports -1
//                           "switched to a formatted card", every later probe
//                           of the unchanged card reports 0 "same card"; the
//                           card is type 2 (PS2), formatted, 0x1F00 free
//                           clusters (a fresh 8 MB card).
//   Bound()                 libmc 3.0.1's readiness predicate: every row but
//                           sceMcSync gates with -100 (NotReady) on the
//                           bound-server word of libmc's own sceSifClientData
//                           (`serve`, +0x24), non-zero once sceMcInit's
//                           sceSifBindRpc to mcserv (0x80000400) returned. On
//                           a cold boot the mcserv face raises it when it
//                           serves the init command; a restored anchor derives
//                           it from the client record its RAM image carries
//                           (DeriveBound) -- the word is EE data, not a flag
//                           of the model's own.
//
// Command codes are Sony libmc 3.0.1's (the values sceMcSync writes to *cmd).
#include <cstdint>

class McCommandModel {
public:
    enum Cmd : int32_t {
        kCmdNone = 0,
        kCmdGetInfo = 0x1,
        kCmdOpen = 0x2,
        kCmdClose = 0x3,
        kCmdRead = 0x5,
        kCmdWrite = 0x6,
        kCmdMkdir = 0xB,
        kCmdChdir = 0xC,
        kCmdGetDir = 0xD,
        kCmdDelete = 0xF,
        kCmdFormat = 0x10,
    };
    static constexpr int32_t kErrNotReady = -100;   // subsystem not bound
    static constexpr int32_t kResChangedFormatted = -1;
    static constexpr int32_t kResSameCard = 0;
    static constexpr int32_t kCardTypePs2 = 2;
    static constexpr int32_t kCardFormatted = 1;
    static constexpr int32_t kFreeClusters = 0x1F00;
    static constexpr int kPorts = 2, kSlots = 2;

    struct GetInfoResult {
        int32_t type = kCardTypePs2;
        int32_t free = kFreeClusters;
        int32_t format = kCardFormatted;
        int32_t result = kResSameCard;
    };
    struct SyncResult {
        int32_t ret = -1;   // 1 complete / 0 executing / -1 idle
        int32_t cmd = 0;
        int32_t result = 0;
    };

    explicit McCommandModel(int latency_polls) : latency_polls_(latency_polls < 0 ? 0 : latency_polls) {}

    void SetBound(bool b) { bound_ = b; }
    bool Bound() const { return bound_; }
    // Savestate restore: Bound from the restored client record's bound-server
    // word (libmc's `cd.serve`, the value sceSifBindRpc wrote; 0 = never bound).
    void DeriveBound(uint32_t serve_word);
    bool BoundDerived() const { return bound_derived_; }

    void Issue(int32_t cmd, int32_t result);
    SyncResult Sync(uint32_t mode);
    GetInfoResult GetInfo(uint32_t port, uint32_t slot);

    int32_t PendingCmd() const { return pending_cmd_; }
    int32_t PendingResult() const { return pending_result_; }
    int BusyPollsLeft() const { return busy_polls_; }
    bool Seen(uint32_t port, uint32_t slot) const { return port < kPorts && slot < kSlots && seen_[port][slot]; }
    void SetSeen(uint32_t port, uint32_t slot, bool v);   // savestate restore
    // Savestate restore of the undelivered (cmd, result) pair: the next
    // sceMcSync delivers it at once (no poll latency survives a load).
    void SetPending(int32_t cmd, int32_t result);
    uint64_t DroppedGetInfoKicks() const { return dropped_getinfo_; }
    uint64_t Clobbers() const { return clobbers_; }

private:
    int latency_polls_;
    bool bound_ = false;
    bool bound_derived_ = false;   // Bound came from a restored client record
    int32_t pending_cmd_ = kCmdNone;
    int32_t pending_result_ = 0;
    int busy_polls_ = 0;
    bool seen_[kPorts][kSlots] = {{false, false}, {false, false}};
    uint64_t dropped_getinfo_ = 0, clobbers_ = 0;
};
