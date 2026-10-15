# `tests/vu/fixtures/`

The retail VU0 microprogram captures that lived here (`vu0_{shadow,vis,decal}_{code,data}.bin`:
raw 4 KB VU0 code / data memory images lifted from the title) are **game bytes** and are not in
the public tree. They are private fixtures (`cmake/Ps2rPrivateFixtures.cmake`): point
`NATIVEEE_PRIVATE_FIXTURES` at a mirror of the original layout and the tests
(`Vu0ShadowReplay`, `Vu0VisMicro`, `Vu0DecalEval`, `VuMacroBridge` section 3,
`VuCachedVsInterpCorpus`'s VU0 legs) read `$NATIVEEE_PRIVATE_FIXTURES/tests/vu/fixtures/<name>`;
without it they SKIP (ctest 77). Files dropped back into this directory are `.gitignore`d
(`*.bin`) and picked up as the fallback root.
