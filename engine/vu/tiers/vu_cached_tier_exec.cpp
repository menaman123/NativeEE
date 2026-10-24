// VuCachedTier -- the kind -> body TABLES (the bodies themselves are the
// templates of vu_cached_tier_exec.inl, included through the header): one
// resolved body per kind for the cached tier's per-bundle dispatch and the
// block tier's pre-resolved ops, and one per (kind, dest) for the AOT tier's
// specialised resolver. The (kind, dest) entries are the SAME text with the
// field read replaced by the parameter, so dynamic and specialised are the
// identity by construction (and proven so by Vu1AotSpecializer).
#include "engine/vu/tiers/vu_cached_tier.h"

#include <array>
#include <utility>

// The kind -> body tables: entry K is &ExecUpperK<K> / &ExecLowerK<K>, the
// compile-time recursion unrolled over the kind range once at load.
const std::array<VuCachedTier::UpperFn, VUK_U_COUNT> VuCachedTier::kUpperTable = [] {
    std::array<UpperFn, VUK_U_COUNT> t{};
    auto fill = [&t](auto self, auto kc) {
        constexpr uint8_t K = decltype(kc)::value;
        t[K] = &VuCachedTier::ExecUpperK<K>;
        if constexpr (K + 1 < VUK_U_COUNT) self(self, std::integral_constant<uint8_t, K + 1>{});
    };
    fill(fill, std::integral_constant<uint8_t, 0>{});
    return t;
}();

const std::array<VuCachedTier::LowerFn, VUK_L_COUNT> VuCachedTier::kLowerTable = [] {
    std::array<LowerFn, VUK_L_COUNT> t{};
    auto fill = [&t](auto self, auto kc) {
        constexpr uint8_t K = decltype(kc)::value;
        t[K] = &VuCachedTier::ExecLowerK<K>;
        if constexpr (K + 1 < VUK_L_COUNT) self(self, std::integral_constant<uint8_t, K + 1>{});
    };
    fill(fill, std::integral_constant<uint8_t, 0>{});
    return t;
}();

// The (kind, dest) tables: entry K * 16 + D is &ExecUpperK<K, D>; a lower kind
// that never reads dest keeps its dynamic body in every D slot.
const std::array<VuCachedTier::UpperFn, VUK_U_COUNT * 16> VuCachedTier::kUpperTableD = [] {
    std::array<UpperFn, VUK_U_COUNT * 16> t{};
    auto fill_d = [&t](auto self, auto kc, auto dc) {
        constexpr uint8_t K = decltype(kc)::value;
        constexpr int D = decltype(dc)::value;
        t[K * 16 + D] = &VuCachedTier::ExecUpperK<K, D>;
        if constexpr (D + 1 < 16) self(self, kc, std::integral_constant<int, D + 1>{});
    };
    auto fill_k = [&](auto self, auto kc) {
        fill_d(fill_d, kc, std::integral_constant<int, 0>{});
        constexpr uint8_t K = decltype(kc)::value;
        if constexpr (K + 1 < VUK_U_COUNT) self(self, std::integral_constant<uint8_t, K + 1>{});
    };
    fill_k(fill_k, std::integral_constant<uint8_t, 0>{});
    return t;
}();

bool VuCachedTier::LowerKindReadsDest(uint8_t k) {
    switch (k) {
        case VUK_L_LQ: case VUK_L_SQ: case VUK_L_ILW: case VUK_L_ISW: case VUK_L_MOVE: case VUK_L_MR32:
        case VUK_L_MFIR: case VUK_L_RGET: case VUK_L_LQI: case VUK_L_SQI:
            return true;
        default:
            return false;
    }
}

const std::array<VuCachedTier::LowerFn, VUK_L_COUNT * 16> VuCachedTier::kLowerTableD = [] {
    std::array<LowerFn, VUK_L_COUNT * 16> t{};
    auto fill_d = [&t](auto self, auto kc, auto dc) {
        constexpr uint8_t K = decltype(kc)::value;
        constexpr int D = decltype(dc)::value;
        t[K * 16 + D] = &VuCachedTier::ExecLowerK<K, D>;
        if constexpr (D + 1 < 16) self(self, kc, std::integral_constant<int, D + 1>{});
    };
    auto fill_k = [&](auto self, auto kc) {
        constexpr uint8_t K = decltype(kc)::value;
        if (LowerKindReadsDest(K)) {
            fill_d(fill_d, kc, std::integral_constant<int, 0>{});
        } else {
            for (int d = 0; d < 16; ++d) t[K * 16 + d] = &VuCachedTier::ExecLowerK<K>;
        }
        if constexpr (K + 1 < VUK_L_COUNT) self(self, std::integral_constant<uint8_t, K + 1>{});
    };
    fill_k(fill_k, std::integral_constant<uint8_t, 0>{});
    return t;
}();

VuCachedTier::UpperFn VuCachedTier::UpperFastFn(uint8_t upper_kind) {
    return kUpperTable[upper_kind < VUK_U_COUNT ? upper_kind : VUK_U_GENERIC];
}

VuCachedTier::LowerFn VuCachedTier::LowerFastFn(uint8_t lower_kind) {
    return kLowerTable[lower_kind < VUK_L_COUNT ? lower_kind : VUK_L_GENERIC];
}

VuCachedTier::UpperFn VuCachedTier::UpperFastFnD(uint8_t upper_kind, uint8_t dest) {
    const uint8_t k = upper_kind < VUK_U_COUNT ? upper_kind : VUK_U_GENERIC;
    return kUpperTableD[k * 16 + (dest & 0xFu)];
}

VuCachedTier::LowerFn VuCachedTier::LowerFastFnD(uint8_t lower_kind, uint8_t dest) {
    const uint8_t k = lower_kind < VUK_L_COUNT ? lower_kind : VUK_L_GENERIC;
    return kLowerTableD[k * 16 + (dest & 0xFu)];
}

void VuCachedTier::ExecUpperFast(VuUnit& u, const VuDecodedSlot& s) { kUpperTable[s.upper_kind](generic_, u, s); }

void VuCachedTier::ExecLowerFast(VuUnit& u, const VuDecodedSlot& s, uint32_t pc, bool& branch_taken,
                                 uint32_t& branch_target) {
    kLowerTable[s.lower_kind](generic_, u, s, pc, branch_taken, branch_target);
}
