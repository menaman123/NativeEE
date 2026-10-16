// LibpadBindings implementation; the row table is in the header.
#include "engine/pad/hle/libpad_bindings.h"

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/pad/hle/pad_protocol.h"

namespace {
PadProtocol* g_protocol = nullptr;
LibpadBindings::Statics g_statics;
LibpadBindings::Now g_now;
uint64_t g_open_calls = 0, g_read_calls = 0, g_get_state_calls = 0, g_reads_served = 0;

using Handler = void (*)(CpuContext&);
struct Row {
    const char* name;
    Handler handler;
};
const Row kRows[] = {
    {"scePadPortOpen", &LibpadBindings::PortOpen},         {"scePadPortClose", &LibpadBindings::PortClose},
    {"scePadGetDmaStr", &LibpadBindings::GetDmaStr},       {"scePadRead", &LibpadBindings::Read},
    {"scePadGetState", &LibpadBindings::GetState},         {"scePadSetReqState", &LibpadBindings::SetReqState},
    {"scePadGetReqState", &LibpadBindings::GetReqState},   {"scePadInfoAct", &LibpadBindings::InfoAct},
    {"scePadInfoMode", &LibpadBindings::InfoMode},         {"scePadSetMainMode", &LibpadBindings::SetMainMode},
    {"scePadSetActDirect", &LibpadBindings::SetActDirect}, {"scePadSetActAlign", &LibpadBindings::SetActAlign},
    {"scePadGetButtonMask", &LibpadBindings::GetButtonMask}, {"scePadSetButtonInfo", &LibpadBindings::SetButtonInfo},
    {"scePadEnterPressMode", &LibpadBindings::EnterPressMode}, {"scePadGetModVersion", &LibpadBindings::GetModVersion},
    {"scePadInit2", &LibpadBindings::Init2},               {"_send_to_iop", &LibpadBindings::SendToIop},
    {"_pad_intr_hdr", &LibpadBindings::IntrHandler},
};
constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);
const char* g_row_names[kRowCount];

// The DualShock 2 actuator descriptors, [actno][term-1] = {func, sub, size, curr}.
constexpr uint8_t kActuatorTable[LibpadBindings::kActuators][4] = {
    {0x00, 0x01, 0x01, 0xFF},   // small motor
    {0x01, 0x01, 0x01, 0xFF},   // big motor
};

uint32_t A(const CpuContext& ctx, int i) { return ctx.cpuRegs.GPR.r[4 + i].UL[0]; }
void Ret(CpuContext& ctx, int32_t v) { ctx.cpuRegs.GPR.r[2].SD[0] = v; }
void RetU(CpuContext& ctx, uint32_t v) { ctx.cpuRegs.GPR.r[2].SD[0] = static_cast<int32_t>(v); }
bool ValidEeAddress(uint32_t a) { return a != 0 && MainMemory::Phys(a) < MainMemory::kSize; }
bool InRange(uint32_t port, uint32_t slot) { return port <= LibpadBindings::kMaxPort && slot <= LibpadBindings::kMaxSlot; }

uint32_t PortEntry(uint32_t port, uint32_t slot) { return g_statics.port_table + port * 0x70 + slot * 0x1c; }
uint32_t OpenFlag(uint32_t port, uint32_t slot) { return g_statics.open_flags + port * 0x1c + slot * 7; }
uint32_t StaticBuffer(uint32_t port, uint32_t slot) { return g_statics.static_area + port * 0x80 + slot * 0x20; }

bool Opened(uint32_t port, uint32_t slot) {
    return InRange(port, slot) && memory::read<uint32_t>(PortEntry(port, slot) + 0x10) != 0;
}

