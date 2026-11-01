#!/bin/sh
# Package a built launcher as a .deb, for ChromeOS and Raspberry Pi OS.
#
#   tools/make_deb.sh <Unifico binary> <version> <amd64|arm64|armhf> <out.deb>
#
# ChromeOS installs Linux apps from a .deb (the Files app runs apt on it in
# its Debian 12 container); Raspberry Pi OS is Debian too. The game ships both
# the same way (tools/make_linux_packages.sh in Open Doctrines).
#
# Laid out as the game's package is: the program in /usr/lib/unifico with a
# MANAGED file beside it, so the launcher leaves its own updating to apt
# (src/update/SelfUpdate.cpp), and /usr/bin/Unifico -> it, which is what the
# .desktop entry's Exec= names. Built with dpkg-deb alone: nothing to install.
set -eu
bin="${1:?the Unifico binary}"
ver="${2:?version}"
arch="${3:?amd64, arm64 or armhf}"
out="${4:?output .deb}"
here=$(cd "$(dirname "$0")/.." && pwd)
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
chmod 0755 "$root"

install -d "$root/DEBIAN" "$root/usr/lib/unifico" "$root/usr/bin" \
           "$root/usr/share/applications" "$root/usr/share/icons/hicolor/256x256/apps"
install -m 0755 "$bin" "$root/usr/lib/unifico/Unifico"
echo "Installed by the package manager; it updates the launcher." > "$root/usr/lib/unifico/MANAGED"
ln -s ../lib/unifico/Unifico "$root/usr/bin/Unifico"
install -m 0644 "$here/packaging/linux/unifico.desktop" "$root/usr/share/applications/unifico.desktop"
install -m 0644 "$here/packaging/linux/unifico.png" "$root/usr/share/icons/hicolor/256x256/apps/unifico.png"
size=$(du -sk "$root/usr" | cut -f1)
# Depends: from what the binary actually links, as the game's
# make_linux_packages.sh does -- a GLES build (UNIFICO_GLES) links
# libOpenGL.so.0 where the desktop one dlopens GL, and apt must know.
deps="libc6, libstdc++6"
for lib in $(readelf -d "$bin" | sed -n 's/.*Shared library: \[\(.*\)\]/\1/p'); do
  case "$lib" in
    libcurl.so.4)     deps="$deps, libcurl4" ;;
    libX11.so.6)      deps="$deps, libx11-6" ;;
    libOpenGL.so.0)   deps="$deps, libopengl0" ;;
    libGL.so.1)       deps="$deps, libgl1" ;;
    libGLESv2.so.2)   deps="$deps, libgles2" ;;
    libEGL.so.1)      deps="$deps, libegl1" ;;
    libSDL3.so.0)     deps="$deps, libsdl3-0" ;;
    libSDL2-2.0.so.0) deps="$deps, libsdl2-2.0-0" ;;
  esac
done
# GL itself is dlopened by the desktop build: still a runtime need.
case "$deps" in *libopengl0*|*libgl1*|*libgles2*) ;; *) deps="$deps, libgl1 | libgles2" ;; esac
cat > "$root/DEBIAN/control" <<CTRL
Package: unifico
Version: $ver
Architecture: $arch
Maintainer: Open Doctrines <noreply@opendoctrines.pages.dev>
Installed-Size: $size
Depends: $deps
Section: games
Priority: optional
Homepage: https://opendoctrines.pages.dev
Description: Unifico, the Open Doctrines launcher
 Installs, plays and manages Open Doctrines versions, worlds and mods.
CTRL
mkdir -p "$(dirname "$out")"
dpkg-deb --root-owner-group --build "$root" "$out" >/dev/null
echo "built $out"
