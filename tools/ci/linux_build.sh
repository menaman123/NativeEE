#!/usr/bin/env bash
# tools/ci/linux_build.sh -- configure + build one Linux preset and run the
# component suite under a virtual X display (the GL tests need a GLX context;
# Mesa llvmpipe provides GL 4.5 core). Used by tools/ci/linux.Dockerfile's
# `build` stage and by the bind-mount driver:
#
#   docker run --rm -v "$PWD":/src -w /src nativeee-linux-toolchain \
#       ps2r-linux-build linux-release-gcc 3
#
#   $1  preset        (default linux-release-gcc; linux-release-clang for the Clang lane)
#   $2  build jobs    (default 3 -- the recompiled shards are 3-4 MB TUs at -O2)
#   $3  ctest jobs    (default 2)
#   PS2R_SRC          the tree (default: the current directory)
#
# Exit = the first failing step. The configure, build and ctest logs land in the
# build dir (ps2r_configure.log / ps2r_build.log / ps2r_ctest.log) for the CI
# artifact upload.
set -euo pipefail
PRESET="${1:-linux-release-gcc}"
JOBS="${2:-3}"
TEST_JOBS="${3:-2}"
cd "${PS2R_SRC:-$PWD}"
[ -f CMakePresets.json ] || { echo "ps2r-linux-build: no CMakePresets.json in $PWD (set PS2R_SRC)"; exit 2; }
BUILD_DIR="build-${PRESET}"
mkdir -p "${BUILD_DIR}"

echo "== ps2r-linux-build: preset ${PRESET} jobs ${JOBS} ($(uname -m), $(nproc) cpus, $(grep MemTotal /proc/meminfo | awk '{printf "%.1f GiB", $2/1048576}'))"
cmake --preset "${PRESET}" > "${BUILD_DIR}/ps2r_configure.log" 2>&1 || { tail -n 40 "${BUILD_DIR}/ps2r_configure.log"; echo "== configure FAILED"; exit 1; }
grep -E "host platform target|SDL2|FFmpeg found|build type|CMAKE_CXX_COMPILER|compiler identification" "${BUILD_DIR}/ps2r_configure.log" || true
grep -E "CMAKE_CXX_COMPILER(:|=)" "${BUILD_DIR}/CMakeCache.txt" | head -n 1 || true

echo "== build (log: ${BUILD_DIR}/ps2r_build.log)"
build_rc=0
cmake --build --preset "${PRESET}" -j "${JOBS}" > "${BUILD_DIR}/ps2r_build.log" 2>&1 || build_rc=$?
grep -E "warning:|FAILED|ninja: build stopped|Linking CXX executable" "${BUILD_DIR}/ps2r_build.log" | head -n 60 || true
if [ "${build_rc}" -ne 0 ]; then
    echo "== build FAILED (exit ${build_rc}; see ${BUILD_DIR}/ps2r_build.log)"
    grep -n -B2 -A6 "error:" "${BUILD_DIR}/ps2r_build.log" | head -n 120
    exit 1
fi
bin_note="nativeee not built (no title input: engine + component tests only)"
[ -f "${BUILD_DIR}/app/host/nativeee" ] && bin_note="nativeee $(stat -c %s "${BUILD_DIR}/app/host/nativeee") bytes"
echo "== build ok: $(grep -c 'Building CXX' "${BUILD_DIR}/ps2r_build.log") TUs compiled; ${bin_note}; warnings: $(grep -c 'warning:' "${BUILD_DIR}/ps2r_build.log" || true)"

echo "== component suite (ctest -j${TEST_JOBS} under Xvfb; Root legs SKIP without game files)"
set +e
xvfb-run -a -s "-screen 0 1280x1024x24" \
    ctest --preset "${PRESET}" -j "${TEST_JOBS}" --timeout 600 > "${BUILD_DIR}/ps2r_ctest.log" 2>&1
rc=$?
set -e
grep -E "^[0-9]+% tests passed|tests failed out of|Not Run|\*\*\*Failed|\*\*\*Timeout|\*\*\*Exception" "${BUILD_DIR}/ps2r_ctest.log" || true
echo "== ctest exit ${rc}"
exit "${rc}"
