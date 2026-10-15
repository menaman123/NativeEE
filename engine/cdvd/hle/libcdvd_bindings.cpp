// LibcdvdBindings implementation; the row table is in the header.
#include "engine/cdvd/hle/libcdvd_bindings.h"

#include <string>
#include <vector>

#include "engine/core/log/log.h"
#include "engine/cdvd/read_engine.h"
#include "engine/cdvd/stream_reader.h"
#include "engine/ee/hle/sdk_binding_manifest.h"
#include "engine/ee/mem/scratchpad.h"
#include "engine/ee/recomp_abi/host_app/memory.h"
#include "engine/ee/timing/derived_clocks.h"

namespace {
ReadEngine* g_engine = nullptr;
const DerivedClocks* g_clocks = nullptr;
StreamReader* g_stream = nullptr;
uint64_t g_read_clock_calls = 0, g_read_calls = 0, g_search_calls = 0, g_stream_calls = 0;

using Handler = void (*)(CpuContext&);
struct Row {
    const char* name;
    Handler handler;
};
const Row kRows[] = {
    {"sceCdInit", &LibcdvdBindings::Init},               {"sceCdMmode", &LibcdvdBindings::Mmode},
    {"sceCdDiskReady", &LibcdvdBindings::DiskReady},     {"sceCdDiskReady_old", &LibcdvdBindings::DiskReady},
    {"sceCdSync", &LibcdvdBindings::Sync},               {"sceCdSyncS", &LibcdvdBindings::Sync},
    {"sceCdGetError", &LibcdvdBindings::GetError},       {"sceCdBreak", &LibcdvdBindings::Break},
    {"sceCdStatus", &LibcdvdBindings::Status},           {"sceCdGetDiskType", &LibcdvdBindings::GetDiskType},
    {"sceCdReadClock", &LibcdvdBindings::ReadClock},     {"sceCdRead", &LibcdvdBindings::Read},
    {"sceCdSearchFile", &LibcdvdBindings::SearchFile},   {"sceCdLayerSearchFile", &LibcdvdBindings::SearchFile},
    {"cmd_sem_init", &LibcdvdBindings::CmdSemInit},      {"_sceCd_scmd_prechk", &LibcdvdBindings::Prechk},
    {"_sceCd_ncmd_prechk", &LibcdvdBindings::Prechk},    {"cdvd_exit", &LibcdvdBindings::Exit},
    {"_sceCd_Poff_Intr", &LibcdvdBindings::PoffIntr},   {"sceCdStInit", &LibcdvdBindings::StInit},
    {"sceCdStRead", &LibcdvdBindings::StRead},           {"sceCdStream", &LibcdvdBindings::Stream},
    {"sceCdStStart", &LibcdvdBindings::StStart},         {"sceCdStSeek", &LibcdvdBindings::StSeek},
    {"sceCdStStop", &LibcdvdBindings::StStop},
};
constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);
const char* g_row_names[kRowCount];

uint32_t A(const CpuContext& ctx, int i) { return ctx.cpuRegs.GPR.r[4 + i].UL[0]; }
void Ret(CpuContext& ctx, int32_t v) { ctx.cpuRegs.GPR.r[2].SD[0] = v; }
// Sector-block destinations (sceCdRead / sceCdStRead / sceCdStream buffers)
// are copied as one host block and must lie in main RAM.
bool ValidEeAddress(uint32_t a) { return a != 0 && (a & 0x1FFFFFFFu) < 0x02000000u; }
// Record pointers (sceCdlFILE, sceCdCLOCK, the sceCdStRead error word) are
// written word by word through the guest address map: the EE library fills
// them itself from the SIF reply, so any EE-mapped address is legal --
// main RAM or the 16 KB scratchpad (this title keeps its sceCdlFILE there).
bool ValidRecordAddress(uint32_t a, uint32_t bytes) {
    if (ValidEeAddress(a)) return true;
    return Scratchpad::Contains(a) && Scratchpad::Offset(a) + bytes <= Scratchpad::kSize;
}

std::string ReadGuestString(uint32_t addr, size_t max) {
    std::string s;
    for (size_t i = 0; i < max; i++) {
        const char c = static_cast<char>(memory::read<uint8_t>(addr + static_cast<uint32_t>(i)));
        if (c == 0) break;
        s.push_back(c);
    }
    return s;
}
}  // namespace

int LibcdvdBindings::Bind(SdkBindingManifest& m, ReadEngine* engine, const DerivedClocks* clocks, StreamReader* stream) {
    g_engine = engine;
    g_clocks = clocks;
    g_stream = stream;
    int n = 0;
    for (const Row& r : kRows) {
        m.Register("libcdvd.a", r.name, r.handler);
        ++n;
    }
    return n;
}

const char* const* LibcdvdBindings::RowNames(size_t* count) {
    for (size_t i = 0; i < kRowCount; i++) g_row_names[i] = kRows[i].name;
    *count = kRowCount;
    return g_row_names;
}

