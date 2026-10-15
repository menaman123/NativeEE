// tests/pad/test_libpad_bindings.cpp -- the libpad rows over PadProtocol and the
// padman RPC face, against a test EE RAM (PHASE4_PLAN section 2.8): PortOpen's
// six table writes and pad id, the open-sentinel gates, the active-slot pick
// (larger +0x58), GetState's (6, 2) -> 5, Read's length copy, the accepted-
// command shape, SetActDirect's static buffer, GetButtonMask, InfoAct; the
// padman opcodes' results fanned out to +0x00 / +0x10 / +0x14 and the open
// opcode's area registration.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/mem/address_map.h"
#include "engine/ee/mem/fast_mem.h"
#include "engine/ee/mem/main_memory.h"
#include "engine/ee/mem/mmio_router.h"
#include "engine/ee/mem/mmio_sink.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/recomp_abi/recomp_abi.h"
#include "engine/pad/hle/libpad_bindings.h"
#include "engine/pad/hle/pad_protocol.h"
#include "engine/pad/hle/padman_module.h"

static int g_fail = 0;
#define CHECK(cond, name) \
    do { \
        if (cond) std::printf("PASS %s\n", name); \
        else { std::printf("FAIL %s\n", name); g_fail++; } \
    } while (0)

static CpuContext g_ctx;
// The title-side statics live in the test RAM at arbitrary addresses (the
// engine never knows them); the pad area and the caller buffers elsewhere.
static constexpr uint32_t kPortTable = 0x00400000, kOpenFlags = 0x00401000, kStaticArea = 0x00402000;
static constexpr uint32_t kArea = 0x00410000, kRdata = 0x00420000, kActData = 0x00430000, kSend = 0x00440000, kRecv = 0x00441000;
static uint64_t g_now = 0;

static uint32_t Call(void (*fn)(CpuContext&), uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
    g_ctx.cpuRegs.GPR.r[4].UL[0] = a0; g_ctx.cpuRegs.GPR.r[5].UL[0] = a1;
    g_ctx.cpuRegs.GPR.r[6].UL[0] = a2; g_ctx.cpuRegs.GPR.r[7].UL[0] = a3;
    g_ctx.cpuRegs.GPR.r[2].UD[0] = 0xDEADBEEFull;
    fn(g_ctx);
    return g_ctx.cpuRegs.GPR.r[2].UL[0];
}
static int32_t S(uint32_t v) { return static_cast<int32_t>(v); }

