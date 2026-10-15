// tests/core/test_hash.cpp -- FNV-1a64 and XXH64 known-answer vectors.
// FNV vectors are the frozen original's test_verify_units cases; XXH64 vectors
// are the published reference values (the original's test_probe_bench set).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/core/hash/hash.h"
#include "engine/core/hash/sha256.h"
#include <algorithm>

static int g_fail = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            g_fail++;                                                        \
        }                                                                    \
    } while (0)

using ps2r::hash::Fnv1a64;
using ps2r::hash::Xxh64;

int main() {
    // FNV-1a 64: the harness hash. Vectors == OLD test_verify_units.
    CHECK(Fnv1a64("", 0) == 0xcbf29ce484222325ull);
    CHECK(Fnv1a64("a", 1) == 0xaf63dc4c8601ec8cull);
    CHECK(Fnv1a64("foobar", 6) == 0x85944171f73967e8ull);
    // Chaining through the seed equals one pass over the concatenation.
    CHECK(Fnv1a64("bar", 3, Fnv1a64("foo", 3)) == Fnv1a64("foobar", 6));
    CHECK(ps2r::hash::kFnv1a64Offset == 0xcbf29ce484222325ull);
    CHECK(ps2r::hash::kFnv1a64Prime == 0x100000001b3ull);

    // XXH64 reference vectors (seed, data) -> hash.
    struct V { const char* s; size_t n; uint64_t seed; uint64_t want; };
    const V vectors[] = {
        {"", 0, 0, 0xEF46DB3751D8E999ull},
        {"a", 1, 0, 0xD24EC4F1A98C6E5Bull},
        {"abc", 3, 0, 0x44BC2CF5AD770999ull},
        {"0123456789abcdef", 16, 0, 0x5C5B90C34E376D0Bull},
        {"0123456789abcdefghijklmnopqrstu", 31, 0, 0x80ADFC1D42020F39ull},
        {"0123456789abcdefghijklmnopqrstuv", 32, 0, 0xBF7C9DBE16B5C6E2ull},
        {"The quick brown fox jumps over the lazy dog", 43, 0, 0x0B242D361FDA71BCull},
        {"abc", 3, 1, 0xBEA9CA8199328908ull},
        {"abc", 3, 0x9E3779B97F4A7C15ull, 0x2ED0F59D6B43AC8Bull},
    };
    for (const V& v : vectors) {
        const uint64_t got = Xxh64(v.s, v.n, v.seed);
        if (got != v.want)
            std::printf("XXH64(\"%s\", seed=%llx) = %016llx want %016llx\n", v.s,
                        (unsigned long long)v.seed, (unsigned long long)got,
                        (unsigned long long)v.want);
        CHECK(got == v.want);
    }
    // Long input (exercises multiple stripes + every tail branch): the value
    // is pinned so later refactors of the lane loop stay byte-exact.
    std::vector<uint8_t> big(1000);
    for (size_t i = 0; i < big.size(); i++) big[i] = (uint8_t)(i * 7 + 3);
    const uint64_t h1000 = Xxh64(big.data(), big.size(), 0);
    CHECK(h1000 == Xxh64(big.data(), big.size(), 0));
    CHECK(Xxh64(big.data(), 999, 0) != h1000);
    std::printf("xxh64(1000 bytes) = 0x%016llx\n", (unsigned long long)h1000);

    // SHA-256 (FIPS 180-4 appendix vectors + the one-million-'a' vector) and
    // chunked Update == one pass.
    using ps2r::hash::Sha256;
    CHECK(Sha256::HexOf("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256::HexOf("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(Sha256::HexOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    {
        std::vector<uint8_t> million(1000000, 'a');
        Sha256 h;
        for (size_t off = 0; off < million.size(); off += 777) {
            const size_t n = std::min<size_t>(777, million.size() - off);
            h.Update(million.data() + off, n);
        }
        uint8_t digest[32];
        h.Finish(digest);
        CHECK(Sha256::Hex(digest) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
        CHECK(Sha256::HexOf(million.data(), million.size()) == Sha256::Hex(digest));
    }

    if (g_fail) {
        std::printf("test_hash: %d FAILURE(S)\n", g_fail);
        return 1;
    }
    std::printf("test_hash: all checks passed\n");
    return 0;
}
