// LibmcBindings implementation; the row table is in the header.
#include "engine/mc/hle/libmc_bindings.h"

#include <string>
#include <vector>

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/mc/hle/mc_command_model.h"
#include "engine/mc/hle/mc_host_store.h"

namespace {
McCommandModel* g_model = nullptr;
McHostStore* g_store = nullptr;
uint64_t g_sync_calls = 0, g_getinfo_calls = 0, g_getdir_calls = 0, g_open_calls = 0;
bool g_format_in_t0 = false;

using Handler = void (*)(CpuContext&);
struct Row {
    const char* name;
    Handler handler;
};
const Row kRows[] = {
    {"sceMcSync", &LibmcBindings::Sync},       {"sceMcGetInfo", &LibmcBindings::GetInfo},
    {"sceMcOpen", &LibmcBindings::Open},       {"sceMcClose", &LibmcBindings::Close},
    {"sceMcRead", &LibmcBindings::Read},       {"sceMcWrite", &LibmcBindings::Write},
    {"sceMcMkdir", &LibmcBindings::Mkdir},     {"sceMcChdir", &LibmcBindings::Chdir},
    {"sceMcGetDir", &LibmcBindings::GetDir},   {"sceMcDelete", &LibmcBindings::Delete},
    {"sceMcFormat", &LibmcBindings::Format},   {"mceIntrReadFixAlign", &LibmcBindings::Internal},
    {"mceGetInfoApdx", &LibmcBindings::Internal}, {"mceStorePwd", &LibmcBindings::Internal},
};
constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);
const char* g_row_names[kRowCount];

uint32_t A(const CpuContext& ctx, int i) { return ctx.cpuRegs.GPR.r[4 + i].UL[0]; }
uint32_t Sp(const CpuContext& ctx) { return ctx.cpuRegs.GPR.r[29].UL[0]; }
void Ret(CpuContext& ctx, int32_t v) { ctx.cpuRegs.GPR.r[2].SD[0] = v; }
bool ValidEeAddress(uint32_t a) { return a != 0 && (a & 0x1FFFFFFFu) < 0x02000000u; }

std::string ReadGuestString(uint32_t addr, size_t max) {
    std::string s;
    if (!ValidEeAddress(addr)) return s;
    for (size_t i = 0; i < max; i++) {
        const char c = static_cast<char>(memory::read<uint8_t>(addr + static_cast<uint32_t>(i)));
        if (c == 0) break;
        s.push_back(c);
    }
    return s;
}

// True when the row may proceed; otherwise -100 has been returned.
bool Gate(CpuContext& ctx) {
    if (g_model && g_model->Bound()) return true;
    Ret(ctx, McCommandModel::kErrNotReady);
    return false;
}

void Queue(CpuContext& ctx, int32_t cmd, int32_t result) {
    g_model->Issue(cmd, result);
    Ret(ctx, 0);
}

// The deterministic profile's fixed timestamp for both create and modify:
// {0, sec 0, min 0, hour 12, day 15, month 8, year 2026 lo, hi}.
void WriteDirEntry(uint32_t addr, const McHostStore::DirEntry& e) {
    static const uint8_t kStamp[8] = {0, 0, 0, 12, 15, 8, static_cast<uint8_t>(2026 & 0xFF), static_cast<uint8_t>(2026 >> 8)};
    for (uint32_t i = 0; i < 8; i++) {
        memory::write<uint8_t>(addr + i, kStamp[i]);
        memory::write<uint8_t>(addr + 8 + i, kStamp[i]);
    }
    memory::write<uint32_t>(addr + 16, e.size);
    memory::write<uint16_t>(addr + 20, e.is_dir ? McHostStore::kAttrDirectory : McHostStore::kAttrFile);
    memory::write<uint16_t>(addr + 22, 0);
    memory::write<uint32_t>(addr + 24, 0);
    memory::write<uint32_t>(addr + 28, 0);
    for (uint32_t i = 0; i < 32; i++)
        memory::write<uint8_t>(addr + 32 + i, i < e.name.size() ? static_cast<uint8_t>(e.name[i]) : 0);
}
}  // namespace

int LibmcBindings::Bind(SdkBindingManifest& m, McCommandModel* model, McHostStore* store) {
    g_model = model;
    g_store = store;
    int n = 0;
    for (const Row& r : kRows) {
        m.Register("libmc.a", r.name, r.handler);
        ++n;
    }
    return n;
}

const char* const* LibmcBindings::RowNames(size_t* count) {
    for (size_t i = 0; i < kRowCount; i++) g_row_names[i] = kRows[i].name;
    *count = kRowCount;
    return g_row_names;
}

void LibmcBindings::SetFormatInT0(bool on) { g_format_in_t0 = on; }
bool LibmcBindings::FormatInT0() { return g_format_in_t0; }
uint64_t LibmcBindings::SyncCalls() { return g_sync_calls; }
uint64_t LibmcBindings::GetInfoCalls() { return g_getinfo_calls; }
uint64_t LibmcBindings::GetDirCalls() { return g_getdir_calls; }
uint64_t LibmcBindings::OpenCalls() { return g_open_calls; }

void LibmcBindings::Sync(CpuContext& ctx) {
    ++g_sync_calls;
    const uint32_t cmd_ptr = A(ctx, 1), result_ptr = A(ctx, 2);
    McCommandModel::SyncResult r;
    if (g_model) r = g_model->Sync(A(ctx, 0));
    if (ValidEeAddress(cmd_ptr)) memory::write<int32_t>(cmd_ptr, r.cmd);
    if (ValidEeAddress(result_ptr)) memory::write<int32_t>(result_ptr, r.result);
    Ret(ctx, r.ret);
}

