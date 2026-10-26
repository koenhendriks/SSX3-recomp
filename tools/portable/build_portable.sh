#!/usr/bin/env bash
# Build ssx3_recomp and the launcher inside Valve's Steam Runtime 3 "sniper" SDK,
# so they need glibc 2.31 and run on SteamOS, Bazzite and most distros since 2021
# (docs/LINUX.md). Needs podman, a regenerated tree (scripts/regen.sh)
# and the shaders from a normal build (ninja -C build-linux), reused as SPIR-V.
#
#   tools/portable/prepare_spv.sh               (where glslc is) -> build-portable-spv/
#   tools/portable/build_portable.sh            -> build-portable/
set -euo pipefail
TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HERE="${SSX3_REPO:-$(cd "$TOOLS/../.." && pwd)}"   # SSX3_REPO: run a copy of this script from elsewhere
PARENT="$(dirname "$HERE")"
IMG=${SNIPER_IMAGE:-registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest}
SPV="${SPV_DIR:-$HERE/build-portable-spv}"   # from tools/portable/prepare_spv.sh

[ -f "$HERE/src/recomp/gen/recomp_funcs.h" ] || { echo "run scripts/regen.sh first" >&2; exit 1; }
# The SPIR-V must be made from these exact shader sources (by content, not by time).
(cd "$HERE" && sha256sum --quiet -c "$SPV/sources.sha256") \
    || { echo "shaders changed since prepare_spv.sh: run it again" >&2; exit 1; }
podman image exists "$IMG" || podman pull -q "$IMG" > /dev/null
# The parent is mounted at the same path, so ../xboxrecomp resolves as on the host.
podman run --rm -v "$PARENT:$PARENT:z" -v "$SPV:$SPV:ro,z" -v "$TOOLS/glslc-prebuilt:/usr/local/bin/glslc:ro,z" -w "$HERE" -e SPV_DIR="$SPV" "$IMG" bash -c '
    set -e
    export CC=gcc-14 CXX=g++-14
    cmake -S . -B build-portable -G Ninja -DCMAKE_BUILD_TYPE=Release > build-portable.cmake.log
    ninja -C build-portable ssx3_recomp launcher
'
for b in ssx3_recomp launcher; do
    v=$(objdump -T "$HERE/build-portable/$b" | grep -o 'GLIBC_[0-9.]*' | sort -t. -k2,2n -k3,3n -u | tail -1)
    echo "$b needs $v"
done
