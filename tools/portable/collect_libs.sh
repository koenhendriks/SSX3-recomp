#!/bin/sh
# Inside the Steam Runtime SDK container: copy the binaries and the libraries they
# need into OUT/bin and OUT/lib (tools/portable/make_bundle.sh takes it from there).
#
#   collect_libs.sh BUILD_DIR OUT
#
# Kept from the machine, never bundled: glibc, the Vulkan loader, and the display
# and sound stack (X11, xcb, Wayland, DRM, GL/EGL, ALSA, PulseAudio), which must
# match the machine. SDL2 here is sdl2-compat and loads SDL3 at run time, which
# ldd cannot see, so SDL3 and its own libraries are added by name.
set -e
B=${1:?build dir}; OUT=${2:?out dir}
rm -rf "$OUT" && mkdir -p "$OUT/bin" "$OUT/lib"
cp "$B/ssx3_recomp" "$B/launcher" "$OUT/bin/"
sdl3=/usr/lib/x86_64-linux-gnu/libSDL3.so.0
[ -e "$sdl3" ] || { echo "no $sdl3 in this image: sdl2-compat would find no SDL3 on the player's machine" >&2; exit 1; }
{ ldd "$OUT/bin/ssx3_recomp" "$OUT/bin/launcher"; echo " x => $sdl3 (0)"; ldd "$sdl3"; } \
    | awk '/=> \//{print $3}' | sort -u | while read so; do
    case $(basename "$so") in
        libc.so*|libm.so*|libpthread.so*|libdl.so*|librt.so*|ld-linux*|libresolv.so*|libutil.so*) ;;
        libvulkan.so*|libGL*|libEGL*|libgbm*|libdrm*|libX*|libxcb*|libwayland*|libxkbcommon*) ;;
        libasound.so*|libpulse*|libsndfile*|libsystemd*|libdbus*|libgcc_s.so*|libstdc++.so*) ;;
        *) cp -L "$so" "$OUT/lib/" ;;
    esac
done
ls "$OUT/lib"
