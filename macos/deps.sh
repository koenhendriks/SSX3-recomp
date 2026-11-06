#!/usr/bin/env bash
# Inside ssx3-macx: fetch and cross-build the macOS arm64 dependencies into /work/deps.
#   MoltenVK 1.4.2 (prebuilt), SDL 2.32.10 (static), SDL2_ttf 2.24.0 + FreeType (static),
#   libepoxy 1.5.10 (static). All for macOS 14+.
set -euo pipefail
D=/work/deps; M=/work/ssx3-recomp/macos; T=$M/toolchain-arm64.cmake
mkdir -p $D && cd $D
[ -d MoltenVK ] || { curl -sSL https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/MoltenVK-macos.tar | tar x; }
[ -d SDL2-src ] || { curl -sSL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz | tar xz && mv SDL2-2.32.10 SDL2-src; }
[ -d SDL2_ttf-src ] || { curl -sSL https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/SDL2_ttf-2.24.0.tar.gz | tar xz && mv SDL2_ttf-2.24.0 SDL2_ttf-src; }
[ -d epoxy-src ] || git clone -q --depth 1 -b 1.5.10 https://github.com/anholt/libepoxy.git epoxy-src
if [ ! -f sdl2-static/lib/libSDL2.a ]; then
  cmake -S SDL2-src -B /tmp/b-sdl -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$T \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL_STATIC_PIC=ON -DCMAKE_INSTALL_PREFIX=$D/sdl2-static
  ninja -C /tmp/b-sdl install
fi
if [ ! -f prefix-arm64/lib/libSDL2_ttf.a ]; then
  cmake -S SDL2_ttf-src -B /tmp/b-ttf -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$T \
    -DSDL2_DIR=/nonexistent -DSDL2_LIBRARY=$D/sdl2-static/lib/libSDL2.a -DSDL2_INCLUDE_DIR=$D/sdl2-static/include/SDL2 \
    -DBUILD_SHARED_LIBS=OFF -DSDL2TTF_VENDORED=ON -DSDL2TTF_HARFBUZZ=OFF -DSDL2TTF_SAMPLES=OFF -DSDL2TTF_INSTALL=ON \
    -DCMAKE_INSTALL_PREFIX=$D/prefix-arm64
  ninja -C /tmp/b-ttf install
fi
if [ ! -f prefix-arm64/lib/libepoxy.a ]; then
  (cd epoxy-src && meson setup /tmp/b-epoxy --cross-file $M/meson-arm64.ini --prefix=$D/prefix-arm64 \
     -Ddefault_library=static -Dglx=no -Degl=no -Dx11=false -Dtests=false -Ddocs=false && ninja -C /tmp/b-epoxy install)
fi
echo "deps ready in $D"
