#!/usr/bin/env bash
# Inside ssx3-macx: configure + build ssx3_recomp and the launcher for macOS arm64
# into /work/build-macos-arm64. Needs scripts/regen.sh done and macos/deps.sh.
#   docker run --rm -v <parent>:/work ssx3-macx /work/ssx3-recomp/macos/build.sh
set -uo pipefail
B=/work/build-macos-arm64; M=/work/ssx3-recomp/macos
export PKG_CONFIG_LIBDIR=/work/deps/prefix-arm64/lib/pkgconfig
if [ ! -f $B/build.ninja ] || [ "${RECONF:-0}" = 1 ]; then
cmake -S /work/ssx3-recomp -B $B -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$M/toolchain-arm64.cmake \
  -DSDL2_DIR=$M/cmake/SDL2 -DSDL2_ttf_DIR=$M/cmake/SDL2_ttf \
  -DVulkan_INCLUDE_DIR=/work/deps/MoltenVK/MoltenVK/include \
  -DVulkan_LIBRARY=/work/deps/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib \
  -DGLSLC=/usr/bin/glslc || exit 1
fi
ninja -C $B "$@"
