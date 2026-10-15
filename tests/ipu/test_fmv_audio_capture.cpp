// tests/ipu/test_fmv_audio_capture.cpp -- ctest FmvAudioCapture: the ADS header
// guard and the planar -> interleaved de-interleave into an FmvPcmSink.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "counting_fmv_pcm_sink.h"
#include "engine/ipu/hle/fmv_audio_capture.h"

using ps2r::ipu::FmvAudioCapture;

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

static void PutLe32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 24));
}

static void PutTag(std::vector<uint8_t>& v, const char* tag) {
    v.insert(v.end(), tag, tag + 4);
}

// FF A0 00 00 substream prefix.
static std::vector<uint8_t> Prefix() { return {0xFF, 0xA0, 0x00, 0x00}; }

// 'SShd' len(0x18) {format, rate, channels, interleave, 0xFFFFFFFF x2} 'SSbd' body.
static std::vector<uint8_t> AdsHeader(uint32_t format, uint32_t rate, uint32_t channels,
                                      uint32_t interleave, uint32_t body) {
    std::vector<uint8_t> h;
    PutTag(h, "SShd");
    PutLe32(h, 0x18);
    PutLe32(h, format);
    PutLe32(h, rate);
    PutLe32(h, channels);
    PutLe32(h, interleave);
    PutLe32(h, 0xFFFFFFFFu);
    PutLe32(h, 0xFFFFFFFFu);
    PutTag(h, "SSbd");
    PutLe32(h, body);
    return h;
}

// One planar 1024-byte pair: 256 L samples then 256 R samples, 16-bit LE.
static std::vector<uint8_t> PlanarPair(int16_t l_base, int16_t r_base) {
    std::vector<uint8_t> p;
    for (int i = 0; i < 256; ++i) {
        const uint16_t l = static_cast<uint16_t>(l_base + i);
        p.push_back(static_cast<uint8_t>(l));
        p.push_back(static_cast<uint8_t>(l >> 8));
    }
    for (int i = 0; i < 256; ++i) {
        const uint16_t r = static_cast<uint16_t>(r_base - i);
        p.push_back(static_cast<uint8_t>(r));
        p.push_back(static_cast<uint8_t>(r >> 8));
    }
    return p;
}