// The slot libpad's reader picks: the half with the larger +0x58 counter
// (`slt` on the two words, so equal counters pick slot 0). 0 when unopened.
uint32_t ActiveSlot(uint32_t port, uint32_t slot) {
    if (!Opened(port, slot)) return 0;
    const uint32_t area = memory::read<uint32_t>(PortEntry(port, slot));
    if (!ValidEeAddress(area)) return 0;
    const int32_t f0 = memory::read<int32_t>(area + PadProtocol::kSeqOffset);
    const int32_t f1 = memory::read<int32_t>(area + PadProtocol::kSlotBytes + PadProtocol::kSeqOffset);
    return f0 < f1 ? area + PadProtocol::kSlotBytes : area;
}

// The accepted-command shape shared by the SetXxx rows: reqState BUSY on the
// active slot (the SDK's scePadSetReqState(port, slot, 2) after a PADMAN reply
// of 1); 0 when the slot was never opened.
void AcceptCommand(CpuContext& ctx) {
    const uint32_t port = A(ctx, 0), slot = A(ctx, 1);
    if (!Opened(port, slot)) { Ret(ctx, 0); return; }
    if (const uint32_t s = ActiveSlot(port, slot)) memory::write<uint8_t>(s + 0x71, PadProtocol::kReqBusy);
    Ret(ctx, 1);
}
}  // namespace

int LibpadBindings::Bind(SdkBindingManifest& m, PadProtocol* protocol, Statics statics, Now now) {
    g_protocol = protocol;
    g_statics = statics;
    g_now = std::move(now);
    int n = 0;
    for (const Row& r : kRows) {
        m.Register("libpad.a", r.name, r.handler);
        ++n;
    }
    return n;
}

const char* const* LibpadBindings::RowNames(size_t* count) {
    for (size_t i = 0; i < kRowCount; i++) g_row_names[i] = kRows[i].name;
    *count = kRowCount;
    return g_row_names;
}

uint64_t LibpadBindings::OpenCalls() { return g_open_calls; }
uint64_t LibpadBindings::ReadCalls() { return g_read_calls; }
uint64_t LibpadBindings::GetStateCalls() { return g_get_state_calls; }
uint64_t LibpadBindings::ReadsServed() { return g_reads_served; }

void LibpadBindings::PortOpen(CpuContext& ctx) {
    ++g_open_calls;
    const uint32_t port = A(ctx, 0), slot = A(ctx, 1), area = A(ctx, 2);
    if (!InRange(port, slot) || !ValidEeAddress(area) || g_statics.port_table == 0) { Ret(ctx, 0); return; }
    const uint32_t entry = PortEntry(port, slot);
    const uint32_t static_buffer = StaticBuffer(port, slot);
    const uint32_t pad_id = port * 2 + slot + 1;
    memory::write<uint8_t>(OpenFlag(port, slot), 1);
    memory::write<uint32_t>(entry + 0x00, area);
    memory::write<uint32_t>(entry + 0x04, static_buffer);
    memory::write<uint32_t>(entry + 0x08, pad_id);
    memory::write<uint32_t>(entry + 0x0C, 0);
    memory::write<uint32_t>(entry + 0x10, 1);
    memory::write<uint32_t>(static_buffer, 0);
    if (g_protocol && port < static_cast<uint32_t>(PadProtocol::kPorts))
        g_protocol->RegisterArea(static_cast<int>(port), area, g_now ? g_now() : 0);
    Ret(ctx, static_cast<int32_t>(pad_id));
}

void LibpadBindings::PortClose(CpuContext& ctx) {
    const uint32_t port = A(ctx, 0), slot = A(ctx, 1);
    if (!Opened(port, slot)) { Ret(ctx, 0); return; }
    const uint32_t entry = PortEntry(port, slot);
    memory::write<uint8_t>(OpenFlag(port, slot), 0);
    memory::write<uint32_t>(entry + 0x00, 0);
    memory::write<uint32_t>(entry + 0x10, 0);
    if (g_protocol && port < static_cast<uint32_t>(PadProtocol::kPorts)) g_protocol->ClearArea(static_cast<int>(port));
    Ret(ctx, 1);
}