void LibmcBindings::GetInfo(CpuContext& ctx) {
    ++g_getinfo_calls;
    if (!Gate(ctx)) return;
    // sceMcGetInfo(port, slot, *type, *free, *format): five arguments, all in
    // registers (EE calling convention: a0-a3 then t0-t3). The inherited read
    // of the fifth from the stack (o32 habit) dereferences a caller's saved
    // word instead -- the title's saved MC-manager pointer -- and the
    // `format = 1` store then lands in that object's header (its slot-count
    // nibble), not in *format. The stack arm stays selectable because the
    // frozen baselines were recorded with it (BUG-089).
    const uint32_t type_ptr = A(ctx, 2), free_ptr = A(ctx, 3);
    const uint32_t format_ptr = g_format_in_t0 ? A(ctx, 4) : memory::read<uint32_t>(Sp(ctx) + 0x10);
    const McCommandModel::GetInfoResult g = g_model->GetInfo(A(ctx, 0), A(ctx, 1));
    if (ValidEeAddress(type_ptr)) memory::write<int32_t>(type_ptr, g.type);
    if (ValidEeAddress(free_ptr)) memory::write<int32_t>(free_ptr, g.free);
    if (ValidEeAddress(format_ptr)) memory::write<int32_t>(format_ptr, g.format);
    Queue(ctx, McCommandModel::kCmdGetInfo, g.result);
}

void LibmcBindings::Open(CpuContext& ctx) {
    ++g_open_calls;
    if (!Gate(ctx)) return;
    const std::string name = ReadGuestString(A(ctx, 2), kMaxPath);
    Queue(ctx, McCommandModel::kCmdOpen, g_store ? g_store->Open(name, A(ctx, 3)) : McHostStore::kResNoEntry);
}

void LibmcBindings::Close(CpuContext& ctx) {
    if (!Gate(ctx)) return;
    if (g_store) g_store->Close(static_cast<int32_t>(A(ctx, 0)));
    Queue(ctx, McCommandModel::kCmdClose, 0);
}

void LibmcBindings::Read(CpuContext& ctx) {
    if (!Gate(ctx)) return;
    const int32_t fd = static_cast<int32_t>(A(ctx, 0));
    const uint32_t buf = A(ctx, 1), size = A(ctx, 2);
    int32_t result = McHostStore::kResDeniedPermit;
    if (g_store && ValidEeAddress(buf) && size < kMaxTransfer) {
        std::vector<uint8_t> bytes;
        result = g_store->Read(fd, bytes, size);
        for (size_t i = 0; i < bytes.size(); i++) memory::write<uint8_t>(buf + static_cast<uint32_t>(i), bytes[i]);
    }
    Queue(ctx, McCommandModel::kCmdRead, result);
}

void LibmcBindings::Write(CpuContext& ctx) {
    if (!Gate(ctx)) return;
    const int32_t fd = static_cast<int32_t>(A(ctx, 0));
    const uint32_t buf = A(ctx, 1), size = A(ctx, 2);
    int32_t result = McHostStore::kResDeniedPermit;
    if (g_store && ValidEeAddress(buf) && size < kMaxTransfer) {
        std::vector<uint8_t> bytes(size);
        for (uint32_t i = 0; i < size; i++) bytes[i] = memory::read<uint8_t>(buf + i);
        result = g_store->Write(fd, bytes.data(), size);
    }
    Queue(ctx, McCommandModel::kCmdWrite, result);
}

void LibmcBindings::Mkdir(CpuContext& ctx) {
    if (!Gate(ctx)) return;
    const std::string path = ReadGuestString(A(ctx, 2), kMaxPath);
    Queue(ctx, McCommandModel::kCmdMkdir, g_store ? g_store->Mkdir(path) : McHostStore::kResNoEntry);
}

void LibmcBindings::Chdir(CpuContext& ctx) {
    if (!Gate(ctx)) return;
    const std::string path = ReadGuestString(A(ctx, 2), kMaxPath);
    Queue(ctx, McCommandModel::kCmdChdir, g_store ? g_store->Chdir(path) : McHostStore::kResNoEntry);
}

// (port, slot, pattern, mode, max, table): max and table are the fifth and
// sixth arguments -- t0/t1 under the EE ABI.
void LibmcBindings::GetDir(CpuContext& ctx) {
    ++g_getdir_calls;
    if (!Gate(ctx)) return;
    const std::string pattern = ReadGuestString(A(ctx, 2), kMaxPath);
    const uint32_t max = ctx.cpuRegs.GPR.r[8].UL[0], table = ctx.cpuRegs.GPR.r[9].UL[0];
    int32_t result = 0;
    if (g_store && ValidEeAddress(table) && max > 0 && max < kMaxDirEntries) {
        std::vector<McHostStore::DirEntry> entries;
        result = g_store->GetDir(pattern, max, entries);
        for (size_t i = 0; i < entries.size(); i++) WriteDirEntry(table + static_cast<uint32_t>(i) * kDirEntryBytes, entries[i]);
    }
    Queue(ctx, McCommandModel::kCmdGetDir, result);
}

void LibmcBindings::Delete(CpuContext& ctx) {
    if (!Gate(ctx)) return;
    const std::string name = ReadGuestString(A(ctx, 2), kMaxPath);
    Queue(ctx, McCommandModel::kCmdDelete, g_store ? g_store->Delete(name) : McHostStore::kResNoEntry);
}

void LibmcBindings::Format(CpuContext& ctx) {
    if (!Gate(ctx)) return;
    Queue(ctx, McCommandModel::kCmdFormat, 0);
}

void LibmcBindings::Internal(CpuContext& ctx) { Ret(ctx, 0); }