uint64_t LibcdvdBindings::ReadClockCalls() { return g_read_clock_calls; }
uint64_t LibcdvdBindings::ReadCalls() { return g_read_calls; }
uint64_t LibcdvdBindings::SearchCalls() { return g_search_calls; }
uint64_t LibcdvdBindings::StreamCalls() { return g_stream_calls; }

void LibcdvdBindings::Init(CpuContext& ctx) { Ret(ctx, 1); }
void LibcdvdBindings::Mmode(CpuContext& ctx) { Ret(ctx, 1); }
void LibcdvdBindings::DiskReady(CpuContext& ctx) { Ret(ctx, static_cast<int32_t>(kDiskReadyComplete)); }
void LibcdvdBindings::Sync(CpuContext& ctx) { Ret(ctx, 0); }
void LibcdvdBindings::GetError(CpuContext& ctx) { Ret(ctx, 0); }
void LibcdvdBindings::Break(CpuContext& ctx) { Ret(ctx, 1); }
void LibcdvdBindings::Status(CpuContext& ctx) { Ret(ctx, static_cast<int32_t>(kStatusPause)); }
void LibcdvdBindings::GetDiskType(CpuContext& ctx) { Ret(ctx, static_cast<int32_t>(kDiskTypePs2Dvd)); }
void LibcdvdBindings::CmdSemInit(CpuContext& ctx) { Ret(ctx, 0); }
void LibcdvdBindings::Prechk(CpuContext& ctx) { Ret(ctx, 1); }
void LibcdvdBindings::Exit(CpuContext& ctx) { Ret(ctx, 1); }
void LibcdvdBindings::PoffIntr(CpuContext& ctx) { Ret(ctx, 0); }

// sceCdCLOCK: {stat, second, minute, hour, pad, day, month, year}, BCD; year
// is the two low decimal digits (the RTC counts from 2000).
void LibcdvdBindings::ReadClock(CpuContext& ctx) {
    ++g_read_clock_calls;
    const uint32_t out = A(ctx, 0);
    if (!ValidRecordAddress(out, kClockBytes)) {
        Ret(ctx, 0);
        return;
    }
    DerivedClocks::RtcTime t{};
    if (g_clocks) t = g_clocks->Rtc();
    else t = DerivedClocks::EpochToCivil(DerivedClocks::kRtcFixedEpochS);
    const uint8_t bytes[kClockBytes] = {
        0,
        ToBcd(static_cast<unsigned>(t.second)),
        ToBcd(static_cast<unsigned>(t.minute)),
        ToBcd(static_cast<unsigned>(t.hour)),
        0,
        ToBcd(static_cast<unsigned>(t.day)),
        ToBcd(static_cast<unsigned>(t.month)),
        ToBcd(static_cast<unsigned>(t.year % 100)),
    };
    for (uint32_t i = 0; i < kClockBytes; i++) memory::write<uint8_t>(out + i, bytes[i]);
    Ret(ctx, 1);
}

// sceCdRead(lbn, sectors, buf, mode): every sector lands in EE RAM inside
// the call (a block copy into main RAM); 0 when there is no disc image, the
// range is off the image, or the buffer leaves main RAM.
void LibcdvdBindings::Read(CpuContext& ctx) {
    ++g_read_calls;
    const uint32_t lbn = A(ctx, 0), sectors = A(ctx, 1), buf = A(ctx, 2);
    if (!g_engine || !ValidEeAddress(buf) || sectors == 0) {
        Ret(ctx, 0);
        return;
    }
    const size_t bytes = static_cast<size_t>(sectors) * IsoFs::kSectorSize;
    uint8_t* dst = memory::get_pointer(buf, bytes);
    if (!dst) {
        Ret(ctx, 0);
        return;
    }
    Ret(ctx, g_engine->ReadSectors(lbn, sectors, dst) == sectors ? 1 : 0);
}

// The stream rows (StreamReader): sceCdStInit(bufmax, bankmax, buf, mode) and
// sceCdStRead(sectors, buf, mode, err), the SDK's internal dispatcher
// sceCdStream(lbn, sectors, buf, command, mode), and the thin wrappers
// sceCdStStart(lbn, mode) / sceCdStSeek(lbn) / sceCdStStop() -- each the
// dispatcher's one command; the generated TU hooks those wrappers at their own
// entry (0x2c4890 / 0x2c48d0 / 0x2c4900 in this title), so the rows exist here
// too. (The generated TU's "sceCdStStat/Pause/Resume" hook sites are the finder's
// sceCdGetError / sceCdBreak / sceCdMmode entries: those rows stay theirs.)
void LibcdvdBindings::StInit(CpuContext& ctx) {
    ++g_stream_calls;
    Ret(ctx, g_stream ? static_cast<int32_t>(g_stream->Init(A(ctx, 0), A(ctx, 1), A(ctx, 2))) : 0);
}

