#!/usr/bin/env bash
# On a machine with glslc: build the shaders (ninja -C build-linux) and put the
# SPIR-V plus a manifest of the shader sources' sha256 in OUT (default
# build-portable-spv/). build_portable.sh reuses them and checks the manifest.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT=${1:-$HERE/build-portable-spv}
ninja -C "$HERE/build-linux" vk/libvkr.a > /dev/null
rm -rf "$OUT" && mkdir -p "$OUT"
cp "$HERE"/build-linux/vk/vkr_gen/*.spv "$OUT/"
(cd "$HERE" && sha256sum src/vk/shaders/*) > "$OUT/sources.sha256"
echo "$(ls "$OUT"/*.spv | wc -l) shaders in $OUT"