static std::vector<uint8_t> Cat(std::vector<uint8_t> a, const std::vector<uint8_t>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

static void TestHeaderAccept() {
    CountingFmvPcmSink sink;
    FmvAudioCapture cap(sink);
    const auto pkt = Cat(Prefix(), AdsHeader(1, 48000, 2, 0x200, 4096));
    cap.OnAudioPes(pkt.data(), pkt.size());
    CHECK(cap.HeaderParsed(), "accept: header parsed");
    CHECK(!cap.Disabled(), "accept: not disabled");
    CHECK(cap.BodyLeft() == 4096, "accept: body_left = SSbd size");
    CHECK(cap.StagedBytes() == 0, "accept: nothing staged past the header");
    CHECK(sink.push_calls == 0, "accept: no PCM yet");
}

static void ExpectReject(uint32_t format, uint32_t rate, uint32_t channels, uint32_t interleave,
                         const char* what) {
    CountingFmvPcmSink sink;
    FmvAudioCapture cap(sink);
    const auto pkt = Cat(Prefix(), AdsHeader(format, rate, channels, interleave, 4096));
    cap.OnAudioPes(pkt.data(), pkt.size());
    CHECK(cap.Disabled(), what);
    CHECK(!cap.HeaderParsed(), "reject: header not accepted");
    // Later body packets are ignored once disabled.
    const auto body = Cat(Prefix(), PlanarPair(0, 0));
    cap.OnAudioPes(body.data(), body.size());
    CHECK(sink.push_calls == 0, "reject: disabled capture pushes nothing");
}

static void TestHeaderRejects() {
    ExpectReject(2, 48000, 2, 0x200, "reject: format != 1 (PCM16LE)");
    ExpectReject(1, 44100, 2, 0x200, "reject: rate != 48000");
    ExpectReject(1, 48000, 1, 0x200, "reject: channels != 2");
    ExpectReject(1, 48000, 2, 0x400, "reject: interleave != 0x200");

    {  // no SShd tag
        CountingFmvPcmSink sink;
        FmvAudioCapture cap(sink);
        auto pkt = Cat(Prefix(), AdsHeader(1, 48000, 2, 0x200, 4096));
        pkt[4] = 'X';
        cap.OnAudioPes(pkt.data(), pkt.size());
        CHECK(cap.Disabled(), "reject: first packet lacks SShd");
    }
    {  // implausible SShd length
        CountingFmvPcmSink sink;
        FmvAudioCapture cap(sink);
        auto pkt = Cat(Prefix(), AdsHeader(1, 48000, 2, 0x200, 4096));
        pkt[8] = 0x08;  // len 8 < 16
        cap.OnAudioPes(pkt.data(), pkt.size());
        CHECK(cap.Disabled(), "reject: SShd length < 16");
    }
    {  // SSbd missing
        CountingFmvPcmSink sink;
        FmvAudioCapture cap(sink);
        auto pkt = Cat(Prefix(), AdsHeader(1, 48000, 2, 0x200, 4096));
        pkt[4 + 8 + 0x18] = 'Q';
        cap.OnAudioPes(pkt.data(), pkt.size());
        CHECK(cap.Disabled(), "reject: SSbd chunk missing after SShd");
    }
    {  // header split across two packets is accepted once complete
        CountingFmvPcmSink sink;
        FmvAudioCapture cap(sink);
        const auto hdr = AdsHeader(1, 48000, 2, 0x200, 2048);
        const std::vector<uint8_t> first(hdr.begin(), hdr.begin() + 20);
        const std::vector<uint8_t> second(hdr.begin() + 20, hdr.end());
        const auto p1 = Cat(Prefix(), first);
        cap.OnAudioPes(p1.data(), p1.size());
        CHECK(!cap.HeaderParsed() && !cap.Disabled(), "split header: waits for the rest");
        const auto p2 = Cat(Prefix(), second);
        cap.OnAudioPes(p2.data(), p2.size());
        CHECK(cap.HeaderParsed() && cap.BodyLeft() == 2048, "split header: parsed once complete");
    }
}

static void TestPrefixSkip() {
    CountingFmvPcmSink sink;
    FmvAudioCapture cap(sink);
    // A substream other than FF A0: skipped, never disabling, header still pending.
    const std::vector<uint8_t> other = {0x80, 0x01, 0x00, 0x00, 'S', 'S', 'h', 'd'};
    cap.OnAudioPes(other.data(), other.size());
    CHECK(!cap.Disabled() && !cap.HeaderParsed() && cap.StagedBytes() == 0,
          "p[0] != 0xFF: packet skipped without disabling");
    // Too short to carry a prefix: ignored.
    const std::vector<uint8_t> tiny = {0xFF, 0xA0, 0x00};
    cap.OnAudioPes(tiny.data(), tiny.size());
    CHECK(cap.StagedBytes() == 0, "n < 4: ignored");
    // Prefix only: ignored.
    const auto prefix_only = Prefix();
    cap.OnAudioPes(prefix_only.data(), prefix_only.size());
    CHECK(cap.StagedBytes() == 0, "prefix only: ignored");
    // The real header still goes through afterwards.
    const auto pkt = Cat(Prefix(), AdsHeader(1, 48000, 2, 0x200, 1024));
    cap.OnAudioPes(pkt.data(), pkt.size());
    CHECK(cap.HeaderParsed(), "header accepted after a skipped foreign substream");
}

static void TestPlanarToInterleaved() {
    CountingFmvPcmSink sink;
    FmvAudioCapture cap(sink);
    // Header + first pair in one packet, body = 3 pairs.
    const auto pkt1 = Cat(Prefix(), Cat(AdsHeader(1, 48000, 2, 0x200, 3 * 1024), PlanarPair(1000, -1000)));
    cap.OnAudioPes(pkt1.data(), pkt1.size());
    CHECK(sink.push_calls == 1 && sink.frames == 256, "pair 1: one Push of 256 frames");
    CHECK(cap.BodyLeft() == 2 * 1024, "pair 1: body_left counts the staged pair");
    bool ok = true;
    for (int i = 0; i < 256; ++i) {
        if (sink.samples[static_cast<size_t>(i) * 2] != static_cast<int16_t>(1000 + i)) ok = false;
        if (sink.samples[static_cast<size_t>(i) * 2 + 1] != static_cast<int16_t>(-1000 - i)) ok = false;
    }
    CHECK(ok, "pair 1: fr[2i] = L[i], fr[2i+1] = R[i]");

    // A pair split across two non-aligned packets (600 + 424 bytes): the tail
    // waits, then one Push.
    const auto pair2 = PlanarPair(5, 7);
    const std::vector<uint8_t> a(pair2.begin(), pair2.begin() + 600);
    const std::vector<uint8_t> b(pair2.begin() + 600, pair2.end());
    const auto pa = Cat(Prefix(), a);
    cap.OnAudioPes(pa.data(), pa.size());
    CHECK(sink.push_calls == 1 && cap.StagedBytes() == 600, "pair 2 head: staged, no push");
    const auto pb = Cat(Prefix(), b);
    cap.OnAudioPes(pb.data(), pb.size());
    CHECK(sink.push_calls == 2 && sink.frames == 512 && cap.StagedBytes() == 0, "pair 2 tail: completes, pushed");
    CHECK(sink.samples[512] == 5 && sink.samples[513] == 7 && sink.samples[512 + 2 * 255] == 5 + 255 &&
          sink.samples[513 + 2 * 255] == 7 - 255,
          "pair 2: interleave order across the split");

    // Two pairs plus 100 bytes of padding beyond the body in one packet: the
    // body bound truncates, two Pushes, then trailing padding is ignored.
    auto pkt3 = Cat(Prefix(), Cat(PlanarPair(0, 0), PlanarPair(1, 1)));
    for (int i = 0; i < 100; ++i) pkt3.push_back(0xAB);
    cap.OnAudioPes(pkt3.data(), pkt3.size());
    CHECK(sink.push_calls == 3 && sink.frames == 768, "body bound: only the promised bytes count (1 more pair)");
    CHECK(cap.BodyLeft() == 0, "body complete");
    CHECK(cap.StagedBytes() == 0, "body bound: excess bytes beyond SSbd size dropped");
    const auto pkt4 = Cat(Prefix(), PlanarPair(9, 9));
    cap.OnAudioPes(pkt4.data(), pkt4.size());
    CHECK(sink.push_calls == 3, "trailing padding after the body is ignored");

    cap.Reset();
    CHECK(!cap.HeaderParsed() && !cap.Disabled() && cap.BodyLeft() == 0 && cap.StagedBytes() == 0,
          "Reset: back to the pre-header state");
    CHECK(sink.gate_opens == 0 && sink.flushes == 0, "capture never touches the gate or flushes (the API's calls)");
}

int main() {
    TestHeaderAccept();
    TestHeaderRejects();
    TestPrefixSkip();
    TestPlanarToInterleaved();

    std::printf("test_fmv_audio_capture: %d/%d checks passed\n", g_checks - g_failed, g_checks);
    if (g_failed == 0) {
        std::printf("test_fmv_audio_capture: PASS (header accept, rejects, prefix skip, planar->interleaved)\n");
        return 0;
    }
    std::printf("test_fmv_audio_capture: %d FAILED\n", g_failed);
    return 1;
}
