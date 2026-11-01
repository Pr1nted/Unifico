#!/bin/sh
# Install a launcher .deb the way ChromeOS or Raspberry Pi OS would, and check
# it would run there. Adapted from the game's tools/chromeos_check.sh and
# tools/raspios_check.sh.
#
#   tools/deb_check.sh <unifico.deb> <chromeos|raspios> <amd64|arm64|armhf>
#
# chromeos: Debian 12 (bookworm), the Linux container a Chromebook runs.
# raspios:  Debian bookworm plus archive.raspberrypi.com, which is what
#           Raspberry Pi OS is. armhf runs under QEMU (setup-qemu-action).
#
# It holds the package to: apt installs it with only the archive behind it;
# every library the launcher links resolves; the .desktop entry the app grid
# or Pi menu lists names a command that is installed; and MANAGED is there, so
# the launcher never tries to replace itself in /usr. It opens no window.
set -eu
deb="${1:?a .deb to check}"
os="${2:?chromeos or raspios}"
arch="${3:?amd64, arm64 or armhf}"
[ -f "$deb" ] || { echo "no such file: $deb"; exit 1; }
case "$arch" in
    amd64) platform=linux/amd64 ;;
    arm64) platform=linux/arm64 ;;
    armhf) platform=linux/arm/v7 ;;
    *) echo "unknown arch $arch"; exit 1 ;;
esac
case "$os" in chromeos|raspios) ;; *) echo "os must be chromeos or raspios"; exit 1 ;; esac
dir=$(cd "$(dirname "$deb")" && pwd)
name=$(basename "$deb")
image="${DEB_CHECK_IMAGE:-mirror.gcr.io/library/debian:bookworm}"

docker run --rm --platform "$platform" -v "$dir:/pkg:ro" -e OS="$os" "$image" sh -euc '
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -qq
  if [ "$OS" = raspios ]; then
    apt-get install -y -qq ca-certificates curl gnupg >/dev/null
    curl -fsSL https://archive.raspberrypi.com/debian/raspberrypi.gpg.key \
      | gpg --dearmor -o /usr/share/keyrings/raspberrypi-archive-keyring.gpg
    echo "deb [signed-by=/usr/share/keyrings/raspberrypi-archive-keyring.gpg] http://archive.raspberrypi.com/debian/ bookworm main" \
      > /etc/apt/sources.list.d/raspi.list
    apt-get update -qq
    apt-get upgrade -y -qq >/dev/null
  fi
  apt-get install -y -qq "/pkg/'"$name"'" >/dev/null
  echo "installed in Debian $(cat /etc/debian_version) ($(dpkg --print-architecture)) as $OS"
  exe=$(dpkg -L unifico | grep -E "/Unifico$" | grep /usr/lib/ | head -1)
  [ -n "$exe" ] && [ -x "$exe" ] || { echo "no Unifico executable in the package"; exit 1; }
  missing=$(ldd "$exe" | grep "not found" || true)
  [ -z "$missing" ] || { echo "unresolved libraries:"; echo "$missing"; exit 1; }
  echo "every library resolves: $exe"
  [ -f "$(dirname "$exe")/MANAGED" ] || { echo "no MANAGED beside the launcher: it would try to update itself in /usr"; exit 1; }
  desktop=$(dpkg -L unifico | grep -E "^/usr/share/applications/.*\.desktop$" | head -1)
  [ -n "$desktop" ] || { echo "no desktop entry"; exit 1; }
  cmd=$(sed -n "s/^Exec=\([^ ]*\).*/\1/p" "$desktop" | head -1)
  command -v "$cmd" >/dev/null || { echo "desktop entry runs $cmd, which is not installed"; exit 1; }
  echo "launcher entry: $desktop -> $(command -v "$cmd")"
'
