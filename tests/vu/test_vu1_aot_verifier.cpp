// Vu1AotVerifier: the AOT tier's verification gate on fabricated corpus cases
// (a four-bundle program: ADD -> SQ -> E-bit NOP -> delay slot, under two register
// files): two cases of one image -> identical, cases=2, the image hash of the
// micro memory, an order-dependent corpus hash, the native legs' bundle count;
// the empty set and a mixed-image set are refused with their reason. Then the
// tier's status machine on the same case: an Unverified image runs on the
// fallback (the block tier) and is reported to the kick observer, a Verified one
// runs native on the specialised table (the program's resolver swapped), a
// Refused one goes back to the dynamic bodies; the interpreter fallback is
// selectable; the registers after every run equal the interpreter's.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/vu/tiers/vu_tier_dispatch.h"
#include "engine/vu/vu1_aot/vu1_aot_verifier.h"
#include "engine/vu/vu1_case.h"
#include "engine/vu/vu_image_hash.h"
#include "engine/vu/vu_unit.h"

namespace {

int g_fails = 0;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);   \
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
            ++g_fails;                                                      \
        }                                                                   \
    } while (0)

void Put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

// ADD.xyzw VF01, VF02, VF03 | NOP ; NOP | SQ.xyzw VF01, (VI01) ; NOP[E] | NOP ; NOP | NOP
void Fabricate(Vu1Case& c, uint32_t seed) {
    std::memset(&c, 0, sizeof(c));
    c.magic = Vu1Case::kMagic;
    c.version = Vu1Case::kVersion;
    c.start_pc = 0;
    const uint32_t upper_nop = 0x2FFu;                    // func 0x3F, op 0x0B
    const uint32_t lower_nop = 0x8000033Cu;               // MOVE ft=0
    const uint32_t add = 0x28u | (1u << 6) | (2u << 11) | (3u << 16) | (0xFu << 21);
    const uint32_t sq = (0x01u << 25) | (0xFu << 21) | (1u << 16) | (1u << 11);
    Put32(c.micro_mem + 0, lower_nop); Put32(c.micro_mem + 4, add);
    Put32(c.micro_mem + 8, sq);        Put32(c.micro_mem + 12, upper_nop);
    Put32(c.micro_mem + 16, lower_nop); Put32(c.micro_mem + 20, upper_nop | (1u << 30));
    Put32(c.micro_mem + 24, lower_nop); Put32(c.micro_mem + 28, upper_nop);
    c.regs.VF[0].w = 1.0f;
    c.regs.VF[2].x = 1.5f + seed; c.regs.VF[2].y = -2.0f; c.regs.VF[2].z = 0.25f; c.regs.VF[2].w = 8.0f;
    c.regs.VF[3].x = 0.5f; c.regs.VF[3].y = 2.0f * seed; c.regs.VF[3].z = -0.25f; c.regs.VF[3].w = 1.0f;
    c.regs.VI[1] = static_cast<uint16_t>(16 + seed);
    c.vu_cycle = 100;
}

}  // namespace