void LibpadBindings::GetDmaStr(CpuContext& ctx) {
    const uint32_t port = A(ctx, 0), slot = A(ctx, 1), dst = A(ctx, 2);
    const uint32_t selected = ActiveSlot(port, slot);
    if (selected == 0) { Ret(ctx, 0); return; }
    if (ValidEeAddress(dst))
        for (uint32_t i = 0; i < kSlotBytes; i += 8) memory::write<uint64_t>(dst + i, memory::read<uint64_t>(selected + i));
    RetU(ctx, selected);
}

void LibpadBindings::Read(CpuContext& ctx) {
    ++g_read_calls;
    const uint32_t port = A(ctx, 0), slot = A(ctx, 1), rdata = A(ctx, 2);
    if (!ValidEeAddress(rdata)) { Ret(ctx, 0); return; }
    const uint32_t selected = ActiveSlot(port, slot);
    if (selected == 0) { Ret(ctx, 0); return; }
    uint32_t length = memory::read<uint32_t>(selected + 0x60);
    if (length > kSlotBytes) length = kSlotBytes;
    for (uint32_t i = 0; i < length; i++) memory::write<uint8_t>(rdata + i, memory::read<uint8_t>(selected + i));
    if (length >= 0x20) ++g_reads_served;
    RetU(ctx, length);
}

void LibpadBindings::GetState(CpuContext& ctx) {
    ++g_get_state_calls;
    const uint32_t selected = ActiveSlot(A(ctx, 0), A(ctx, 1));
    if (selected == 0) { Ret(ctx, kStateDisconnected); return; }
    const uint8_t state = memory::read<uint8_t>(selected + 0x70);
    const uint8_t req_state = memory::read<uint8_t>(selected + 0x71);
    Ret(ctx, state == PadProtocol::kStateStable && req_state == PadProtocol::kReqBusy ? kStateFindCtp1 : state);
}

void LibpadBindings::SetReqState(CpuContext& ctx) {
    const uint32_t selected = ActiveSlot(A(ctx, 0), A(ctx, 1));
    if (selected == 0) { Ret(ctx, 0); return; }
    memory::write<uint8_t>(selected + 0x71, static_cast<uint8_t>(A(ctx, 2)));
    Ret(ctx, 1);
}

void LibpadBindings::GetReqState(CpuContext& ctx) {
    const uint32_t selected = ActiveSlot(A(ctx, 0), A(ctx, 1));
    Ret(ctx, selected ? memory::read<uint8_t>(selected + 0x71) : 0);
}

void LibpadBindings::InfoAct(CpuContext& ctx) {
    const uint32_t port = A(ctx, 0), slot = A(ctx, 1);
    const int32_t actno = static_cast<int32_t>(A(ctx, 2)), term = static_cast<int32_t>(A(ctx, 3));
    if (!Opened(port, slot)) { Ret(ctx, 0); return; }
    if (actno == -1) { Ret(ctx, kActuators); return; }
    if (actno < 0 || actno >= kActuators || term < 1 || term > 4) { Ret(ctx, 0); return; }
    Ret(ctx, kActuatorTable[actno][term - 1]);
}

