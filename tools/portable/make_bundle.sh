#!/usr/bin/env bash
# Make the one-click SSX3/ folder (docs/LINUX.md) from a portable
# build (build_portable.sh, then bin/ + lib/ with the bundled libraries).
#
#   tools/portable/make_bundle.sh BUILD_OUT DEST
#
# BUILD_OUT holds bin/ssx3_recomp, bin/launcher and lib/. DEST becomes the folder.
# It holds your own game files and texture packs made from them: for your own
# machines only, never to publish.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT=${1:?build output with bin/ and lib/}; DEST=${2:?destination folder}
[ -x "$OUT/bin/ssx3_recomp" ] && [ -x "$OUT/bin/launcher" ] || { echo "no binaries in $OUT/bin" >&2; exit 1; }
[ -e "$HERE/game/default.xbe" ] || { echo "no game/default.xbe in $HERE" >&2; exit 1; }
mkdir -p "$DEST"
DEST=$(realpath "$DEST")

echo "binaries and libraries"
# A folder made before keeps its launcher.ini (the player's settings).
ini_keep=
[ -f "$DEST/bin/launcher.ini" ] && { ini_keep=$(mktemp); cp "$DEST/bin/launcher.ini" "$ini_keep"; }
rm -rf "$DEST/bin" "$DEST/lib" && cp -r "$OUT/bin" "$OUT/lib" "$DEST/"

echo "game files (a real copy, ~2 GB)"
mkdir -p "$DEST/game"
cp "$HERE/game/default.xbe" "$DEST/game/"
[ -e "$DEST/game/data" ] || cp -rL "$HERE/game/data" "$DEST/game/data"
# Your saves, only into a folder that has none: a re-run must not roll back saves made in the folder.
if [ -d "$HERE/game/UDATA" ] && [ ! -e "$DEST/game/UDATA" ]; then cp -r "$HERE/game/UDATA" "$DEST/game/"; fi

echo "HD pack, mods, keys"
rm -rf "$DEST/hd-textures" && cp -rL "$HERE/hd-textures" "$DEST/hd-textures"
mkdir -p "$DEST/mods"
for m in "$HERE"/mods/*/; do
    [ -d "$m" ] && { rm -rf "$DEST/mods/$(basename "$m")"; cp -rL "$m" "$DEST/mods/"; }
done
[ -f "$HERE/mods/load-order.txt" ] && cp "$HERE/mods/load-order.txt" "$DEST/mods/"
cp "$HERE/keys.example.ini" "$DEST/"
[ -f "$HERE/keys.ini" ] && cp "$HERE/keys.ini" "$DEST/"
# The launcher's settings, without the game folder path: it then finds game/ itself.
if [ -n "$ini_keep" ]; then mv "$ini_keep" "$DEST/bin/launcher.ini"
elif [ -f "$HERE/build-linux/launcher.ini" ]; then grep -v '^game_dir=' "$HERE/build-linux/launcher.ini" > "$DEST/bin/launcher.ini"
fi

# Start scripts: bundled libraries first. The launcher saves the game folder as a full path, so a moved
# folder would point at the old place: a saved path with no default.xbe is dropped, and game/ is found again.
start_script() {   # $1 file, $2 launcher argument
    cat > "$1" <<EOS
#!/bin/sh
here=\$(cd "\$(dirname "\$0")" && pwd)
ini="\$here/bin/launcher.ini"
gd=\$(sed -n 's/^game_dir=//p' "\$ini" 2>/dev/null)
[ -n "\$gd" ] && [ ! -f "\$gd/default.xbe" ] && sed -i '/^game_dir=/d' "\$ini"
export LD_LIBRARY_PATH="\$here/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
cd "\$here/bin" && exec ./launcher $2
EOS
    chmod +x "$1"
}
start_script "$DEST/Play SSX 3.sh" "--play"
start_script "$DEST/SSX 3 Settings.sh" ""

cat > "$DEST/install-shortcuts.sh" <<'EOS'
#!/bin/sh
# App-menu entries for "Play SSX 3" and "SSX 3 Settings", pointing at this folder.
here=$(cd "$(dirname "$0")" && pwd)
# Exec= field codes: % is %%, and " ` $ \ are escaped inside the quotes.
exec_here=$(printf '%s' "$here" | sed -e 's/%/%%/g' -e 's/[\\"`$]/\\\\&/g')
apps="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$apps"
for e in "play:Play SSX 3:Play SSX 3.sh" "settings:SSX 3 Settings:SSX 3 Settings.sh"; do
    id=${e%%:*}; rest=${e#*:}; name=${rest%%:*}; script=${rest#*:}
    cat > "$apps/ssx3-$id.desktop" <<EOD
[Desktop Entry]
Type=Application
Name=$name
Exec="$exec_here/$script"
Path=$here
Icon=applications-games
Categories=Game;
Terminal=false
EOD
done
echo "Added \"Play SSX 3\" and \"SSX 3 Settings\" to the app menu."
echo "Steam: Games > Add a Non-Steam Game > Browse > $here/Play SSX 3.sh"
EOS
chmod +x "$DEST/install-shortcuts.sh"

cat > "$DEST/README.txt" <<'EOS'
SSX 3 -- native Linux (static recompilation), your own copy
==========================================================

Start it one of three ways:

  Play SSX 3.sh        the game, straight away, with the settings you last chose.
                       Works with only a controller: use this one in Steam.
  SSX 3 Settings.sh    the settings window (resolution, Ink & Color, texture mods,
                       controllers); its Play button starts the game.
  install-shortcuts.sh puts both in your app menu, and shows how to add
                       "Play SSX 3" to Steam (Games > Add a Non-Steam Game).

In Steam's gaming mode, add "Play SSX 3.sh" as a non-Steam game; Steam shows it
fullscreen.

Needs: a Vulkan GPU driver (any current Linux, SteamOS and Bazzite have one).
Everything else is in this folder. Saves are in game/UDATA: copy the whole
folder to move the game and its saves. Logs: logs/ next to game/.

This folder holds your own game files and texture packs made from them.
Keep it for your own machines; don't share it.
EOS

echo "done: $DEST ($(du -sh "$DEST" | cut -f1))"