int main() {
    Vu1Case* cases = new Vu1Case[3];
    Fabricate(cases[0], 0);
    Fabricate(cases[1], 1);
    Fabricate(cases[2], 2);
    Put32(cases[2].micro_mem + 32, 0x12345678u);  // a different image

    // ---- the verifier ----
    {
        std::vector<const Vu1Case*> v{&cases[0], &cases[1]};
        const Vu1AotVerifier::Result r = Vu1AotVerifier::Verify(v);
        CHECK(r.identical, "two cases of one image: %s", r.divergence.c_str());
        CHECK(r.cases == 2, "cases=%zu", r.cases);
        CHECK(r.image_hash == VuImageHash::Fast(cases[0].micro_mem, Vu1Case::kMicroSize), "image hash");
        CHECK(r.image_hash == Vu1AotVerifier::ImageHashOf(cases[1]), "same image both cases");
        CHECK(r.bundles == 2 * 4, "bundles=%llu (4 per run: ADD, SQ, E-bit NOP, delay slot)", static_cast<unsigned long long>(r.bundles));
        CHECK(r.corpus_hash != 0 && r.corpus_hash == Vu1AotVerifier::CorpusHashOf(v), "corpus hash");
        std::vector<const Vu1Case*> rev{&cases[1], &cases[0]};
        CHECK(Vu1AotVerifier::CorpusHashOf(rev) != r.corpus_hash, "the corpus hash is order-dependent");
        const Vu1AotVerifier::Result empty = Vu1AotVerifier::Verify({});
        CHECK(!empty.identical && empty.divergence == "no corpus case", "empty: %s", empty.divergence.c_str());
        std::vector<const Vu1Case*> mixed{&cases[0], &cases[2]};
        const Vu1AotVerifier::Result m = Vu1AotVerifier::Verify(mixed);
        CHECK(!m.identical && m.divergence == "mixed images" && m.image_hash == 0, "mixed: %s", m.divergence.c_str());
    }

    // ---- the tier's status machine ----
    {
        struct Seen {
            int calls = 0;
            uint64_t hash = 0;
            uint32_t pc = 0;
            Vu1AotTier::Status status = Vu1AotTier::Status::Verified;
        } seen;
        VuUnit vu(1);
        vu.dispatch().settings().vu1_mode = VuTierMode::Aot;
        Vu1AotTier& aot = vu.dispatch().aot();
        aot.SetKickObserver(
            [](void* user, VuUnit&, uint32_t pc, uint64_t hash, Vu1AotTier::Status st) {
                Seen* s = static_cast<Seen*>(user);
                ++s->calls;
                s->hash = hash;
                s->pc = pc;
                s->status = st;
            },
            &seen);
        VuUnit ref(1);
        ref.dispatch().settings().vu1_mode = VuTierMode::Interp;
        auto run_both = [&](const char* tag) {
            vu.LoadCase(cases[0]);
            ref.LoadCase(cases[0]);
            vu.Execute(0);
            ref.Execute(0);
            CHECK(std::memcmp(&vu.regs(), &ref.regs(), sizeof(VUregs)) == 0, "%s: registers differ from the interpreter", tag);
            CHECK(std::memcmp(vu.state().data, ref.state().data, Vu1Case::kDataSize) == 0, "%s: data differs", tag);
            CHECK(vu.cycle() == ref.cycle(), "%s: cycle", tag);
        };
        const uint64_t h = Vu1AotVerifier::ImageHashOf(cases[0]);
        CHECK(aot.status(h) == Vu1AotTier::Status::Unverified, "unknown image is Unverified");
        run_both("unverified");
        CHECK(aot.stats().fallback_kicks == 1 && aot.stats().native_kicks == 0, "unverified -> fallback (%llu/%llu)",
              static_cast<unsigned long long>(aot.stats().fallback_kicks), static_cast<unsigned long long>(aot.stats().native_kicks));
        CHECK(seen.calls == 1 && seen.hash == h && seen.pc == 0 && seen.status == Vu1AotTier::Status::Unverified, "observer saw the kick");
        CHECK(vu.dispatch().block().Acquire(vu)->resolve == nullptr, "fallback runs the dynamic bodies");
        CHECK(vu.dispatch().block().bundles() == 4, "the block tier ran it");

        aot.SetStatus(h, Vu1AotTier::Status::Verified);
        CHECK(aot.status(h) == Vu1AotTier::Status::Verified && aot.images() == 1 && aot.verified() == 1, "status map");
        run_both("verified");
        CHECK(aot.stats().native_kicks == 1 && aot.stats().specialized == 1 && aot.stats().native_bundles == 4, "verified -> native");
        CHECK(seen.calls == 1, "no observer call for a verified image");
        CHECK(vu.dispatch().block().Acquire(vu)->resolve == &Vu1AotTier::ResolveSpecialized, "the table is specialised");
        run_both("verified again");
        CHECK(aot.stats().native_kicks == 2 && aot.stats().specialized == 1, "specialised once per table");
        CHECK(aot.native_ns_per_bundle() > 0.0, "ns/bundle measured");

        aot.SetStatus(h, Vu1AotTier::Status::Refused);
        run_both("refused");
        CHECK(aot.stats().fallback_kicks == 2 && aot.refused() == 1, "refused -> fallback");
        CHECK(seen.calls == 2 && seen.status == Vu1AotTier::Status::Refused, "observer sees the refused kick");
        CHECK(vu.dispatch().block().Acquire(vu)->resolve == nullptr, "back to the dynamic bodies");

        aot.set_fallback(Vu1AotTier::Fallback::Interp);
        const uint64_t interp_before = vu.dispatch().interp().bundles();
        run_both("refused, interp fallback");
        CHECK(vu.dispatch().interp().bundles() == interp_before + 4, "the interpreter ran it");

        aot.set_assume_verified(true);
        run_both("assume verified");
        CHECK(aot.stats().native_kicks == 3, "assume_verified runs native");
    }

    delete[] cases;
    std::printf("Vu1AotVerifier: %s\n", g_fails ? "FAIL" : "ALL PASS");
    return g_fails ? 1 : 0;
}