void LibpadBindings::InfoMode(CpuContext& ctx) {
    const int32_t info_mode = static_cast<int32_t>(A(ctx, 2));
    const uint32_t index = A(ctx, 3);
    const uint32_t selected = ActiveSlot(A(ctx, 0), A(ctx, 1));
    if (selected == 0) { Ret(ctx, 0); return; }
    if (memory::read<uint8_t>(selected + 0x72) != 1 || memory::read<uint8_t>(selected + 0x73) == 2) { Ret(ctx, 0); return; }
    const uint8_t mode_config = memory::read<uint8_t>(selected + 0x64);
    const uint8_t mode_cur_id = memory::read<uint8_t>(selected + 0x65);
    const uint32_t mode_table = selected + 0x50;   // u16 entries, nrOfModes at +0x68
    switch (info_mode) {
        case 1:   // PAD_MODECURID: 0xF3 = not ready
            Ret(ctx, mode_cur_id == 0xF3 ? 0 : mode_cur_id >> 4);
            return;
        case 2:   // PAD_MODECURTABLE
            if (mode_config == 1) { Ret(ctx, 0); return; }
            RetU(ctx, memory::read<uint16_t>(mode_table + (index & 0xF) * 2));
            return;
        case 3:   // PAD_MODECUREXID
            Ret(ctx, mode_config == 1 ? 0 : memory::read<uint8_t>(selected + 0x69));
            return;
        case 4: {   // PAD_MODECURCOUNT
            if (mode_config == 1) { Ret(ctx, 0); return; }
            const uint8_t nr_of_modes = memory::read<uint8_t>(selected + 0x68);
            if (index == 0xFFFFFFFFu) { Ret(ctx, nr_of_modes); return; }
            if (static_cast<int32_t>(index) < static_cast<int32_t>(nr_of_modes)) {
                RetU(ctx, memory::read<uint16_t>(mode_table + (index & 0xF) * 2));
                return;
            }
            Ret(ctx, 0);
            return;
        }
        default:
            Ret(ctx, 0);
            return;
    }
}

void LibpadBindings::SetMainMode(CpuContext& ctx) { AcceptCommand(ctx); }
void LibpadBindings::SetActAlign(CpuContext& ctx) { AcceptCommand(ctx); }
void LibpadBindings::SetButtonInfo(CpuContext& ctx) { AcceptCommand(ctx); }
void LibpadBindings::EnterPressMode(CpuContext& ctx) { AcceptCommand(ctx); }

void LibpadBindings::SetActDirect(CpuContext& ctx) {
    const uint32_t port = A(ctx, 0), slot = A(ctx, 1), data = A(ctx, 2);
    const uint32_t selected = ActiveSlot(port, slot);
    if (selected == 0 || memory::read<uint8_t>(selected + 0x72) != 1) { Ret(ctx, 0); return; }
    const uint32_t static_buffer = memory::read<uint32_t>(PortEntry(port, slot) + 0x04);
    if (ValidEeAddress(static_buffer)) {
        if (ValidEeAddress(data))
            for (uint32_t i = 0; i < 6; i++) memory::write<uint8_t>(static_buffer + 0x0C + i, memory::read<uint8_t>(data + i));
        memory::write<uint32_t>(static_buffer + 0x04, 1);   // command: set actuators
        memory::write<uint32_t>(static_buffer + 0x08, 6);   // payload length
    }
    Ret(ctx, 1);
}

void LibpadBindings::GetButtonMask(CpuContext& ctx) {
    const uint32_t selected = ActiveSlot(A(ctx, 0), A(ctx, 1));
    if (selected == 0) { Ret(ctx, 0); return; }
    if (memory::read<uint8_t>(selected + 0x72) != 1 || memory::read<uint8_t>(selected + 0x64) < 2 ||
        memory::read<uint8_t>(selected + 0x66) < 2) {
        Ret(ctx, 0);
        return;
    }
    RetU(ctx, (static_cast<uint32_t>(memory::read<uint8_t>(selected + 0x7C)) << 24) |
                  (static_cast<uint32_t>(memory::read<uint8_t>(selected + 0x7B)) << 16) |
                  (static_cast<uint32_t>(memory::read<uint8_t>(selected + 0x7A)) << 8) |
                  static_cast<uint32_t>(memory::read<uint8_t>(selected + 0x79)));
}

void LibpadBindings::GetModVersion(CpuContext& ctx) { Ret(ctx, kModVersion); }
void LibpadBindings::Init2(CpuContext& ctx) { Ret(ctx, 1); }
void LibpadBindings::SendToIop(CpuContext& ctx) { Ret(ctx, 1); }
void LibpadBindings::IntrHandler(CpuContext& ctx) { Ret(ctx, 0); }
