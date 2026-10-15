// core/hash/hash.h -- the two content hashes every subsystem keys on.
//
//   Fnv1a64 : FNV-1a, 64-bit, offset 0xcbf29ce484222325 / prime 0x100000001b3.
//             This is THE harness hash (frames.fhash / audio.ahash / mem.mhash /
//             run.meta env_hash); the seed parameter allows chaining across
//             buffers exactly as the harness emitters do.
//   Xxh64   : xxHash64 (the published algorithm, our own implementation);
//             used for texture/content cache keys where the 4-lane structure
//             beats FNV's serial dependency chain.
#pragma once

#include <cstddef>
#include <cstdint>

namespace ps2r {
namespace hash {

constexpr uint64_t kFnv1a64Offset = 0xcbf29ce484222325ull;
constexpr uint64_t kFnv1a64Prime  = 0x100000001b3ull;

// FNV-1a over `len` bytes, continuing from `seed` (default: the offset basis).
uint64_t Fnv1a64(const void* data, size_t len, uint64_t seed = kFnv1a64Offset);

// xxHash64 of `len` bytes with `seed`.
uint64_t Xxh64(const void* data, size_t len, uint64_t seed = 0);

}  // namespace hash
}  // namespace ps2r
