// tests/ipu/test_pss_demux.cpp -- ctest PssDemux. Half 1: a synthetic program
// stream built from the 13818-1 rules (MPEG-2 pack with stuffing, system header,
// video PES with a known length, video PES with PES_packet_length 0, private
// stream 1 audio PES, program end, a start code straddling two Process calls).
// Half 2 (argv[1] = the frozen OLD tree, SKIP when absent): the two intro movies
// walked sector by sector against the counts pss_census.py reports for them
// (fixtures/pss_census_counts.txt).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "engine/ipu/hle/pss_demux.h"
#include "recording_pss_listener.h"

using ps2r::ipu::PssDemux;

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failed;                                                      \
            std::printf("FAIL: %s (line %d): %s\n", msg, __LINE__, #cond);   \
        }                                                                    \
    } while (0)

using Bytes = std::vector<uint8_t>;

static void Put(Bytes& v, std::initializer_list<int> bytes) {
    for (int b : bytes) v.push_back(static_cast<uint8_t>(b));
}

static void PutBe16(Bytes& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x));
}

// MPEG-2 pack header: 00 00 01 BA, '01' marker byte, SCR (6 bytes total incl.
// marker), mux_rate (3 bytes), reserved(5) + pack_stuffing_length(3) at +13,
// then that many 0xFF stuffing bytes.
static Bytes Mpeg2Pack(int stuffing) {
    Bytes v;
    Put(v, {0x00, 0x00, 0x01, 0xBA});
    Put(v, {0x44, 0x00, 0x04, 0x00, 0x04, 0x01});  // SCR with the '01' marker
    Put(v, {0x01, 0x89, 0xC3});                    // mux_rate
    v.push_back(static_cast<uint8_t>(0xF8 | (stuffing & 7)));
    for (int i = 0; i < stuffing; ++i) v.push_back(0xFF);
    return v;
}

// MPEG-1 pack header: 12 bytes, '0010' marker nibble at +4.
static Bytes Mpeg1Pack() {
    Bytes v;
    Put(v, {0x00, 0x00, 0x01, 0xBA});
    Put(v, {0x21, 0x00, 0x01, 0x00, 0x01, 0x80, 0x01, 0x89});
    return v;
}

static Bytes SystemHeader(uint16_t len) {
    Bytes v;
    Put(v, {0x00, 0x00, 0x01, 0xBB});
    PutBe16(v, len);
    for (uint16_t i = 0; i < len; ++i) v.push_back(static_cast<uint8_t>(0xA0 + i));
    return v;
}

// MPEG-2 PES: 00 00 01 <id> <len16> 0x80 <flags> <header_data_length> <header data> <payload>.
static Bytes Mpeg2Pes(uint8_t id, uint8_t header_data_len, const Bytes& payload, bool zero_length) {
    Bytes v;
    Put(v, {0x00, 0x00, 0x01, id});
    const size_t body = 3 + header_data_len + payload.size();
    PutBe16(v, zero_length ? 0 : static_cast<uint16_t>(body));
    Put(v, {0x80, 0x80});
    v.push_back(header_data_len);
    for (uint8_t i = 0; i < header_data_len; ++i) v.push_back(0x11);  // PTS bytes stand-in
    v.insert(v.end(), payload.begin(), payload.end());
    return v;
}

// MPEG-1 PES: stuffing 0xFF x2, STD buffer '01' 2 bytes, PTS ('0010' nibble) 5 bytes.
static Bytes Mpeg1Pes(uint8_t id, const Bytes& payload) {
    Bytes v;
    Put(v, {0x00, 0x00, 0x01, id});
    PutBe16(v, static_cast<uint16_t>(2 + 2 + 5 + payload.size()));
    Put(v, {0xFF, 0xFF, 0x40, 0x20, 0x21, 0x00, 0x01, 0x00, 0x01});
    v.insert(v.end(), payload.begin(), payload.end());
    return v;
}

