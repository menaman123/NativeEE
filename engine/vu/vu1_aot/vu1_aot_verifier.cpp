// Vu1AotVerifier -- the native-vs-interpreter corpus compare per image. Rules
// in the header.
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"

#include <cstdio>
#include <cstring>

#include "engine/gif/gif_arbiter.h"
#include "engine/gif/gif_parser.h"
#include "engine/gif/gif_regs.h"
#include "engine/gif/null_gs_sink.h"
#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_image_hash.h"
#include "engine/vu/vu_unit.h"

namespace {

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

uint64_t Fnv1a(uint64_t h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= kFnvPrime;
    }
    return h;
}

// The arbiter's submit tap: the kicked PATH1 bytes, hashed in acceptance order.
struct KickHash {
    uint64_t hash = kFnvOffset;
    size_t packets = 0;
    size_t bytes = 0;
};
void OnKickSubmit(void* user, GifPath path, const uint8_t* data, size_t size) {
    if (path != GifPath::Path1 || data == nullptr) return;
    KickHash* k = static_cast<KickHash*>(user);
    k->hash = Fnv1a(k->hash, data, size);
    ++k->packets;
    k->bytes += size;
}

}  // namespace

struct Vu1AotVerifier::Outcome {
    VUregs regs;
    uint8_t data[Vu1Case::kDataSize];
    uint64_t cycle = 0;
    uint64_t bundles = 0;
    uint64_t kick_hash = 0;
    size_t kick_packets = 0;
    size_t kick_bytes = 0;
    int kicks = 0;
};

uint64_t Vu1AotVerifier::ImageHashOf(const Vu1Case& c) { return VuImageHash::Fast(c.micro_mem, Vu1Case::kMicroSize); }

uint64_t Vu1AotVerifier::CorpusHashOf(const std::vector<const Vu1Case*>& cases) {
    uint64_t h = kFnvOffset;
    for (const Vu1Case* c : cases) h = Fnv1a(h, reinterpret_cast<const uint8_t*>(c), sizeof(Vu1Case));
    return h;
}

// One leg on a fresh unit: the case restored, the program run on the AOT tier
// with every image assumed verified (native) or on the interpreter, the
// arbiter drained into a null sink (the identity is judged on the kicked bytes
// at the submit tap; the parse only keeps the arbiter's contract).
void Vu1AotVerifier::RunLeg(const Vu1Case& c, bool native, Outcome& out) {
    NullGsSink null_sink;
    GifRegs gif_regs;
    GifParser parser(null_sink, gif_regs);
    GifArbiter arbiter(parser);
    KickHash kicks;
    arbiter.SetSubmitObserver(&OnKickSubmit, &kicks);
    VuUnit vu(1, &arbiter);
    vu.LoadCase(c);
    VuSettings& settings = vu.dispatch().settings();
    settings.vu1_mode = native ? VuTierMode::Aot : VuTierMode::Interp;
    settings.hle_enabled = false;
    vu.dispatch().aot().set_assume_verified(native);
    vu.Execute(c.start_pc);
    arbiter.drain();
    std::memcpy(&out.regs, &vu.regs(), sizeof(VUregs));
    std::memcpy(out.data, vu.state().data, Vu1Case::kDataSize);
    out.cycle = vu.cycle();
    out.bundles = vu.dispatch().bundles();
    out.kick_hash = kicks.hash;
    out.kick_packets = kicks.packets;
    out.kick_bytes = kicks.bytes;
    out.kicks = vu.state().xgkick_count;
}

bool Vu1AotVerifier::Same(const Outcome& a, const Outcome& b, std::string& field) {
    if (std::memcmp(&a.regs, &b.regs, sizeof(VUregs)) != 0) {
        char buf[96];
        for (int r = 0; r < 32; ++r) {
            if (std::memcmp(&a.regs.VF[r], &b.regs.VF[r], 16) != 0) {
                std::snprintf(buf, sizeof(buf), "VF%02d native=%08x,%08x,%08x,%08x interp=%08x,%08x,%08x,%08x", r,
                              a.regs.VF[r].I[0], a.regs.VF[r].I[1], a.regs.VF[r].I[2], a.regs.VF[r].I[3], b.regs.VF[r].I[0],
                              b.regs.VF[r].I[1], b.regs.VF[r].I[2], b.regs.VF[r].I[3]);
                field = buf;
                return false;
            }
        }
        for (int r = 0; r < 16; ++r) {
            if (a.regs.VI[r] != b.regs.VI[r]) {
                std::snprintf(buf, sizeof(buf), "VI%02d native=%04x interp=%04x", r, a.regs.VI[r], b.regs.VI[r]);
                field = buf;
                return false;
            }
        }
        if (a.regs.TPC != b.regs.TPC) {
            std::snprintf(buf, sizeof(buf), "TPC native=%04x interp=%04x", a.regs.TPC, b.regs.TPC);
            field = buf;
            return false;
        }
        std::snprintf(buf, sizeof(buf), "status/mac/clip native=%04x/%04x/%06x interp=%04x/%04x/%06x", a.regs.status_flag,
                      a.regs.mac_flag, a.regs.clip_flag, b.regs.status_flag, b.regs.mac_flag, b.regs.clip_flag);
        field = buf;
        return false;
    }
    if (std::memcmp(a.data, b.data, Vu1Case::kDataSize) != 0) {
        size_t n = 0;
        for (size_t i = 0; i < Vu1Case::kDataSize; ++i) n += a.data[i] != b.data[i];
        field = "data_diff_bytes=" + std::to_string(n);
        return false;
    }
    if (a.cycle != b.cycle) {
        field = "cycle native=" + std::to_string(a.cycle) + " interp=" + std::to_string(b.cycle);
        return false;
    }
    if (a.bundles != b.bundles) {
        field = "bundles native=" + std::to_string(a.bundles) + " interp=" + std::to_string(b.bundles);
        return false;
    }
    if (a.kick_hash != b.kick_hash || a.kick_packets != b.kick_packets || a.kick_bytes != b.kick_bytes || a.kicks != b.kicks) {
        field = "kicks native=" + std::to_string(a.kicks) + "/" + std::to_string(a.kick_packets) + "/" + std::to_string(a.kick_bytes) +
                " interp=" + std::to_string(b.kicks) + "/" + std::to_string(b.kick_packets) + "/" + std::to_string(b.kick_bytes);
        return false;
    }
    return true;
}

Vu1AotVerifier::Result Vu1AotVerifier::Verify(const std::vector<const Vu1Case*>& cases) {
    Result r;
    r.cases = cases.size();
    if (cases.empty()) {
        r.divergence = "no corpus case";
        return r;
    }
    r.image_hash = ImageHashOf(*cases[0]);
    for (const Vu1Case* c : cases) {
        if (ImageHashOf(*c) != r.image_hash) {
            r.image_hash = 0;
            r.divergence = "mixed images";
            return r;
        }
    }
    r.corpus_hash = CorpusHashOf(cases);
    // The two outcomes are 2 x ~17 KB: heap-held, reused across cases.
    std::vector<Outcome> legs(2);
    for (size_t i = 0; i < cases.size(); ++i) {
        RunLeg(*cases[i], true, legs[0]);
        RunLeg(*cases[i], false, legs[1]);
        r.bundles += legs[0].bundles;
        std::string field;
        if (!Same(legs[0], legs[1], field)) {
            r.divergence = std::to_string(i) + ": " + field;
            return r;
        }
    }
    r.identical = true;
    return r;
}
