#!/usr/bin/env bash
# One-time setup on the Mac for the macOS build (nothing is installed on the Mac):
#   - copies a macOS SDK from the Command Line Tools into <parent>/deps/sdk
#   - builds the ssx3-macx Docker image (Ubuntu + clang/lld 18, cmake, glslc)
#   - builds the dependencies for macOS arm64 inside it (macos/deps.sh)
# <parent> is the folder holding ssx3-recomp/ and xboxrecomp/ (scripts/setup.sh).
# lld 18 cannot read the 27.x SDKs' .tbd files (arm64e.x1); 26.x works.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"; PARENT="$(dirname "$HERE")"
SDK="${SDK:-$(ls -d /Library/Developer/CommandLineTools/SDKs/MacOSX26*.sdk 2>/dev/null | grep -v 'MacOSX26.sdk$' | tail -1)}"
[ -d "$SDK" ] || { echo "no MacOSX26.x SDK found (set SDK=/path/to/MacOSX26.x.sdk)"; exit 1; }
mkdir -p "$PARENT/deps/sdk"
[ -d "$PARENT/deps/sdk/MacOSX.sdk" ] || rsync -a "$SDK/" "$PARENT/deps/sdk/MacOSX.sdk/"
docker build -t ssx3-macx "$HERE/macos"
docker run --rm -v "$PARENT":/work ssx3-macx /work/ssx3-recomp/macos/deps.sh