static Bytes Cat(const std::vector<Bytes>& parts) {
    Bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

static Bytes Filled(size_t n, uint8_t seed) {
    Bytes v(n);
    for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(seed + i * 3);
    // keep 00 00 01 out of the payload
    for (size_t i = 0; i + 2 < n; ++i) if (v[i] == 0 && v[i + 1] == 0 && v[i + 2] == 1) v[i + 2] = 2;
    return v;
}

static void TestClassifiers() {
    CHECK(PssDemux::IsVideoStreamId(0xE0) && PssDemux::IsVideoStreamId(0xEF) && !PssDemux::IsVideoStreamId(0xDF) &&
          !PssDemux::IsVideoStreamId(0xF0), "video ids are E0..EF");
    CHECK(PssDemux::IsAudioStreamId(0xBD) && PssDemux::IsAudioStreamId(0xC0) && PssDemux::IsAudioStreamId(0xDF) &&
          !PssDemux::IsAudioStreamId(0xBE) && !PssDemux::IsAudioStreamId(0xE0), "audio ids are BD, C0..DF");
    const uint8_t prefixed[] = {0xBB, 0xBC, 0xBE, 0xBF, 0xF0, 0xF1, 0xF2, 0xF8, 0xFF};
    bool all = true;
    for (uint8_t id : prefixed) if (!PssDemux::IsLengthPrefixedHeader(id)) all = false;
    CHECK(all, "length-prefixed header set {BB,BC,BE,BF,F0,F1,F2,F8,FF}");
    CHECK(!PssDemux::IsLengthPrefixedHeader(0xBD) && !PssDemux::IsLengthPrefixedHeader(0xBA) &&
          !PssDemux::IsLengthPrefixedHeader(0xE0) && !PssDemux::IsLengthPrefixedHeader(0xF3),
          "BD/BA/E0/F3 are not length-prefixed headers");

    const Bytes buf = {0x12, 0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x00, 0x01, 0xB7};
    CHECK(PssDemux::FindStartCode(buf, 0) == 1, "FindStartCode from 0");
    CHECK(PssDemux::FindStartCode(buf, 2) == 6, "FindStartCode from 2 finds the second");
    CHECK(PssDemux::FindStartCode(buf, 7) == PssDemux::kStartCodeNotFound, "FindStartCode: none past 7");
    CHECK(PssDemux::FindStartCode({0x00, 0x00, 0x01}, 0) == PssDemux::kStartCodeNotFound, "3-byte buffer: not found");
    CHECK(PssDemux::ContainsSequenceEnd(buf.data() + 5, 5), "ContainsSequenceEnd sees 00 00 01 B7");
    CHECK(!PssDemux::ContainsSequenceEnd(buf.data(), 9), "ContainsSequenceEnd: B7 cut off");
    CHECK(!PssDemux::ContainsSequenceEnd(nullptr, 8), "ContainsSequenceEnd: null");

    // MPEG-2 header: 9 + header_data_length.
    const Bytes m2 = Mpeg2Pes(0xE0, 5, Filled(10, 7), false);
    CHECK(PssDemux::ParsePesPayloadOffset(m2.data(), m2.size()) == 14, "MPEG-2 PES payload at 9 + 5");
    // MPEG-1: 6 + 2 stuffing + 2 STD + 5 PTS = 15.
    const Bytes m1 = Mpeg1Pes(0xE0, Filled(10, 9));
    CHECK(PssDemux::ParsePesPayloadOffset(m1.data(), m1.size()) == 15, "MPEG-1 PES payload after stuffing/STD/PTS");
    // MPEG-1 with PTS+DTS ('0011' nibble) -> +10; with 0x0F -> +1.
    Bytes m1b = {0x00, 0x00, 0x01, 0xE0, 0x00, 0x20, 0x31, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xAA};
    CHECK(PssDemux::ParsePesPayloadOffset(m1b.data(), m1b.size()) == 16, "MPEG-1 PTS+DTS -> 6 + 10");
    Bytes m1c = {0x00, 0x00, 0x01, 0xE0, 0x00, 0x02, 0x0F, 0xAA};
    CHECK(PssDemux::ParsePesPayloadOffset(m1c.data(), m1c.size()) == 7, "MPEG-1 no-timestamp byte -> 6 + 1");
    CHECK(PssDemux::ParsePesPayloadOffset(m1c.data(), 6) == 6, "packet_size <= 6 -> packet_size");
    Bytes m2clamp = {0x00, 0x00, 0x01, 0xE0, 0x00, 0x04, 0x80, 0x00, 0xF0, 0x11};
    CHECK(PssDemux::ParsePesPayloadOffset(m2clamp.data(), m2clamp.size()) == m2clamp.size(),
          "MPEG-2 header longer than the packet clamps to packet_size");
}

static void TestSyntheticStream() {
    const Bytes v1 = Filled(300, 1);
    const Bytes v2 = Filled(70, 2);
    Bytes v3 = Filled(40, 3);
    Put(v3, {0x00, 0x00, 0x01, 0xB7});  // sequence end inside the last video payload
    const Bytes a1 = Filled(50, 4);

    RecordingPssListener rec;
    PssDemux demux(rec);

    // Pack(MPEG-2, 3 stuffing) / system header / video PES (len) / MPEG-1 pack /
    // audio PES 0xBD (MPEG-1 header) / video PES (len 0, runs to the next start
    // code) / padding stream 0xBE / video PES with B7 / program end.
    const Bytes stream = Cat({Mpeg2Pack(3), SystemHeader(18), Mpeg2Pes(0xE0, 5, v1, false), Mpeg1Pack(),
                              Mpeg1Pes(0xBD, a1), Mpeg2Pes(0xE0, 0, v2, true), SystemHeader(4),
                              Mpeg2Pes(0xE3, 5, v3, false), Bytes{0x00, 0x00, 0x01, 0xB9},
                              Mpeg1Pes(0xBD, a1)});  // trailing audio after B9: never delivered

    demux.Append(stream.data(), stream.size());
    demux.Process(false);

    CHECK(rec.events.size() == 4, "events: 3 video + 1 audio (program end unreachable behind B7)");
    if (rec.events.size() >= 4) {
        CHECK(rec.events[0].kind == PssEvent::kVideo && rec.events[0].bytes == v1 && !rec.events[0].sequence_end,
              "event 0: video PES payload v1 exactly");
        CHECK(rec.events[1].kind == PssEvent::kAudio && rec.events[1].bytes == a1, "event 1: audio PES payload a1");
        CHECK(rec.events[2].kind == PssEvent::kVideo && rec.events[2].bytes == v2 && !rec.events[2].sequence_end,
              "event 2: zero-length video PES runs to the next start code");
        CHECK(rec.events[3].kind == PssEvent::kVideo && rec.events[3].bytes == v3 && rec.events[3].sequence_end,
              "event 3: video PES flagged contains_sequence_end");
    }
    CHECK(demux.Ended(), "sequence end code ends the stream");
    CHECK(demux.Buffered() == 0, "everything after the ending packet is discarded");
    CHECK(rec.program_end_count == 0, "B9 behind the sequence end is never reported");
    CHECK(demux.PacksSeen() == 2 && demux.SystemHeadersSeen() == 2, "2 packs (MPEG-2 + MPEG-1), 2 length-prefixed headers");
    const size_t walked = Mpeg2Pack(3).size() + SystemHeader(18).size() + Mpeg2Pes(0xE0, 5, v1, false).size() +
                          Mpeg1Pack().size() + Mpeg1Pes(0xBD, a1).size() + Mpeg2Pes(0xE0, 0, v2, true).size() +
                          SystemHeader(4).size() + Mpeg2Pes(0xE3, 5, v3, false).size();
    CHECK(demux.Consumed() == walked, "Consumed() = offset just past the ending packet");
    CHECK(Mpeg2Pack(3).size() == 17 && Mpeg1Pack().size() == 12, "pack sizes 14 + 3 and 12");

    // After the end every later byte is dropped.
    demux.Append(stream.data(), stream.size());
    demux.Process(false);
    CHECK(rec.events.size() == 4 && demux.Buffered() == 0, "ended: later input is discarded");

    // Reset re-opens the walk.
    demux.Reset();
    CHECK(!demux.Ended() && demux.Consumed() == 0 && demux.PacksSeen() == 0, "Reset clears ended/consumed/counters");
}

static void TestProgramEnd() {
    RecordingPssListener rec;
    PssDemux demux(rec);
    const Bytes v1 = Filled(30, 5);
    const Bytes stream = Cat({Mpeg2Pack(0), Mpeg2Pes(0xE0, 0, v1, false), Bytes{0x00, 0x00, 0x01, 0xB9},
                              Mpeg2Pes(0xE0, 0, v1, false)});
    demux.Append(stream.data(), stream.size());
    demux.Process(false);
    CHECK(rec.video_count == 1 && rec.program_end_count == 1, "program end reported once after the video PES");
    CHECK(demux.Ended() && demux.Buffered() == 0, "program end ends the stream and drops the rest");
    CHECK(demux.Consumed() == Mpeg2Pack(0).size() + Mpeg2Pes(0xE0, 0, v1, false).size() + 4,
          "Consumed() includes the 4-byte end code");
    CHECK(rec.events.back().kind == PssEvent::kProgramEnd, "last event is the program end");

    // MarkEnded from outside behaves the same.
    RecordingPssListener rec2;
    PssDemux d2(rec2);
    d2.MarkEnded();
    d2.Append(stream.data(), stream.size());
    d2.Process(false);
    CHECK(rec2.events.empty() && d2.Buffered() == 0, "MarkEnded: all input discarded");
}

static void TestChunkingAndTail() {
    const Bytes v1 = Filled(200, 6);
    const Bytes a1 = Filled(64, 8);
    const Bytes stream = Cat({Mpeg2Pack(2), Mpeg2Pes(0xE0, 5, v1, false), Mpeg2Pes(0xBD, 0, a1, false),
                              Mpeg2Pes(0xE1, 0, v1, true)});

    // Byte-at-a-time feeding must reproduce the same events as one shot.
    RecordingPssListener whole;
    {
        PssDemux d(whole);
        d.Append(stream.data(), stream.size());
        d.Process(true);  // final: the zero-length PES takes the rest
    }
    RecordingPssListener trickle;
    {
        PssDemux d(trickle);
        for (size_t i = 0; i < stream.size(); ++i) {
            d.Append(&stream[i], 1);
            d.Process(false);
        }
        CHECK(trickle.video_count == 1 && trickle.audio_count == 1, "trickle: zero-length PES waits for a start code");
        d.Process(true);
    }
    CHECK(whole.events.size() == 3 && trickle.events.size() == 3, "whole vs trickle: 3 events each");
    bool same = whole.events.size() == trickle.events.size();
    for (size_t i = 0; same && i < whole.events.size(); ++i) {
        same = whole.events[i].kind == trickle.events[i].kind && whole.events[i].bytes == trickle.events[i].bytes;
    }
    CHECK(same, "whole vs trickle: identical event sequence and payload bytes");

    // Start-code prefix straddling two chunks: no start code visible -> keep 3 bytes.
    RecordingPssListener rec;
    PssDemux d(rec);
    const Bytes junk = {0x55, 0x66, 0x77, 0x00, 0x00};  // ends with 2 bytes of a start code
    d.Append(junk.data(), junk.size());
    d.Process(false);
    CHECK(d.Buffered() == 3, "no start code: the last 3 bytes are kept");
    CHECK(d.Consumed() == 2, "no start code: the dropped junk counts as walked");
    const Bytes rest = Cat({Bytes{0x01, 0xE0}, Bytes{0x00, 0x00, 0x80, 0x80, 0x00}, Filled(16, 9),
                            Bytes{0x00, 0x00, 0x01, 0xB9}});
    d.Append(rest.data(), rest.size());
    d.Process(false);
    CHECK(rec.video_count == 1 && rec.events[0].bytes == Filled(16, 9),
          "straddling start code reassembles into the video PES");
    CHECK(rec.program_end_count == 1, "then the program end");

    // Final chunk with no start code drops everything.
    RecordingPssListener rec3;
    PssDemux d3(rec3);
    d3.Append(junk.data(), junk.size());
    d3.Process(true);
    CHECK(d3.Buffered() == 0, "final chunk without a start code drops all");

    // Incomplete length-known PES waits (not final) or is taken whole (final).
    RecordingPssListener rec4;
    PssDemux d4(rec4);
    const Bytes pes = Mpeg2Pes(0xE0, 0, v1, false);
    d4.Append(pes.data(), pes.size() - 10);
    d4.Process(false);
    CHECK(rec4.video_count == 0 && d4.Buffered() == pes.size() - 10, "short PES waits for its tail");
    d4.Process(true);
    CHECK(rec4.video_count == 1 && rec4.events[0].bytes.size() == v1.size() - 10 && d4.Buffered() == 0,
          "final: the truncated PES is delivered as far as it goes");

    // Incomplete pack header waits / is dropped on final.
    RecordingPssListener rec5;
    PssDemux d5(rec5);
    const Bytes pack = Mpeg2Pack(4);
    d5.Append(pack.data(), 13);
    d5.Process(false);
    CHECK(d5.Buffered() == 13 && d5.PacksSeen() == 0, "13 of a 14+ byte MPEG-2 pack waits");
    d5.Append(pack.data() + 13, pack.size() - 13);
    d5.Process(false);
    CHECK(d5.Buffered() == 0 && d5.PacksSeen() == 1, "completed pack consumed (14 + 4 stuffing)");
}

// ---------------------------------------------------------------------------
// OLD-gated census half.
// ---------------------------------------------------------------------------

struct CensusCounts {
    uint64_t video_pes = 0, audio_pes = 0, packs = 0, sys = 0, program_end = 0, sequence_end = 0, total_pictures = 0;
};

static bool LoadCensusFixture(const std::string& path, std::map<std::string, CensusCounts>& out) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string name, kv;
        ss >> name;
        CensusCounts c;
        while (ss >> kv) {
            const size_t eq = kv.find('=');
            if (eq == std::string::npos) continue;
            const std::string k = kv.substr(0, eq);
            const uint64_t v = std::stoull(kv.substr(eq + 1));
            if (k == "video_pes") c.video_pes = v;
            else if (k == "audio_pes") c.audio_pes = v;
            else if (k == "packs") c.packs = v;
            else if (k == "sys") c.sys = v;
            else if (k == "program_end") c.program_end = v;
            else if (k == "sequence_end") c.sequence_end = v;
            else if (k == "total_pictures") c.total_pictures = v;
        }
        out[name] = c;
    }
    return !out.empty();
}

