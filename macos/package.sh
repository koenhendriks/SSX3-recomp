#!/usr/bin/env bash
# On the Mac after macos/build.sh: <parent>/dist-macos/SSX3.app, SSX3-recomp-macos-arm64.dmg
# (the app + an Applications link) and .zip. Optional: <parent>/icon.png (square, 1024 px)
# -> the app icon.
set -euo pipefail
HERE="$(cd "$(dirname "$0")/.." && pwd)"; ROOT="$(dirname "$HERE")"
B="$ROOT/build-macos-arm64"; OUT="$ROOT/dist-macos"; APP="$OUT/SSX3.app"; VER="${VER:-0.2.7-macos1}"
NAME="SSX3-recomp-macos-arm64"
rm -rf "$APP" && mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/licenses"
# Only code in Contents/MacOS: other files there would carry their signature in
# extended attributes, which a zip does not keep.
cp "$B/launcher" "$B/ssx3_recomp" "$ROOT/deps/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" "$APP/Contents/MacOS/"
cp "$HERE/keys.example.ini" "$APP/Contents/Resources/"
# HD textures (Git LFS: `git lfs pull`), found by the game in Contents/Resources.
first="$(find "$HERE/hd-textures" -name "*.dds" -print -quit 2>/dev/null || true)"
if [ -n "$first" ] && [ "$(head -c 3 "$first")" = "DDS" ]; then
    cp -R "$HERE/hd-textures" "$APP/Contents/Resources/"
else
    echo "note: hd-textures are LFS pointers, not packaged (git lfs pull)"
fi
sed "s/@VER@/$VER/" "$HERE/macos/Info.plist.in" > "$APP/Contents/Info.plist"
if [ -f "$ROOT/icon.png" ]; then
    IS="$(mktemp -d)/SSX3.iconset"; mkdir -p "$IS"
    for s in 16 32 128 256 512; do
        sips -s format png -Z $s "$ROOT/icon.png" --out "$IS/icon_${s}x${s}.png" >/dev/null
        sips -s format png -Z $((s*2)) "$ROOT/icon.png" --out "$IS/icon_${s}x${s}@2x.png" >/dev/null
    done
    iconutil -c icns "$IS" -o "$APP/Contents/Resources/SSX3.icns"
    /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string SSX3" "$APP/Contents/Info.plist"
fi
L="$APP/Contents/Resources/licenses"
cp "$HERE/LICENSE" "$L/ssx3-recomp-GPL-2.0.txt"
cp "$ROOT/deps/MoltenVK/LICENSE" "$L/MoltenVK-Apache-2.0.txt"
cp "$ROOT/deps/SDL2-src/LICENSE.txt" "$L/SDL2-zlib.txt"
cp "$ROOT/deps/SDL2_ttf-src/LICENSE.txt" "$L/SDL2_ttf-zlib.txt"
cp "$ROOT/deps/SDL2_ttf-src/external/freetype/LICENSE.TXT" "$L/FreeType.txt"
cp "$ROOT/deps/epoxy-src/COPYING" "$L/libepoxy-MIT.txt"
xattr -cr "$APP"
codesign --force --deep --sign - "$APP"
codesign -v --deep "$APP"
# Disk image: the app and a link to /Applications to drag it onto.
STAGE="$(mktemp -d)/SSX 3"; mkdir -p "$STAGE"
ditto "$APP" "$STAGE/SSX3.app"
ln -s /Applications "$STAGE/Applications"
cp "$HERE/macos/READ-ME-FIRST.txt" "$STAGE/" 2>/dev/null || true
rm -f "$OUT/$NAME.dmg"
hdiutil create -volname "SSX 3" -srcfolder "$STAGE" -fs HFS+ -format UDZO -imagekey zlib-level=9 -ov "$OUT/$NAME.dmg" >/dev/null
( cd "$OUT" && rm -f "$NAME.zip" && ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent SSX3.app "$NAME.zip" )
ls -la "$OUT/$NAME.dmg" "$OUT/$NAME.zip"
