// vu1_aot_cold.inl -- the COLD calls of a generated block function (included by
// vu1_aot_prelude.h only). Every op of a block may call XgKick::Pace (the paced
// XGKICK drain) -- an out-of-line call at 30+ sites per function made the
// register allocator spill the live register file on the HOT path (a store
// before every cold branch) to have it in memory for the call. Under clang the
// wrappers carry the `preserve_all` calling convention: the callee saves every
// register it touches (the SIMD file included), so the caller keeps its values
// in registers across the call and the saving cost moves to the cold path.
// Elsewhere a plain call (the code is identical in effect; only the register
// traffic differs). Not on Windows: clang targeting x86-64 Windows has no
// preserve_all lowering (clang 18 crashes generating code for such a callee, with
// no diagnostic). The clock goes by VALUE and comes back in the result, so the
// caller's local never escapes.
#if defined(__clang__) && !defined(_WIN32)
#if __has_attribute(preserve_all)
#define VU1_AOT_COLD __attribute__((preserve_all, noinline))
#endif
#endif
#if !defined(VU1_AOT_COLD)
#define VU1_AOT_COLD
#endif
struct Vu1AotClockResult {
    uint64_t cycle;
    uint64_t pace_due;
};
VU1_AOT_COLD inline Vu1AotClockResult Vu1AotXgkPace(XgKick* xgk, const uint8_t* data, uint64_t cycle) {
    xgk->Pace(data, cycle);
    return Vu1AotClockResult{cycle, xgk->armed() ? xgk->next_due() : ~0ull};
}
VU1_AOT_COLD inline Vu1AotClockResult Vu1AotXgkArm(XgKick* xgk, const uint8_t* data, uint32_t vi_addr, uint64_t cycle) {
    xgk->Arm(data, vi_addr, cycle);
    return Vu1AotClockResult{cycle, xgk->armed() ? xgk->next_due() : ~0ull};
}