void LibcdvdBindings::StRead(CpuContext& ctx) {
    ++g_stream_calls;
    const uint32_t sectors = A(ctx, 0), buf = A(ctx, 1), err_ptr = A(ctx, 3);
    uint32_t err = 1, got = 0;
    if (g_stream && ValidEeAddress(buf)) {
        uint8_t* dst = memory::get_pointer(buf, static_cast<size_t>(sectors) * IsoFs::kSectorSize);
        if (dst) got = g_stream->Read(sectors, dst, &err);
    }
    if (ValidRecordAddress(err_ptr, 4)) memory::write<uint32_t>(err_ptr, err);
    Ret(ctx, static_cast<int32_t>(got));
}

void LibcdvdBindings::StStart(CpuContext& ctx) {
    ++g_stream_calls;
    Ret(ctx, g_stream ? static_cast<int32_t>(g_stream->Start(A(ctx, 0))) : 0);
}

void LibcdvdBindings::StSeek(CpuContext& ctx) {
    ++g_stream_calls;
    Ret(ctx, g_stream ? static_cast<int32_t>(g_stream->Seek(A(ctx, 0))) : 0);
}

void LibcdvdBindings::StStop(CpuContext& ctx) {
    ++g_stream_calls;
    Ret(ctx, g_stream ? static_cast<int32_t>(g_stream->Stop()) : 0);
}

void LibcdvdBindings::Stream(CpuContext& ctx) {
    ++g_stream_calls;
    if (!g_stream) {
        Ret(ctx, 0);
        return;
    }
    const uint32_t lbn = A(ctx, 0), sectors = A(ctx, 1), buf = A(ctx, 2), command = A(ctx, 3);
    uint32_t result = 0;
    switch (command) {
        case StreamReader::kCmdStart: result = g_stream->Start(lbn); break;
        case StreamReader::kCmdStop: result = g_stream->Stop(); break;
        case StreamReader::kCmdSeek:
        case StreamReader::kCmdSeekF: result = g_stream->Seek(lbn); break;
        case StreamReader::kCmdInit: result = g_stream->Init(lbn, sectors, buf); break;
        case StreamReader::kCmdStat: result = g_stream->Stat(); break;
        case StreamReader::kCmdPause: result = g_stream->Pause(); break;
        case StreamReader::kCmdResume: result = g_stream->Resume(); break;
        case StreamReader::kCmdRead: {
            // sceCdStream's READ carries no error pointer (the SDK's
            // sceCdStRead wrapper owns *err); the ring's error is dropped here.
            uint32_t err = 1;
            uint8_t* dst = ValidEeAddress(buf) ? memory::get_pointer(buf, static_cast<size_t>(sectors) * IsoFs::kSectorSize) : nullptr;
            result = dst ? g_stream->Read(sectors, dst, &err) : 0;
            break;
        }
        default: result = 0; break;
    }
    Ret(ctx, static_cast<int32_t>(result));
}

// sceCdlFILE: {u32 lsn, u32 size, char name[16], u8 date[8]}; the record is
// zeroed first so a miss never leaves stale fields.
void LibcdvdBindings::SearchFile(CpuContext& ctx) {
    ++g_search_calls;
    const uint32_t fp = A(ctx, 0), name_addr = A(ctx, 1);
    if (!ValidRecordAddress(fp, kFileRecordBytes) || !ValidRecordAddress(name_addr, 1)) {
        PS2R_LOG(CDVD, Info, "sceCdSearchFile fp=0x%08x name=0x%08x -> rejected (unmapped)", fp, name_addr);
        Ret(ctx, 0);
        return;
    }
    for (uint32_t i = 0; i < kFileRecordBytes; i += 4) memory::write<uint32_t>(fp + i, 0);
    if (!g_engine) {
        Ret(ctx, 0);
        return;
    }
    IsoFs::Entry e;
    const std::string name = ReadGuestString(name_addr, 256);
    if (!g_engine->Find(name, e)) {
        PS2R_LOG(CDVD, Info, "sceCdSearchFile \"%s\" -> miss", name.c_str());
        Ret(ctx, 0);
        return;
    }
    PS2R_LOG(CDVD, Info, "sceCdSearchFile \"%s\" -> lba=%u size=%u", name.c_str(), e.lba, e.size);
    memory::write<uint32_t>(fp, e.lba);
    memory::write<uint32_t>(fp + 4, e.size);
    for (uint32_t i = 0; i < 16; i++) memory::write<uint8_t>(fp + 8 + i, i < e.name.size() ? static_cast<uint8_t>(e.name[i]) : 0);
    // sceCdlFILE.date: {pad, second, minute, hour, pad, day, month, year} BCD, from the recording date.
    const uint8_t date[8] = {0, ToBcd(e.date[5]), ToBcd(e.date[4]), ToBcd(e.date[3]), 0, ToBcd(e.date[2]), ToBcd(e.date[1]),
                             ToBcd(static_cast<unsigned>((e.date[0] + 1900) % 100))};
    for (uint32_t i = 0; i < 8; i++) memory::write<uint8_t>(fp + 24 + i, date[i]);
    Ret(ctx, 1);
}