int main() {
    static MainMemory ram;
    static Scratchpad spad;
    static AddressMap map(ram, spad);
    static MmioSink sink;
    static MmioRouter mmio(sink);
    static FastMem fm(ram, map, mmio);
    RecompAbi::BindMemory(&fm);

    PadProtocol pad([](uint32_t a, uint32_t v, int size) {
        if (size == 1) memory::write<uint8_t>(a, static_cast<uint8_t>(v));
        else if (size == 2) memory::write<uint16_t>(a, static_cast<uint16_t>(v));
        else memory::write<uint32_t>(a, v);
    });
    LibpadBindings::Statics statics;
    statics.port_table = kPortTable;
    statics.open_flags = kOpenFlags;
    statics.static_area = kStaticArea;
    SdkBindingManifest& manifest = SdkBindingManifest::Instance();
    const int rows = LibpadBindings::Bind(manifest, &pad, statics, [] { return g_now; });
    size_t names = 0;
    LibpadBindings::RowNames(&names);
    CHECK(rows == 19 && names == 19, "nineteen_libpad_rows_registered");

    // --- the unopened gates ---------------------------------------------------
    CHECK(S(Call(&LibpadBindings::GetState, 0, 0)) == 0x63, "unopened_get_state_is_disconn_0x63");
    CHECK(Call(&LibpadBindings::Read, 0, 0, kRdata) == 0, "unopened_read_is_0");
    CHECK(Call(&LibpadBindings::SetMainMode, 0, 0, 1, 3) == 0, "unopened_set_main_mode_is_0");
    CHECK(Call(&LibpadBindings::InfoAct, 0, 0, 0xFFFFFFFFu, 0) == 0, "unopened_info_act_is_0");
    CHECK(Call(&LibpadBindings::PortOpen, 2, 0, kArea) == 0 && Call(&LibpadBindings::PortOpen, 0, 0, 0) == 0,
          "port_open_rejects_port_2_and_null_area");

    // --- scePadPortOpen: the six writes, the pad id, the protocol's area -------
    g_now = 100;
    CHECK(Call(&LibpadBindings::PortOpen, 0, 0, kArea) == 1, "port_open_returns_pad_id_1");
    CHECK(memory::read<uint8_t>(kOpenFlags) == 1 && memory::read<uint32_t>(kPortTable + 0x00) == kArea &&
              memory::read<uint32_t>(kPortTable + 0x04) == kStaticArea && memory::read<uint32_t>(kPortTable + 0x08) == 1 &&
              memory::read<uint32_t>(kPortTable + 0x0C) == 0 && memory::read<uint32_t>(kPortTable + 0x10) == 1,
          "port_open_writes_flag_area_static_id_zero_sentinel");
    CHECK(pad.Area(0) == kArea && pad.OpenedAt(0) == 100, "port_open_registers_area_at_now");
    CHECK(Call(&LibpadBindings::PortOpen, 1, 0, kArea + 0x100) == 3 &&
              memory::read<uint32_t>(kPortTable + 0x70 + 0x04) == kStaticArea + 0x80 && memory::read<uint8_t>(kOpenFlags + 0x1c) == 1,
          "port_1_open_id_3_strides_0x70_0x80_0x1c");

    // --- staging through the rows: DISCONN then READY --------------------------
    PadProtocol::Input idle;
    pad.Stamp(idle, 100);
    CHECK(S(Call(&LibpadBindings::GetState, 0, 0)) == 0, "disconn_stamp_get_state_0");
    CHECK(Call(&LibpadBindings::GetReqState, 0, 0) == 2, "disconn_req_state_busy");
    PadProtocol::Input in;
    in.buttons = 0x4008;   // CROSS (bit 14) + START (bit 3)
    in.lx = 0x11;
    pad.Stamp(in, 100 + PadProtocol::kConfigUntil);
    CHECK(S(Call(&LibpadBindings::GetState, 0, 0)) == 6, "ready_stamp_get_state_stable_6");
    // The active slot is slot 1 (seq + 1): GetDmaStr returns it and copies 128 bytes.
    const uint32_t selected = Call(&LibpadBindings::GetDmaStr, 0, 0, kRdata);
    CHECK(selected == kArea + 0x80 && memory::read<uint8_t>(kRdata + 0x02) == 0xF7 && memory::read<uint8_t>(kRdata + 0x03) == 0xBF &&
              memory::read<uint8_t>(kRdata + 0x70) == 6,
          "get_dma_str_picks_slot_1_and_copies_the_image");
    for (uint32_t i = 0; i < 0x40; i++) memory::write<uint8_t>(kRdata + i, 0xEE);
    CHECK(Call(&LibpadBindings::Read, 0, 0, kRdata) == 0x20 && memory::read<uint8_t>(kRdata + 0x00) == 0 &&
              memory::read<uint8_t>(kRdata + 0x01) == PadProtocol::kModeAnalog && memory::read<uint8_t>(kRdata + 0x02) == 0xF7 &&
              memory::read<uint8_t>(kRdata + 0x06) == 0x11 && memory::read<uint8_t>(kRdata + 0x0E) == 0xFF && memory::read<uint8_t>(kRdata + 0x20) == 0xEE,
          "read_copies_0x20_bytes_of_the_report");
    CHECK(LibpadBindings::ReadsServed() == 1 && LibpadBindings::ReadCalls() == 2, "read_census_served_1_of_2");
    // The accepted-command shape: reqState 2 on the active slot -> GetState 5.
    CHECK(Call(&LibpadBindings::SetMainMode, 0, 0, 1, 3) == 1 && memory::read<uint8_t>(selected + 0x71) == 2 &&
              S(Call(&LibpadBindings::GetState, 0, 0)) == 5,
          "set_main_mode_accepted_state_6_req_2_reads_5");
    CHECK(Call(&LibpadBindings::SetReqState, 0, 0, 0) == 1 && S(Call(&LibpadBindings::GetState, 0, 0)) == 6, "set_req_state_0_back_to_6");
    CHECK(Call(&LibpadBindings::GetButtonMask, 0, 0) == 0x3FFFF, "button_mask_0x3ffff");
    CHECK(Call(&LibpadBindings::InfoAct, 0, 0, 0xFFFFFFFFu, 0) == 2 && Call(&LibpadBindings::InfoAct, 0, 0, 1, 1) == 1 &&
              Call(&LibpadBindings::InfoAct, 0, 0, 1, 4) == 0xFF && Call(&LibpadBindings::InfoAct, 0, 0, 2, 1) == 0,
          "info_act_dualshock2_table");
    CHECK(Call(&LibpadBindings::InfoMode, 0, 0, 1, 0) == (PadProtocol::kModeCurIdAnalog >> 4), "info_mode_cur_id_7");
    for (uint32_t i = 0; i < 6; i++) memory::write<uint8_t>(kActData + i, static_cast<uint8_t>(0xA0 + i));
    CHECK(Call(&LibpadBindings::SetActDirect, 0, 0, kActData) == 1 && memory::read<uint32_t>(kStaticArea + 0x04) == 1 &&
              memory::read<uint32_t>(kStaticArea + 0x08) == 6 && memory::read<uint8_t>(kStaticArea + 0x0C) == 0xA0 &&
              memory::read<uint8_t>(kStaticArea + 0x11) == 0xA5,
          "set_act_direct_fills_the_static_buffer");
    CHECK(Call(&LibpadBindings::GetModVersion) == 0x0300 && Call(&LibpadBindings::Init2) == 1 &&
              Call(&LibpadBindings::IntrHandler) == 0,
          "mod_version_0x300_init2_1_intr_0");
    CHECK(Call(&LibpadBindings::PortClose, 0, 0) == 1 && memory::read<uint32_t>(kPortTable + 0x10) == 0 && pad.Area(0) == 0 &&
              S(Call(&LibpadBindings::GetState, 0, 0)) == 0x63,
          "port_close_clears_sentinel_and_area");

    // --- PadmanModule: the opcodes' results, the fan-out, the open registration -
    PadmanModule padman(
        pad, [](uint32_t a) { return memory::read<uint32_t>(a); }, [](uint32_t a, uint8_t v) { memory::write<uint8_t>(a, v); },
        [](uint32_t a, uint32_t v) { memory::write<uint32_t>(a, v); }, [] { return g_now; });
    RpcRouter::CallRequest req;
    req.server_id = PadmanModule::kCommandServerId;
    req.func = 1;
    req.send = kSend;
    req.send_size = 0x10;
    req.recv = kRecv;
    req.recv_size = 0x20;
    memory::write<uint32_t>(kSend, PadmanModule::kOpInit);
    padman.Call(req);
    CHECK(padman.InitCalls() == 1 && memory::read<uint32_t>(kRecv) == 1 && memory::read<uint32_t>(kRecv + 0x10) == 1 &&
              memory::read<uint32_t>(kRecv + 0x14) == 1,
          "padman_init_1_fanned_to_00_10_14");
    memory::write<uint32_t>(kSend, PadmanModule::kOpOpen);
    memory::write<uint32_t>(kSend + 4, 0);
    memory::write<uint32_t>(kSend + 8, 0);
    memory::write<uint32_t>(kSend + 12, kArea);
    g_now = 500;
    padman.Call(req);
    CHECK(padman.OpenCalls() == 1 && pad.Area(0) == kArea && pad.OpenedAt(0) == 500 && memory::read<uint32_t>(kRecv) == 1,
          "padman_open_registers_the_area");
    memory::write<uint32_t>(kSend, PadmanModule::kOpGetState);
    padman.Call(req);
    CHECK(memory::read<uint32_t>(kRecv) == 2, "padman_get_state_stable_2");
    memory::write<uint32_t>(kSend, PadmanModule::kOpInfoMode);
    padman.Call(req);
    CHECK(memory::read<uint32_t>(kRecv) == 0 && memory::read<uint32_t>(kRecv + 0x14) == 0, "padman_info_mode_0");
    memory::write<uint32_t>(kSend, PadmanModule::kOpRead);
    padman.Call(req);
    CHECK(memory::read<uint8_t>(kRecv + 1) == 0x70 && memory::read<uint8_t>(kRecv + 2) == 0xFF && memory::read<uint8_t>(kRecv + 6) == 0x80,
          "padman_read_idle_report");
    memory::write<uint32_t>(kSend, 0x42);
    padman.Call(req);
    CHECK(padman.UnknownCalls() == 1 && memory::read<uint32_t>(kRecv) == 1 && padman.Calls() == 6, "padman_unknown_op_1_counted");

    std::printf(g_fail ? "FAILED %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
