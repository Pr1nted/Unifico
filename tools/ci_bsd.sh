#!/bin/sh
# Build, test and package the launcher inside a BSD VM -- the `run` of every
# BSD job in .github/workflows/build.yml.
#
#   tools/ci_bsd.sh <tag> <expected ELF machine> [extra cmake arguments...]
#   tools/ci_bsd.sh netbsd-arm64 aarch64 -DCMAKE_PREFIX_PATH=/usr/X11R7
#
# One script for FreeBSD, OpenBSD and NetBSD on every architecture, so the
# three VM steps differ only in how they install packages. Plain sh: it is
# what all three run, whatever the login shell.
#
# THE TESTS RUN HERE, not only the build. A BSD job that only compiled would
# be green for a launcher that cannot read a release list or verify a grant on
# that system -- the desktop jobs run UnificoTest, so these do too.
set -eu

tag="${1:?platform tag, e.g. freebsd-x64}"
want="${2:?expected machine, e.g. x86_64}"
shift 2

uname -a
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "-DOD_ACHIEVEMENT_KEYS=${OD_ACHIEVEMENT_KEYS:-}" "$@"
# getconf on Linux-likes, sysctl on the BSDs, psrinfo on illumos.
jobs=$(getconf NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || psrinfo | wc -l)
cmake --build build -j $jobs
cmake --build build --target UnificoTest -j 2
# With the tag: the launcher must call this platform what it is released as,
# or its self-updater asks for a zip that is never published.
./build/UnificoTest . "$tag"

# The launcher is the architecture this job says it is. Under emulation a
# mistake here -- an x86 image booted for an "arm64" job -- would otherwise
# publish an x86 binary under an Arm name, and nothing below would notice.
python3 - build/Unifico "$want" <<'EOF'
import sys
path, want = sys.argv[1], sys.argv[2]
elf = {62: "x86_64", 183: "aarch64"}
b = open(path, "rb").read(64)
got = elf.get(int.from_bytes(b[18:20], "little")) if b[:4] == b"\x7fELF" else None
print(f"{path}: {got} (want {want})")
sys.exit(0 if got == want else 1)
EOF

mkdir -p dist pkg
cp build/Unifico packaging/linux/unifico.desktop packaging/linux/unifico.png pkg/
(cd pkg && zip -q "../dist/Unifico-$tag.zip" *)
ls -l dist