static uint64_t CountPictureStarts(const Bytes& es) {
    uint64_t n = 0;
    for (size_t j = 0; j + 3 < es.size();) {
        if (es[j] == 0 && es[j + 1] == 0 && es[j + 2] == 1) {
            if (es[j + 3] == 0x00) ++n;
            j += 4;
        } else {
            ++j;
        }
    }
    return n;
}

// Walks one movie in 2048-byte sectors (the sector pump's grain). The demuxer
// stops at the sequence end code (the movie is over for the IPU); the
// container's tail after that packet -- padding packs, the remaining audio PES,
// the program end code -- is walked in a second pass from Consumed() so the
// per-file totals compare 1:1 with the census script.
static void CensusOneMovie(const std::string& path, const std::string& name, const CensusCounts& want) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        CHECK(false, ("census: cannot open " + path).c_str());
        return;
    }
    Bytes file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    RecordingPssListener rec;
    PssDemux demux(rec);
    size_t off = 0;
    while (off < file.size() && !demux.Ended()) {
        const size_t n = std::min<size_t>(2048, file.size() - off);
        demux.Append(&file[off], n);
        demux.Process(off + n >= file.size());
        off += n;
    }
    const uint64_t consumed = demux.Consumed();
    CensusCounts got;
    got.video_pes = rec.video_count;
    got.audio_pes = rec.audio_count;
    got.packs = demux.PacksSeen();
    got.sys = demux.SystemHeadersSeen();
    got.program_end = rec.program_end_count;
    got.sequence_end = rec.sequence_end_count;
    got.total_pictures = CountPictureStarts(rec.es);
    std::printf("census %s: pass 1 stopped at 0x%llx (sequence_end=%llu): video_pes=%llu audio_pes=%llu packs=%llu "
                "sys=%llu program_end=%llu pictures=%llu\n",
                name.c_str(), (unsigned long long)consumed, (unsigned long long)got.sequence_end,
                (unsigned long long)got.video_pes, (unsigned long long)got.audio_pes, (unsigned long long)got.packs,
                (unsigned long long)got.sys, (unsigned long long)got.program_end, (unsigned long long)got.total_pictures);
    CHECK(demux.Ended() && got.sequence_end == 1, (name + ": the video ends with one sequence_end_code").c_str());

    // Pass 2: the container tail after the ending packet.
    RecordingPssListener tail;
    PssDemux d2(tail);
    if (consumed < file.size()) {
        d2.Append(&file[consumed], file.size() - consumed);
        d2.Process(true);
    }
    got.video_pes += tail.video_count;
    got.audio_pes += tail.audio_count;
    got.packs += d2.PacksSeen();
    got.sys += d2.SystemHeadersSeen();
    got.program_end += tail.program_end_count;
    CHECK(tail.video_count == 0, (name + ": no video PES after the sequence end").c_str());
    std::printf("census %s: tail from 0x%llx: audio_pes=%u packs=%llu sys=%llu program_end=%u\n", name.c_str(),
                (unsigned long long)consumed, tail.audio_count, (unsigned long long)d2.PacksSeen(),
                (unsigned long long)d2.SystemHeadersSeen(), tail.program_end_count);

    CHECK(got.video_pes == want.video_pes, (name + ": video_pes").c_str());
    CHECK(got.audio_pes == want.audio_pes, (name + ": audio_pes").c_str());
    CHECK(got.packs == want.packs, (name + ": packs").c_str());
    CHECK(got.sys == want.sys, (name + ": sys").c_str());
    CHECK(got.program_end == want.program_end, (name + ": program_end").c_str());
    CHECK(got.sequence_end == want.sequence_end, (name + ": sequence_end").c_str());
    CHECK(got.total_pictures == want.total_pictures, (name + ": total_pictures").c_str());
    std::printf("census %s: totals video_pes=%llu audio_pes=%llu packs=%llu sys=%llu program_end=%llu "
                "sequence_end=%llu total_pictures=%llu -> %s\n",
                name.c_str(), (unsigned long long)got.video_pes, (unsigned long long)got.audio_pes,
                (unsigned long long)got.packs, (unsigned long long)got.sys, (unsigned long long)got.program_end,
                (unsigned long long)got.sequence_end, (unsigned long long)got.total_pictures,
                (got.video_pes == want.video_pes && got.audio_pes == want.audio_pes && got.packs == want.packs &&
                 got.sys == want.sys && got.program_end == want.program_end && got.sequence_end == want.sequence_end &&
                 got.total_pictures == want.total_pictures) ? "7/7 MATCH" : "MISMATCH");
}

