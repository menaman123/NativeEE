// HLE kick lowering against the established GIF parser's sink contract.
// Packet shapes follow the strip and sprite emitters' output layouts; the
// fixtures are encoded independently of both the builder and the lowerer.
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_boundary_hook.h"
#include "engine/gif/gif_marker_owner.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/hle/gs_sink_fanout.h"
#include "engine/gif/hle/gs_state_sink.h"
#include "engine/gs/hle/ir/register_op_stream.h"
#include "engine/gs/state/gs_state.h"
#include "engine/vu/hle/kick_lowering.h"
#include "tests/lib/capture_sink.h"

namespace {

using Bytes = std::vector<uint8_t>;
int checks = 0;
int failures = 0;

#define CHECK(condition, message)                                                        \
    do {                                                                                \
        ++checks;                                                                       \
        if (!(condition)) {                                                             \
            ++failures;                                                                 \
            std::fprintf(stderr, "FAIL line %d: %s (%s)\n", __LINE__, message, #condition); \
        }                                                                               \
    } while (0)

void Word(Bytes& bytes, uint64_t word) {
    for (uint32_t byte = 0; byte < 8; ++byte) bytes.push_back(static_cast<uint8_t>(word >> (byte * 8)));
}

void Quad(Bytes& bytes, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    Word(bytes, uint64_t{a} | (uint64_t{b} << 32));
    Word(bytes, uint64_t{c} | (uint64_t{d} << 32));
}

uint32_t Bits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void Tag(Bytes& bytes, uint16_t loops, uint8_t format, const std::vector<uint8_t>& registers,
         bool eop = true, int prim = -1) {
    CHECK(!registers.empty() && registers.size() <= 16, "fixture descriptor count");
    const uint64_t low = loops | (uint64_t{eop} << 15) | (uint64_t{format} << 58) |
                         ((registers.size() & 15ull) << 60) |
                         (prim < 0 ? 0 : ((uint64_t{1} << 46) | (uint64_t(prim & 0x7ff) << 47)));
    uint64_t high = 0;
    for (size_t i = 0; i < registers.size(); ++i) high |= uint64_t{registers[i]} << (i * 4);
    Word(bytes, low);
    Word(bytes, high);
}

void Append(Bytes& bytes, const Bytes& tail) { bytes.insert(bytes.end(), tail.begin(), tail.end()); }

Bytes Strip(bool fog, uint16_t vertices = 5, bool eop = true) {
    Bytes bytes;
    Tag(bytes, vertices, 0, {2, 1, static_cast<uint8_t>(fog ? 4 : 5)}, eop, 0x15c);
    for (uint32_t i = 0; i < vertices; ++i) {
        // Include signed zero and a zero Q. These are raw STQ payloads:
        // the GS sink owns Q-latch behavior, so lowering must not normalize.
        Quad(bytes, i == 0 ? 0x80000000u : Bits(0.25f * i), Bits(-0.5f * i),
             i == 2 ? 0u : Bits(0.5f + i), 0xfedcba98);
        Quad(bytes, 0xabc00010u + i, 0xdef00020u + i, 0x78900030u + i, 0x45600080u + i);
        const uint32_t x = 0xbeef0000u | (100 * 16 + i);
        const uint32_t y = 0xabcd0000u | (200 * 16 + 3 * i);
        const uint32_t adc = (i < 2 ? 0xffff8000u : 0xffff0000u);
        Quad(bytes, x, y, fog ? (0x123400u + i) << 4 : 0xfe000001u + i,
             adc | (fog ? (0x30u + i) << 4 : 0u));
    }
    return bytes;
}

Bytes Sprite() {
    // sprite_text.cpp writes UV, RGBAQ, XYZ2, UV, XYZ2 for each glyph.
    Bytes bytes;
    Tag(bytes, 2, 0, {3, 1, 5, 3, 5}, true, 0x156);
    for (uint32_t i = 0; i < 2; ++i) {
        Quad(bytes, 0xffffc011, 0xdead8012, 0x33333333, 0x44444444);
        Quad(bytes, 1 + i, 2 + i, 3 + i, 0x80);
        Quad(bytes, 1600, 3200, 0x0f000001, 0x8000);
        Quad(bytes, 0xbeeffffe, 0x1234ffff, 0, 0);
        Quad(bytes, 1760, 3360, 0x0f000002, 0);
    }
    return bytes;
}

// The particle quad emitter's tag (TR particles.cpp: nine descriptors
// {UV,RGBAQ,XYZ2,UV,XYZ2,UV,XYZ2,UV,XYZ2}), two quads.
Bytes Particles() {
    Bytes bytes;
    Tag(bytes, 2, 0, {3, 1, 5, 3, 5, 3, 5, 3, 5}, true, 0x1d6);
    for (uint32_t i = 0; i < 2; ++i) {
        Quad(bytes, 0x10 + i, 0x20, 0, 0);
        Quad(bytes, 0x80, 0x40 + i, 0x20, 0x60);
        Quad(bytes, 1600 + 16 * i, 3200, 0x0a000001, 0x8000);
        Quad(bytes, 0x3ff0 + i, 0x20, 0, 0);
        Quad(bytes, 1760 + 16 * i, 3200, 0x0a000001, 0x8000);
        Quad(bytes, 0x10 + i, 0x3ff0, 0, 0);
        Quad(bytes, 1600 + 16 * i, 3360, 0x0a000001, 0);
        Quad(bytes, 0x3ff0 + i, 0x3ff0, 0, 0);
        Quad(bytes, 1760 + 16 * i, 3360, 0x0a000001, 0);
    }
    return bytes;
}

// The decal strip's tag (TR game/decals.cpp:30-32: UvGifTag NREG 12,
// 4 x {ST,RGBAQ,XYZ2}), two loops, ADC on the first two vertices of each.
Bytes Decals() {
    Bytes bytes;
    Tag(bytes, 2, 0, {2, 1, 5, 2, 1, 5, 2, 1, 5, 2, 1, 5}, true, 0x15c);
    for (uint32_t loop = 0; loop < 2; ++loop) {
        for (uint32_t v = 0; v < 4; ++v) {
            Quad(bytes, Bits(0.25f * v), Bits(0.5f * loop), Bits(1.0f + v), 0);
            Quad(bytes, 0x70 + v, 0x60, 0x50, 0x80);
            Quad(bytes, 2000 + 32 * v, 3000 + 48 * loop, 0x00456789 + v, v < 2 ? 0x8000u : 0u);
        }
    }
    return bytes;
}

Bytes StateWrites() {
    Bytes bytes;
    Tag(bytes, 6, 0, {14}, false);
    // Material state and context selection, followed by vertex records.
    for (const auto& entry : std::array<std::array<uint64_t, 2>, 6>{{
             {0x06, 0x0123456789abcdef}, {0x07, 0xfedcba9876543210},
             {0x42, 0x0000008000000044}, {0x47, 0x00000000000500a4},
             {0x1a, 1}, {0x00, 0xfffffffffffff95c}}}) {
        Word(bytes, entry[1]);
        Word(bytes, 0xdeadbeef00000000ull | entry[0]);
    }
    Append(bytes, Strip(false, 3));
    return bytes;
}

// The material's register block as a shader pass hands it through VU1: one
// PACKED tag, one A+D descriptor, EOP, NLOOP writes (TR AddressDataTag). The
// address rides in the low byte of the high half; the other bits of the high
// half are don't-care and are filled with noise here.
using Writes = std::vector<std::pair<uint8_t, uint64_t>>;

Bytes AddressData(const Writes& writes, int prim = -1) {
    Bytes bytes;
    Tag(bytes, static_cast<uint16_t>(writes.size()), 0, {14}, true, prim);
    uint32_t noise = 0x5a5a0000u;
    for (const auto& write : writes) {
        Word(bytes, write.second);
        Word(bytes, (uint64_t{noise++} << 32) | 0xcafe00u | write.first);
    }
    return bytes;
}

// Seven writes: the plain (untextured) pass's block (TR PlainWrites = 7).
Bytes PlainBlock() {
    return AddressData({{0x47, 0x00000000000500a4}, {0x42, 0x0000008000000044}, {0x4e, 0x0000000000000130},
                        {0x4a, 1}, {0x1b, 0x000000000000004c}, {0x1a, 1}, {0x46, 1}});
}

// Nine writes: the textured pass's block (TR TexturedWrites = 9).
Bytes TexturedBlock() {
    return AddressData({{0x47, 0x00000000000500a4}, {0x42, 0x0000008000000044}, {0x4e, 0x0000000000000130},
                        {0x4a, 0}, {0x1b, 0x000000000000015c}, {0x1a, 1}, {0x46, 1},
                        {0x06, 0x0000000a00123456}, {0x08, 0x0000000000000005}});
}

// Nine writes plus the texture's pairs (TEX1, MIPTBP, CLAMP, TEXA, TEXFLUSH).
Bytes TexturedBlockWithPairs() {
    return AddressData({{0x47, 0x00000000000500a4}, {0x42, 0x0000008000000044}, {0x4e, 0x0000000000000130},
                        {0x4a, 0}, {0x1b, 0x000000000000015c}, {0x1a, 1}, {0x46, 1},
                        {0x06, 0x0000000a00123456}, {0x08, 0x0000000000000005},
                        {0x14, 0x0000000000000160}, {0x34, 0x0123456789abcdef}, {0x36, 0xfedcba9876543210},
                        {0x3b, 0x0000008000000080}, {0x3f, 0}});
}

// Every 64-bit dispatcher class in one block: PRIM (masked), the four vertex
// registers (XYZ2/XYZF2 draw, XYZ3/XYZF3 do not), forwarded registers, and the
// registers the dispatcher drops (reserved 0x0B, A+D, NOP, TEXCLUT, undefined,
// and addresses above 0x7F).
Bytes DispatcherClasses() {
    return AddressData({{0x00, 0xfffffffffffff95c}, {0x01, 0x3f00000080402010}, {0x03, 0x0000000002340123},
                        {0x05, 0x0f00000100c80064}, {0x04, 0x7f12345602000100}, {0x0d, 0xffffffff06000500},
                        {0x0c, 0x8912345604000300}, {0x0b, 0x1111111111111111}, {0x0e, 0x2222222222222222},
                        {0x0f, 0x3333333333333333}, {0x1c, 0x4444444444444444}, {0x20, 0x5555555555555555},
                        {0x7f, 0x6666666666666666}, {0x80, 0x7777777777777777}, {0xff, 0x8888888888888888},
                        {0x60, 0x9999999999999999}, {0x61, 0xaaaaaaaaaaaaaaaa}, {0x62, 0xbbbbbbbbbbbbbbbb}});
}

Bytes RegisterList() {
    Bytes bytes;
    Tag(bytes, 1, 1, {0, 1, 4, 12, 13}, true, 0x123);
    Word(bytes, 0xfffffffffffff956);  // PRIM is 11 bits
    Word(bytes, 0x3f00000080402010);  // RGBAQ retains its Q field in REGLIST
    Word(bytes, 0x7f12345602000100);  // XYZF2 draws, fog 0x7f
    Word(bytes, 0x8912345604000300);  // XYZF3 never draws
    Word(bytes, 0xffffffff06000500);  // XYZ3 uses the complete 32-bit Z
    Word(bytes, 0xdeadbeeffeedface);  // odd high half is padding
    return bytes;
}

struct TestKickLowering : GifBoundaryHook {
    struct QueuedOwner : GifMarkerOwner {
        bool worker = false;
        std::shared_ptr<GifMtvuMarker> marker;
        bool Running() const override { return true; }
        bool OnWorkerThread() const override { return worker; }
        std::shared_ptr<GifMtvuMarker> ActiveMarker() const override { return marker; }
    };
    struct StateRig {
        ps2r::gs::GsState gs;
        ps2r::gs::CaptureSink draws;
        GsStateSink state_sink{gs};
        RegisterOpStream ops;
        GsSinkFanout fanout{&state_sink, &ops};
        GifRegs regs;
        GifParser parser{fanout, regs};
        GifArbiter arbiter{parser};
        KickLowering lowering{arbiter, fanout};
        StateRig() {
            gs.SetSink(&draws);
            state_sink.SetFrameInstance(42);
            arbiter.SetSubmitObserver(&GsStateSink::OnArbiterSubmit, &state_sink);
            arbiter.SetDrainObserver(&GsStateSink::OnArbiterDeliver, &state_sink);
        }
        void Raw(const Bytes& bytes, GifPath path = GifPath::Path1) {
            arbiter.submit(path, bytes.data(), bytes.size());
        }
        void Hle(const Bytes& bytes, bool direct, uint16_t ordinal) {
            state_sink.StageKickIdentity(17, 0x12345678abcdef01ull, 0x180, ordinal);
            if (direct) lowering.Submit(Bytes(bytes));
            else Raw(bytes);
            state_sink.ClearStagedIdentity();
        }
    };
    RegisterOpStream ops;
    GifRegs regs;
    GifParser parser;
    GifArbiter arbiter;
    KickLowering lowering;
    std::vector<size_t> boundary_at;
    std::vector<uint32_t> boundaries;
    TestKickLowering() : parser(ops, regs), arbiter(parser, this), lowering(arbiter, ops) {}
    void OnBoundary(uint32_t seq, uint64_t) override {
        boundaries.push_back(seq);
        boundary_at.push_back(ops.Count());
    }
    void Raw(const Bytes& bytes, GifPath path = GifPath::Path1) {
        arbiter.submit(path, bytes.data(), bytes.size());
    }
    void Lower(const Bytes& bytes) { lowering.Submit(Bytes(bytes)); }
};
using QueuedOwner = TestKickLowering::QueuedOwner;
using StateRig = TestKickLowering::StateRig;

void Same(const RegisterOpStream& actual, const RegisterOpStream& expected, const char* name) {
    bool equal = actual.Count() == expected.Count();
    size_t first = 0;
    if (equal) {
        while (first < actual.Ops().size() &&
               std::memcmp(&actual.Ops()[first], &expected.Ops()[first], sizeof(RegisterOp)) == 0) ++first;
        equal = first == actual.Ops().size();
    }
    if (!equal) {
        std::fprintf(stderr, "parity %s: actual=%zu expected=%zu first=%zu\n",
                     name, actual.Count(), expected.Count(), first);
    }
    CHECK(equal, "complete sink sequence agrees with parser");
    CHECK(actual.Hash() == expected.Hash(), "record bytes/hash agree with parser");
}

// The route a shape takes: the raw parser, or one of the three direct forms
// (the pre-decoded strip, the A+D register block, the generic descriptor list).
enum class Route { Raw, Strip, State, Packed };

void PacketParity(const char* name, const Bytes& bytes, Route route = Route::Raw) {
    TestKickLowering actual, reference;
    actual.Lower(bytes);
    reference.Raw(bytes);
    actual.arbiter.drain();
    reference.arbiter.drain();
    Same(actual.ops, reference.ops, name);
    CHECK(actual.lowering.submitted() == 1, "one submission per complete kick");
    const auto census = actual.lowering.census();
    const bool direct = route != Route::Raw;
    CHECK(census.queued == uint64_t(direct) && census.lowered == uint64_t(direct),
          "supported shape actually executes direct lowering");
    CHECK(census.raw_fallback == uint64_t(!direct) && census.unsupported == uint64_t(!direct),
          "unsupported shape visibly retains raw fallback");
    CHECK(census.queued_state == uint64_t(route == Route::State) && census.lowered_state == uint64_t(route == Route::State),
          "register blocks are counted as state");
    CHECK(census.queued_packed == uint64_t(route == Route::Packed) && census.lowered_packed == uint64_t(route == Route::Packed),
          "generic descriptor lists are counted as packed");
    if (direct) CHECK(actual.parser.BulkQuads() == 0, "direct delivery bypasses the raw PACKED parser");
    if (route == Route::Strip) CHECK(reference.parser.BulkQuads() != 0, "the reference strip went through the bulk lane");
}

void KnownShapes() {
    PacketParity("strip XYZ2", Strip(false), Route::Strip);
    PacketParity("strip XYZF2", Strip(true), Route::Strip);
    PacketParity("text sprites", Sprite(), Route::Packed);
    PacketParity("particle quads", Particles(), Route::Packed);
    PacketParity("decal quads", Decals(), Route::Packed);
    PacketParity("material state plus strip", StateWrites());
    PacketParity("plain register block", PlainBlock(), Route::State);
    PacketParity("textured register block", TexturedBlock(), Route::State);
    PacketParity("textured register block with texture pairs", TexturedBlockWithPairs(), Route::State);
    PacketParity("register block with PRE", AddressData({{0x47, 0x500a4}, {0x42, 0x44}}, 0x15c), Route::State);
    PacketParity("empty register block", AddressData({}), Route::State);
    PacketParity("empty register block with PRE", AddressData({}, 0x204), Route::State);
    PacketParity("every dispatcher class through A+D", DispatcherClasses(), Route::State);
    Bytes ad_no_eop;
    Tag(ad_no_eop, 2, 0, {14}, false);
    Quad(ad_no_eop, 0x500a4, 0, 0x47, 0);
    Quad(ad_no_eop, 0x44, 0, 0x42, 0);
    PacketParity("register block without EOP stays raw", ad_no_eop);
    Bytes ad_two_descriptors;
    Tag(ad_two_descriptors, 1, 0, {14, 14});
    Quad(ad_two_descriptors, 0x500a4, 0, 0x47, 0);
    Quad(ad_two_descriptors, 0x44, 0, 0x42, 0);
    PacketParity("two A+D descriptors are a generic descriptor list", ad_two_descriptors, Route::Packed);
    Bytes mixed;
    // Every PACKED descriptor class in one tag: PRIM, RGBAQ, ST, UV, XYZF2, XYZ2,
    // TEX0_1 (a generic descriptor: the register's value in the low half), FOG,
    // reserved 0x0B (dropped), XYZF3, XYZ3, A+D (TEST_1), NOP, with ADC set on
    // the second loop's XYZ2.
    Tag(mixed, 2, 0, {0, 1, 2, 3, 4, 5, 6, 10, 11, 12, 13, 14, 15}, true);
    for (uint32_t loop = 0; loop < 2; ++loop) {
        Quad(mixed, 0xfffff95c + loop, 0xffffffff, 0, 0);
        Quad(mixed, 0x10 + loop, 0x20, 0x30, 0x80);
        Quad(mixed, Bits(0.5f), Bits(0.25f), Bits(2.0f + loop), 0xdeadbeef);
        Quad(mixed, 0xffffc011 + loop, 0xdead8012, 0x33333333, 0x44444444);
        Quad(mixed, 1600, 3200, (0x123400u + loop) << 4 | 0xf, 0x7f0u << 4 | (loop << 15));
        Quad(mixed, 1760, 3360, 0x0f000002 + loop, loop << 15);
        Quad(mixed, 0x00123456, 0x0000000a, 0xcafe, 0xbabe);
        Quad(mixed, 0, 0, 0x55u << 4, 0);
        Quad(mixed, 0x11111111, 0x11111111, 0x11111111, 0x11111111);
        Quad(mixed, 1700, 3300, 0x00abcdef << 4, 0x7e0u << 4 | 0x8000u);
        Quad(mixed, 1800, 3400, 0x0f000003, 0);
        Quad(mixed, 0x500a4 + loop, 0, 0x47, 0xffffffff);
        Quad(mixed, 1, 2, 3, 4);
    }
    PacketParity("every PACKED descriptor class in one tag", mixed, Route::Packed);
    Bytes ad_reglist;
    Tag(ad_reglist, 1, 1, {14});
    Word(ad_reglist, 0x500a4);
    Word(ad_reglist, 0);
    PacketParity("REGLIST with an A+D descriptor stays raw", ad_reglist);
    PacketParity("odd REGLIST", RegisterList());
    Bytes all;
    Tag(all, 1, 0, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});
    for (uint32_t reg = 0; reg < 16; ++reg) Quad(all, 0x12345678 + reg, 0x89abcdef, 0xfedcba98, 0x76543210);
    PacketParity("NREG zero means sixteen, every descriptor", all, Route::Packed);
    Bytes empty;
    Tag(empty, 0, 0, {15}, true, 0x456);
    PacketParity("zero-loop PRE/EOP", empty, Route::Packed);
}

void ValuePins() {
    TestKickLowering r;
    r.Lower(Strip(true, 3));
    r.arbiter.drain();
    const auto& ops = r.ops.Ops();
    CHECK(ops.size() == 12, "tag, PRE, three vertex triples and EOP");
    if (ops.size() != 12) return;
    CHECK(ops[0].kind == uint8_t(RegisterOpKind::Tag) && ops[0].path == 1, "PATH1 tag precedes writes");
    CHECK(ops[1].kind == uint8_t(RegisterOpKind::Reg) && ops[1].reg == 0 && ops[1].lo == 0x15c,
          "PRE writes the primitive before vertex attributes");
    CHECK(ops[2].kind == uint8_t(RegisterOpKind::St) && uint32_t(ops[2].lo) == 0x80000000u,
          "ST signed zero survives lowering");
    CHECK(ops[3].kind == uint8_t(RegisterOpKind::Rgbaq) && ops[3].lo == 0x80302010u,
          "packed color takes one low byte from each word");
    CHECK(ops[4].kind == uint8_t(RegisterOpKind::Kick) && uint32_t(ops[4].lo) == Bits(100.0f) &&
          uint32_t(ops[4].lo >> 32) == Bits(200.0f), "XY ignores the high half of each packed word");
    CHECK(uint32_t(ops[4].hi) == Bits(float(0x123400)) && ((ops[4].hi >> 32) & 255) == 0x30,
          "XYZF2 uses 24-bit Z and separate fog");
    CHECK(((ops[4].hi >> 40) & 1) == 0 && ((ops[7].hi >> 40) & 1) == 0 &&
          ((ops[10].hi >> 40) & 1) == 1, "ADC suppresses only its own vertex draw");
    CHECK(ops[8].kind == uint8_t(RegisterOpKind::St) && ops[8].hi == 0, "zero Q is delivered for sink policy");
    CHECK(ops[11].kind == uint8_t(RegisterOpKind::Eop), "EOP follows all vertex effects");
    TestKickLowering sprite;
    sprite.Lower(Sprite());
    sprite.arbiter.drain();
    CHECK(sprite.ops.Ops()[2].lo == (uint64_t{0x12} << 16 | 0x11u), "packed UV is unsigned 14-bit");
    TestKickLowering reglist;
    reglist.Lower(RegisterList());
    reglist.arbiter.drain();
    CHECK(reglist.ops.Count() == 8, "odd register list drops the padding half");
    CHECK(reglist.ops.Ops()[2].lo == 0x156, "register-list PRIM masks reserved bits");
    CHECK(reglist.ops.Ops()[3].lo == 0x3f00000080402010, "register-list RGBAQ preserves the raw Q bits");
    CHECK(((reglist.ops.Ops()[4].hi >> 40) & 1) == 1 &&
          ((reglist.ops.Ops()[5].hi >> 40) & 1) == 0 &&
          ((reglist.ops.Ops()[6].hi >> 40) & 1) == 0, "64-bit XYZ2 draws while XYZ3 variants do not");
}

void StateValuePins() {
    TestKickLowering r;
    r.Lower(DispatcherClasses());
    r.arbiter.drain();
    const auto& ops = r.ops.Ops();
    CHECK(r.lowering.census().queued_state == 1 && r.lowering.census().lowered_state == 1 &&
          r.lowering.census().queued == 1 && r.lowering.census().lowered == 1,
          "a register block is counted as state inside the queued/lowered totals");
    // tag, PRIM, RGBAQ, UV, XYZ2, XYZF2, XYZ3, XYZF3, SIGNAL, FINISH, LABEL, EOP.
    CHECK(ops.size() == 12, "dropped addresses leave no record");
    if (ops.size() != 12) return;
    CHECK(ops[0].kind == uint8_t(RegisterOpKind::Tag) && ops[0].path == 1, "PATH1 tag precedes the writes");
    CHECK(ops[1].kind == uint8_t(RegisterOpKind::Reg) && ops[1].reg == 0 && ops[1].lo == 0x15c,
          "A+D PRIM masks to 11 bits");
    CHECK(ops[2].kind == uint8_t(RegisterOpKind::Reg) && ops[2].reg == 1 && ops[2].lo == 0x3f00000080402010,
          "A+D RGBAQ is a 64-bit register write keeping its Q bits");
    CHECK(ops[3].kind == uint8_t(RegisterOpKind::Reg) && ops[3].reg == 3, "A+D UV is a register write");
    CHECK(ops[4].kind == uint8_t(RegisterOpKind::Kick) && uint32_t(ops[4].lo) == Bits(100.0f / 16.0f) &&
          uint32_t(ops[4].lo >> 32) == Bits(200.0f / 16.0f) && ((ops[4].hi >> 40) & 1) == 1,
          "A+D XYZ2 kicks a drawing vertex from the 64-bit form");
    CHECK(ops[5].kind == uint8_t(RegisterOpKind::Kick) && ((ops[5].hi >> 40) & 1) == 1 &&
          ((ops[5].hi >> 32) & 255) == 0x7f, "A+D XYZF2 draws with its fog byte");
    CHECK(ops[6].kind == uint8_t(RegisterOpKind::Kick) && ((ops[6].hi >> 40) & 1) == 0 &&
          ops[7].kind == uint8_t(RegisterOpKind::Kick) && ((ops[7].hi >> 40) & 1) == 0,
          "A+D XYZ3/XYZF3 never draw");
    CHECK(ops[8].reg == 0x60 && ops[9].reg == 0x61 && ops[10].reg == 0x62, "SIGNAL/FINISH/LABEL are forwarded");
    CHECK(ops[11].kind == uint8_t(RegisterOpKind::Eop), "EOP follows the block");
    TestKickLowering plain;
    plain.Lower(PlainBlock());
    plain.arbiter.drain();
    CHECK(plain.ops.Count() == 9, "tag, seven writes, EOP");
    CHECK(plain.ops.Count() == 9 && plain.ops.Ops()[1].reg == 0x47 && plain.ops.Ops()[1].lo == 0x500a4 &&
          plain.ops.Ops()[7].reg == 0x46, "writes arrive in loop order with their full 64-bit values");
    CHECK(plain.parser.BulkQuads() == 0, "the register block bypasses the raw parser");
}

void MalformedAndUnsupported() {
    Bytes truncated = Strip(false);
    truncated.resize(truncated.size() - 1);
    Bytes suffix = Strip(false);
    suffix.insert(suffix.end(), {1, 2, 3, 4, 5, 6, 7});
    Bytes invalid_second = Strip(false);
    Tag(invalid_second, 0x7fff, 0, {2, 1, 5});  // missing second tag's entire payload
    Bytes image;
    Tag(image, 2, 2, {15});
    Quad(image, 0x11111111, 0x22222222, 0x33333333, 0x44444444);
    Quad(image, 0xaaaaaaaa, 0xbbbbbbbb, 0xcccccccc, 0xdddddddd);
    Bytes disabled;
    Tag(disabled, 1, 3, {15});
    Quad(disabled, 11, 22, 33, 44);
    Bytes block_truncated = PlainBlock();
    block_truncated.resize(block_truncated.size() - 16);
    Bytes block_suffix = PlainBlock();
    Append(block_suffix, Strip(false, 2));
    Bytes block_then_block = TexturedBlock();
    Append(block_then_block, PlainBlock());
    for (const auto& bytes : std::vector<Bytes>{Bytes{}, Bytes(8, 0), truncated, suffix, invalid_second, image, disabled,
                                                 block_truncated, block_suffix, block_then_block}) {
        TestKickLowering actual, reference;
        actual.Lower(bytes);
        reference.Raw(bytes);
        // Decline is all-or-nothing: a valid first tag must not emit before
        // the trailing unsupported or incomplete span is given to the parser.
        CHECK(actual.ops.Count() == 0, "fallback does not emit a partial direct packet");
        actual.arbiter.drain();
        reference.arbiter.drain();
        Same(actual.ops, reference.ops, "unsupported/malformed fallback");
        CHECK(actual.parser.BufferedBytes() == reference.parser.BufferedBytes() &&
              actual.parser.Transfer().in_transfer == reference.parser.Transfer().in_transfer,
              "fallback retains buffered bytes and in-flight continuation contract");
        CHECK(actual.lowering.submitted() == 1, "rejected and empty input still contributes one issue");
        CHECK(actual.lowering.census().lowered == 0 && actual.lowering.census().raw_fallback == 1,
              "unsupported/malformed input has one counted raw fallback");
    }
}

void UnsupportedByProgramCensus() {
    // Unsupported submits are attributed to the producing program the runtime
    // names; an origin-free submit lands on the unknown row; supported
    // geometry adds no row. Rows come back largest first.
    static const char kStrip[] = "vs_poscolor_strip";
    static const char kText[] = "vs_sprite_text";
    Bytes image;
    Tag(image, 1, 2, {15});
    Quad(image, 1, 2, 3, 4);
    TestKickLowering rig;
    KickLowering::Origin strip{0x1234567890abcdefull, kStrip, 0x0000};
    KickLowering::Origin text{0xfedcba0987654321ull, kText, 0x1748};
    rig.lowering.Submit(Bytes(image), text);
    rig.lowering.Submit(Bytes(image), strip);
    rig.lowering.Submit(Bytes(image), strip);
    rig.lowering.Submit(Bytes(image));
    rig.lowering.Submit(Strip(false), strip);
    const auto rows = rig.lowering.unsupported_by_program();
    CHECK(rig.lowering.census().unsupported == 4, "four unsupported submits counted");
    CHECK(rows.size() == 3, "three distinct origins (two named, one unknown)");
    CHECK(rows.size() == 3 && rows[0].count == 2 && rows[0].origin.program_hash == strip.program_hash &&
              rows[0].origin.name == kStrip && rows[0].origin.entry_pc == 0,
          "largest row first carries the strip origin");
    CHECK(rows.size() == 3 && std::strcmp(rows[0].shape, "image:f") == 0, "the declined shape names format and regs");
    // A second shape from the same origin is its own row: an A+D state packet
    // without EOP (the complete one is lowered and adds no row) and a two-tag
    // geometry packet.
    Bytes state;
    Tag(state, 2, 0, {14}, false);
    Quad(state, 0, 0, 0x47, 0);
    Quad(state, 0, 0, 0x06, 0);
    Bytes two_tags = Strip(false);
    Append(two_tags, Strip(false));
    rig.lowering.Submit(std::move(state), strip);
    rig.lowering.Submit(std::move(two_tags), strip);
    rig.lowering.Submit(PlainBlock(), strip);
    rig.lowering.Submit(TexturedBlockWithPairs(), strip);
    bool ad_row = false, tail_row = false, complete_ad_row = false;
    for (const auto& row : rig.lowering.unsupported_by_program()) {
        if (row.origin.name == kStrip && std::strcmp(row.shape, "packed:e:noeop") == 0 && row.count == 1) ad_row = true;
        if (row.origin.name == kStrip && std::strcmp(row.shape, "packed:215:tail") == 0 && row.count == 1) tail_row = true;
        if (std::strcmp(row.shape, "packed:e") == 0) complete_ad_row = true;
    }
    CHECK(ad_row, "an A+D state packet without EOP is its own shape row");
    CHECK(tail_row, "a geometry packet with trailing data is its own shape row");
    CHECK(!complete_ad_row && rig.lowering.census().queued_state == 2,
          "the complete A+D register block is lowered: no packed:e row");
    bool text_row = false, unknown_row = false;
    for (const auto& row : rows) {
        if (row.origin.name == kText && row.origin.program_hash == text.program_hash && row.origin.entry_pc == 0x1748 &&
            row.count == 1)
            text_row = true;
        if (row.origin.name == nullptr && row.origin.program_hash == 0 && row.count == 1) unknown_row = true;
    }
    CHECK(text_row, "the text origin has its own row with its entry pc");
    CHECK(unknown_row, "an origin-free submit is attributed to the unknown row");
    rig.arbiter.drain();
}

void PartialAndMaskedState() {
    const Bytes packet = Strip(false, 3);
    for (size_t prefix : {size_t{7}, size_t{16}, size_t{32}, packet.size() - 16}) {
        TestKickLowering actual, reference;
        // A prior raw fragment may leave a tag tail or a partially consumed
        // vertex. A complete-looking new kick is still continuation data.
        actual.parser.ReceiveData(GifPath::Path2, packet.data(), prefix);
        reference.parser.ReceiveData(GifPath::Path2, packet.data(), prefix);
        const Bytes rest(packet.begin() + static_cast<std::ptrdiff_t>(prefix), packet.end());
        actual.Lower(rest);
        reference.Raw(rest);
        actual.arbiter.drain();
        reference.arbiter.drain();
        Same(actual.ops, reference.ops, "raw fragment then HLE continuation");
        CHECK(actual.parser.BufferedBytes() == reference.parser.BufferedBytes() &&
              actual.parser.Transfer().in_transfer == reference.parser.Transfer().in_transfer,
              "active parser transfer is not bypassed");
    }
    TestKickLowering actual, reference;
    actual.regs.Write(GifRegs::kBase + GifRegs::kMode, 4, 1);
    reference.regs.Write(GifRegs::kBase + GifRegs::kMode, 4, 1);
    actual.Lower(packet);
    reference.Raw(packet);
    actual.arbiter.drain();
    reference.arbiter.drain();
    CHECK(actual.ops.Count() == 0 && reference.ops.Count() == 0, "masked state delays every effect");
    CHECK(actual.parser.BufferedBytes() == packet.size(), "masked packet is retained by parser fallback");
    CHECK(actual.lowering.census().queued == 1 && actual.lowering.census().lowered == 0 &&
          actual.lowering.census().ordering_fallback == 1, "supported geometry declines at delivery when parser is masked");
    actual.regs.Write(GifRegs::kBase + GifRegs::kMode, 4, 0);
    reference.regs.Write(GifRegs::kBase + GifRegs::kMode, 4, 0);
    const Bytes tail = Sprite();
    actual.Lower(tail);
    reference.Raw(tail);
    actual.arbiter.drain();
    reference.arbiter.drain();
    Same(actual.ops, reference.ops, "unmask retains both packets in order");
    {
        TestKickLowering busy, raw;
        Bytes prefix;
        // The next complete-looking kick is payload of an earlier NOP tag.
        // It cannot safely be interpreted as a fresh geometry packet.
        Tag(prefix, static_cast<uint16_t>(packet.size() / 16), 0, {15});
        busy.Raw(prefix, GifPath::Path2);
        raw.Raw(prefix, GifPath::Path2);
        busy.Lower(packet);
        raw.Raw(packet);
        busy.arbiter.drain();
        raw.arbiter.drain();
        Same(busy.ops, raw.ops, "candidate arrives behind active transfer");
        CHECK(busy.lowering.census().queued == 1 && busy.lowering.census().lowered == 0 &&
              busy.lowering.census().ordering_fallback == 1 && busy.lowering.census().raw_fallback == 1,
              "active prior transfer forces one observable delivery-time fallback");
    }
}



void InterleavedOrder() {
    QueuedOwner actual_owner, reference_owner;
    TestKickLowering actual, reference;
    actual.arbiter.SetMarkerOwner(&actual_owner);
    reference.arbiter.SetMarkerOwner(&reference_owner);
    const Bytes state = StateWrites();
    const Bytes strip = Strip(true, 3);
    const Bytes sprite = Sprite();
    actual.Raw(state, GifPath::Path2);
    reference.Raw(state, GifPath::Path2);
    actual.Lower(strip);
    reference.Raw(strip);
    actual.arbiter.noteFeedForBoundary();
    reference.arbiter.noteFeedForBoundary();
    actual.arbiter.openBoundary(17, 1234);
    reference.arbiter.openBoundary(17, 1234);
    actual.Lower(sprite);
    reference.Raw(sprite);
    actual.arbiter.drain();
    reference.arbiter.drain();
    Same(actual.ops, reference.ops, "PATH2, HLE, boundary, HLE");
    CHECK(actual.boundaries == reference.boundaries && actual.boundary_at == reference.boundary_at,
          "boundary sees exactly the preceding ordered packet effects");
    CHECK(actual.boundaries == std::vector<uint32_t>{17}, "one boundary was delivered");
    CHECK(actual.boundary_at.size() == 1 && actual.boundary_at[0] + 13 == actual.ops.Count(),
          "the last sprite packet is delivered after the boundary");
    CHECK(actual.lowering.submitted() == 2, "mixed traffic counts only HLE submissions");
    CHECK(actual.lowering.census().lowered == 2 && actual.lowering.census().raw_fallback == 0 &&
          actual.lowering.census().lowered_packed == 1, "queued direct strip and direct sprite both execute once");
    CHECK(!actual.arbiter.hasPending() && actual.arbiter.PendingBytes() == 0, "ordered delivery drains all accepted work");
}

void StateThenGeometryOrder() {
    // The material pass as the title emits it: the register block, then the
    // geometry, both from the same program in consecutive slots; a PATH2 packet
    // and a boundary in between keep their places.
    QueuedOwner actual_owner, reference_owner;
    TestKickLowering actual, reference;
    actual.arbiter.SetMarkerOwner(&actual_owner);
    reference.arbiter.SetMarkerOwner(&reference_owner);
    actual.Lower(TexturedBlockWithPairs());
    reference.Raw(TexturedBlockWithPairs());
    actual.Lower(Strip(true, 4));
    reference.Raw(Strip(true, 4));
    actual.Raw(StateWrites(), GifPath::Path2);
    reference.Raw(StateWrites(), GifPath::Path2);
    actual.Lower(PlainBlock());
    reference.Raw(PlainBlock());
    actual.arbiter.noteFeedForBoundary();
    reference.arbiter.noteFeedForBoundary();
    actual.arbiter.openBoundary(9, 99);
    reference.arbiter.openBoundary(9, 99);
    actual.Lower(Strip(false, 3));
    reference.Raw(Strip(false, 3));
    actual.arbiter.drain();
    reference.arbiter.drain();
    Same(actual.ops, reference.ops, "state, geometry, PATH2, state, boundary, geometry");
    CHECK(actual.boundaries == reference.boundaries && actual.boundary_at == reference.boundary_at,
          "the boundary sees the same preceding effects on both routes");
    const auto census = actual.lowering.census();
    CHECK(census.submitted == 4 && census.queued == 4 && census.lowered == 4 && census.queued_state == 2 &&
          census.lowered_state == 2 && census.raw_fallback == 0 && census.unsupported == 0,
          "two blocks and two strips all lowered, nothing raw");
}

void DeferredMarkerOrder() {
    QueuedOwner owner, raw_owner;
    TestKickLowering actual, reference;
    actual.arbiter.SetMarkerOwner(&owner);
    reference.arbiter.SetMarkerOwner(&raw_owner);
    actual.Raw(StateWrites(), GifPath::Path2);
    reference.Raw(StateWrites(), GifPath::Path2);
    owner.marker = actual.arbiter.openMarker();
    raw_owner.marker = reference.arbiter.openMarker();
    owner.worker = raw_owner.worker = true;
    actual.Lower(Strip(false));
    reference.Raw(Strip(false));
    owner.worker = raw_owner.worker = false;
    actual.Raw(Sprite(), GifPath::Path2);
    reference.Raw(Sprite(), GifPath::Path2);
    actual.arbiter.drain();
    reference.arbiter.drain();
    Same(actual.ops, reference.ops, "unsealed direct marker holds later traffic");
    CHECK(actual.lowering.census().queued == 1 && actual.lowering.census().lowered == 0 &&
          actual.arbiter.hasPending(), "direct candidate waits for its ordered marker");
    actual.arbiter.sealMarker(owner.marker);
    reference.arbiter.sealMarker(raw_owner.marker);
    actual.arbiter.drain();
    reference.arbiter.drain();
    Same(actual.ops, reference.ops, "sealed direct marker precedes later raw packet");
    CHECK(actual.lowering.census().lowered == 1 && !actual.arbiter.hasPending(),
          "sealed marker delivers direct geometry exactly once");
}

void RawCompatibilityAndClear() {
    TestKickLowering actual, reference;
    KickLowering unconfigured(actual.arbiter);
    unconfigured.Submit(Strip(false));
    reference.Raw(Strip(false));
    actual.arbiter.drain();
    reference.arbiter.drain();
    Same(actual.ops, reference.ops, "raw-only compatibility constructor");
    CHECK(unconfigured.census().submitted == 1 && unconfigured.census().unconfigured == 1 &&
          unconfigured.census().raw_fallback == 1 && unconfigured.census().lowered == 0,
          "unconfigured path is explicitly observable");
    actual.ops.Clear();
    actual.Lower(Strip(true));
    CHECK(actual.lowering.census().queued == 1 && actual.ops.Count() == 0, "queued candidate has no submit-time GS effects");
    actual.arbiter.clear();
    actual.arbiter.drain();
    CHECK(actual.ops.Count() == 0 && actual.lowering.census().lowered == 0 &&
          actual.arbiter.PendingBytes() == 0 && !actual.arbiter.hasPending(), "clear drops queued direct work without a late draw");
}



Bytes InheritedState() {
    Bytes bytes;
    Tag(bytes, 8, 0, {14}, true);
    for (const auto& entry : std::array<std::array<uint64_t, 2>, 8>{{
             {0x1a, 1}, {0x00, 0x20c},  // context 1, Gouraud triangle strip
             {0x4d, 0x000000ff000a0020}, // context 1 frame / mask
             {0x19, 0x0000003000000020}, // context 1 XYOFFSET = 2,3 pixels
             {0x41, 0x01bf0000027f0000}, // context 1 scissor = 0..639,0..447
             {0x43, 0x0000008000000044}, // context 1 blending
             {0x01, 0x40a0000080664422}, // inherited color plus stale Q
             {0x03, 0x0000000002340123}}}) { // inherited UV latch
        Word(bytes, entry[1]);
        Word(bytes, entry[0]);
    }
    return bytes;
}

Bytes CarryVertices(bool first) {
    Bytes bytes;
    if (first) {
        Tag(bytes, 2, 0, {5}, true);
        Quad(bytes, 32, 48, 0x123456, 0);
        Quad(bytes, 192, 48, 0x123457, 0);
    } else {
        // A supported direct packet without PRE must inherit PRIM/context and
        // continue the window rather than resetting it at the new packet.
        Tag(bytes, 2, 0, {2, 1, 5}, true);
        for (uint32_t i = 0; i < 2; ++i) {
            Quad(bytes, Bits(0x123 / 16.0f), Bits(0x234 / 16.0f), 0, 0);
            Quad(bytes, 0x22, 0x44, 0x66, 0x80);
            Quad(bytes, i == 0 ? 32 : 192, 208, 0x123456 + i, 0);
        }
    }
    return bytes;
}

void SameDraws(const StateRig& actual, const StateRig& reference) {
    CHECK(actual.draws.draws.size() == reference.draws.draws.size(), "direct and parser paths flush the same draw count");
    if (actual.draws.draws.size() != reference.draws.draws.size()) return;
    for (size_t i = 0; i < actual.draws.draws.size(); ++i) {
        const auto& a = actual.draws.draws[i];
        const auto& b = reference.draws.draws[i];
        const auto ar = a.ToDrawRec();
        const auto br = b.ToDrawRec();
        CHECK(std::memcmp(&ar, &br, sizeof(ar)) == 0, "draw pipeline, bounds, snapshot and identity match parser");
        CHECK(a.raster.dthe == b.raster.dthe && a.raster.dimx == b.raster.dimx &&
              a.tex.lcm == b.tex.lcm && a.tex.mxl == b.tex.mxl &&
              a.tex.l == b.tex.l && a.tex.k == b.tex.k &&
              std::memcmp(a.tex.mip_tbp, b.tex.mip_tbp, sizeof(a.tex.mip_tbp)) == 0 &&
              std::memcmp(a.tex.mip_tbw, b.tex.mip_tbw, sizeof(a.tex.mip_tbw)) == 0,
              "state omitted from disk DrawRec also agrees");
        CHECK(a.verts.size() == b.verts.size(), "assembled vertex count matches");
        if (a.verts.size() == b.verts.size() && !a.verts.empty()) {
            CHECK(std::memcmp(a.verts.data(), b.verts.data(), a.verts.size() * sizeof(a.verts[0])) == 0,
                  "all assembled position, texture, color, fog and draw-kick values match");
        }
    }
}

void InheritedStateCarryAndIdentity() {
    // Exercise both a previously delivered primitive prefix and a prefix still
    // queued ahead of the HLE packet. The direct path must use the same GS
    // state and assembler rather than starting an isolated primitive.
    for (bool queued : {false, true}) {
        StateRig actual, reference;
        actual.Raw(InheritedState(), GifPath::Path2);
        reference.Raw(InheritedState(), GifPath::Path2);
        actual.Raw(CarryVertices(true), GifPath::Path2);
        reference.Raw(CarryVertices(true), GifPath::Path2);
        if (!queued) {
            actual.arbiter.drain();
            reference.arbiter.drain();
            CHECK(actual.draws.draws.empty(), "two inherited strip vertices do not form a triangle");
        }
        actual.Hle(CarryVertices(false), true, 3);
        reference.Hle(CarryVertices(false), false, 3);
        actual.arbiter.drain();
        reference.arbiter.drain();
        Same(actual.ops, reference.ops, "inherited state and primitive carry");
        SameDraws(actual, reference);
        CHECK(actual.lowering.census().lowered == 1 && actual.lowering.census().raw_fallback == 0,
              "inherited-state parity compares actual direct delivery with parser reference");
        CHECK(actual.draws.draws.size() == 1, "continuation makes one strip draw");
        if (actual.draws.draws.size() != 1) continue;
        const auto& draw = actual.draws.draws[0];
        CHECK(draw.verts.size() == 6 && draw.prim_type == 4 && draw.ctx == 1,
              "two strip triangles retain inherited primitive and drawing context");
        CHECK(draw.off_x == 2 && draw.off_y == 3 && draw.raster.frame.fbp == 0x20 * 8192 &&
              draw.raster.frame.fbw == 640 && draw.raster.frame.fbmsk == 0xff,
              "inherited framebuffer, mask and XYOFFSET reach the draw");
        if (draw.verts.size() == 6) {
            const auto& v = draw.verts;
            CHECK(v[0].x == 0 && v[0].y == 0 && v[1].x == 10 && v[1].y == 0 &&
                  v[2].x == 0 && v[2].y == 10, "first triangle uses the preceding packet's vertices");
            CHECK(v[3].x == 0 && v[3].y == 10 && v[4].x == 10 && v[4].y == 0 &&
                  v[5].x == 10 && v[5].y == 10, "strip parity survives the packet/EOP boundary");
            for (const auto& vertex : v) {
                CHECK(vertex.r == 0x22 && vertex.g == 0x44 && vertex.b == 0x66 && vertex.a == 0x80,
                      "color inherited across raw/direct boundary");
                CHECK(vertex.uv_u == 0x123 && vertex.uv_v == 0x234 && vertex.q == 1.0f,
                      "UV latch and tag Q reset survive direct lowering");
            }
        }
        CHECK(draw.identity.hle_handler == 17 && draw.identity.image_hash == 0x12345678abcdef01ull &&
              draw.identity.kick_pc == 0x180 && draw.identity.kick_ord == 3 && draw.identity.frame_inst == 42,
              "the completing HLE packet stamps the emitted draw");
        CHECK(actual.state_sink.IdentityStamped() == 1 && actual.state_sink.IdentityPending() == 0 &&
              actual.state_sink.IdentityUnderflows() == 0, "direct delivery consumes exactly one identity slot");
        // An immediately following raw draw must clear, rather than reuse,
        // the preceding HLE packet's identity.
        Bytes raw = Strip(false, 3);
        actual.Raw(raw, GifPath::Path2);
        reference.Raw(raw, GifPath::Path2);
        actual.arbiter.drain();
        reference.arbiter.drain();
        SameDraws(actual, reference);
        CHECK(actual.draws.draws.size() == 2 && actual.draws.draws.back().identity.hle_handler == 0,
              "later raw traffic clears the HLE identity");
    }
}

void StateBlockReachesTheDraw() {
    // The register block through the direct route then the strip through the
    // direct route: the draw the GS flushes carries the block's TEX0/TEST/
    // ALPHA/ZBUF/FBA/PRMODE/COLCLAMP exactly as when the parser applied them,
    // and both packets stamp their identities in order.
    for (bool queued_block : {false, true}) {
        StateRig actual, reference;
        actual.Raw(InheritedState(), GifPath::Path2);
        reference.Raw(InheritedState(), GifPath::Path2);
        actual.Hle(TexturedBlockWithPairs(), true, 5);
        reference.Hle(TexturedBlockWithPairs(), false, 5);
        if (!queued_block) {
            actual.arbiter.drain();
            reference.arbiter.drain();
            CHECK(actual.draws.draws.empty() && actual.lowering.census().lowered_state == 1,
                  "a register block alone draws nothing and is delivered direct");
        }
        actual.Hle(Strip(false, 3), true, 6);
        reference.Hle(Strip(false, 3), false, 6);
        actual.arbiter.drain();
        reference.arbiter.drain();
        Same(actual.ops, reference.ops, "register block then strip, both direct");
        SameDraws(actual, reference);
        CHECK(actual.draws.draws.size() == 1, "the strip after the block makes one draw");
        if (actual.draws.draws.size() == 1) {
            const auto& draw = actual.draws.draws[0];
            CHECK(draw.identity.hle_handler == 17 && draw.identity.kick_ord == 6,
                  "the geometry packet's identity stamps the draw, not the block's");
            CHECK(draw.verts.size() == 3 && draw.verts[0].r == 0x10, "the strip's vertices reach the draw");
        }
        CHECK(actual.state_sink.IdentityStamped() == reference.state_sink.IdentityStamped() &&
              actual.state_sink.IdentityRaw() == reference.state_sink.IdentityRaw() &&
              actual.state_sink.IdentityPending() == 0 && actual.state_sink.IdentityUnderflows() == 0,
              "the block consumes one identity slot on both routes");
        CHECK(actual.lowering.census().lowered == 2 && actual.lowering.census().lowered_state == 1 &&
              actual.lowering.census().raw_fallback == 0, "both HLE packets were delivered direct");
    }
}

void PacketFreeLibraryOrder() {
    TestKickLowering actual, reference;
    actual.Raw(Strip(false, 3));
    reference.Raw(Strip(false, 3));
    reference.arbiter.drain();
    const size_t preceding = reference.ops.Count();
    actual.arbiter.submitOrdered([&] {
        CHECK(actual.ops.Count() == preceding, "library job follows all preceding packet effects");
        actual.ops.WriteRegister(0x3d, 99);
    });
    actual.arbiter.noteFeedForBoundary();
    actual.arbiter.openBoundary(7, 0);
    reference.ops.WriteRegister(0x3d, 99);
    actual.Raw(Strip(true, 4));
    reference.Raw(Strip(true, 4));
    CHECK(actual.boundaries.empty(), "library and boundary slots stay deferred");
    actual.arbiter.drain();
    reference.arbiter.drain();
    Same(actual.ops, reference.ops, "packet-free library work");
    CHECK(actual.boundaries == std::vector<uint32_t>({7}) &&
          actual.boundary_at == std::vector<size_t>({preceding + 1}),
          "frame boundary follows library work and precedes later packets");
    bool executed = false;
    auto lease = std::make_shared<int>(1);
    std::weak_ptr<int> weak = lease;
    actual.arbiter.submitOrdered([lease, &executed] { executed = true; });
    lease.reset();
    CHECK(actual.arbiter.hasPending() && actual.arbiter.PendingBytes() == 0 && !weak.expired(),
          "packet-free queued work is pending and owns its payload");
    actual.arbiter.clear();
    actual.arbiter.drain();
    CHECK(!executed && weak.expired() && !actual.arbiter.hasPending(),
          "clear releases library work without executing it");
}

}  // namespace

int main() {
    KnownShapes();
    ValuePins();
    StateValuePins();
    MalformedAndUnsupported();
    UnsupportedByProgramCensus();
    PartialAndMaskedState();
    InterleavedOrder();
    StateThenGeometryOrder();
    DeferredMarkerOrder();
    RawCompatibilityAndClear();
    InheritedStateCarryAndIdentity();
    StateBlockReachesTheDraw();
    PacketFreeLibraryOrder();
    std::printf("KickLowering: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
