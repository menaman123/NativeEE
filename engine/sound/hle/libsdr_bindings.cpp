// engine/sound/hle/libsdr_bindings.cpp -- the libsdr rows over SdrRemote.
#include "engine/sound/hle/libsdr_bindings.h"

#include <cstdio>
#include <map>

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/sound/hle/sdr_remote.h"

namespace ps2r::sound::hle {
namespace {

SdrRemote* g_remote = nullptr;
uint64_t g_init_calls = 0;
uint64_t g_remote_calls = 0;
std::map<uint32_t, uint64_t> g_cmd_calls;

struct Row {
    const char* name;
    SdkBindingManifest::Handler handler;
};
const Row kRows[LibsdrBindings::kRowCount] = {
    {"sceSdRemoteInit", &LibsdrBindings::RemoteInit},
    {"sceSdRemote", &LibsdrBindings::Remote},
};
const char* g_row_names[LibsdrBindings::kRowCount];

uint32_t A(const CpuContext& ctx, int reg) { return ctx.cpuRegs.GPR.r[reg].UL[0]; }
void Ret(CpuContext& ctx, uint32_t v) { ctx.cpuRegs.GPR.r[2].UD[0] = v; }

}  // namespace

int LibsdrBindings::Bind(SdkBindingManifest& m, SdrRemote* remote) {
    g_remote = remote;
    int n = 0;
    for (const Row& r : kRows) {
        m.Register("libsdr.a", r.name, r.handler);
        ++n;
    }
    return n;
}

const char* const* LibsdrBindings::RowNames(size_t* count) {
    for (size_t i = 0; i < kRowCount; i++) g_row_names[i] = kRows[i].name;
    *count = kRowCount;
    return g_row_names;
}

uint64_t LibsdrBindings::InitCalls() { return g_init_calls; }
uint64_t LibsdrBindings::RemoteCalls() { return g_remote_calls; }

std::string LibsdrBindings::CallsCensus() {
    std::string s = "init=" + std::to_string(g_init_calls) + ",remote=" + std::to_string(g_remote_calls);
    for (const auto& [cmd, n] : g_cmd_calls) {
        char buf[32];
        std::snprintf(buf, sizeof buf, ",0x%x:%llu", cmd, static_cast<unsigned long long>(n));
        s += buf;
    }
    return s;
}

void LibsdrBindings::RemoteInit(CpuContext& ctx) {
    ++g_init_calls;
    Ret(ctx, 0);
}

// a0 = mode, a1 = cmd, a2/a3 = the first two arguments, t0/t1 (r8/r9) the rest.
void LibsdrBindings::Remote(CpuContext& ctx) {
    ++g_remote_calls;
    const uint32_t cmd = A(ctx, 5);
    g_cmd_calls[cmd]++;
    Ret(ctx, g_remote ? g_remote->Dispatch(cmd, A(ctx, 6), A(ctx, 7), A(ctx, 8), A(ctx, 9)) : 0);
}

}  // namespace ps2r::sound::hle