static void TestOldCensus(const char* og_root, const char* fixture) {
    if (!og_root || !*og_root || !fixture || !*fixture) {
        std::printf("SKIP: census half (no OLD root / fixture path on the command line)\n");
        return;
    }
    std::map<std::string, CensusCounts> want;
    if (!LoadCensusFixture(fixture, want)) {
        std::printf("SKIP: census half (fixture %s unreadable)\n", fixture);
        return;
    }
    unsigned ran = 0;
    for (const auto& kv : want) {
        const std::string path = std::string(og_root) + "/" + kv.first;
        std::ifstream probe(path, std::ios::binary);
        if (!probe) {
            std::printf("SKIP: census half (%s absent)\n", path.c_str());
            continue;
        }
        probe.close();
        CensusOneMovie(path, kv.first, kv.second);
        ++ran;
    }
    if (ran == 0) std::printf("SKIP: census half (no movie present under %s)\n", og_root);
}

int main(int argc, char** argv) {
    TestClassifiers();
    TestSyntheticStream();
    TestProgramEnd();
    TestChunkingAndTail();
    TestOldCensus(argc > 1 ? argv[1] : nullptr, argc > 2 ? argv[2] : nullptr);

    std::printf("test_pss_demux: %d/%d checks passed\n", g_checks - g_failed, g_checks);
    if (g_failed == 0) {
        std::printf("test_pss_demux: PASS (classifiers, synthetic stream, program end, chunking/tail, census)\n");
        return 0;
    }
    std::printf("test_pss_demux: %d FAILED\n", g_failed);
    return 1;
}
