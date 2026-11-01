# Platforms: where Unifico runs, and what it does with the rest

Open Doctrines ships on more systems than a launcher makes sense on. This is
the decision for each, against the game's own lists (`tools/check_release_names.py`,
`GameUpdates::platformKey()` in `src/GameUpdates.cpp`, the release workflows and
`packaging/web/site/download.html` in Open Doctrines).

A launcher fits a desktop-class OS: a window, a file system the person owns,
a network, and a game build that unpacks from a zip. Where any of those is
missing, Unifico does not run there, but still recognises the build in a
release (`ureleases::assetKind`, `src/od/Releases.cpp`): the Installations
page names it ("also for DOS, Wii, ...") with a **Download page** button, and
never mistakes it for this machine's build.

## Launcher builds

| Unifico tag (`Unifico-<tag>.zip`) | Game updater key installed | How it is built (`.github/workflows/build.yml`) | Notes |
|---|---|---|---|
| windows-x64 / -x86 / -arm64 | windows-x64 / -x86 / -arm64 | MSVC, desktop matrix | |
| macos-arm64 / -x64 | macos-arm64 / -x64 | macos-14 | |
| linux-x64 / -arm64 | linux-x64 / -arm64 | native runners | also `Unifico-chromeos-x64.deb` / `-arm64.deb` |
| linux-x86 / -armv7 / -riscv64 | linux-x86 / -armv7 / -riscv64 | cross + QEMU user (`tools/ci_cross_deps.sh`) | |
| **raspios-arm64** | **raspios-arm64** | arm64 runner, `-DUNIFICO_GLES=ON` | new; also `Unifico-raspios-arm64.deb` |
| **raspios-armhf** | **raspios-armhf** | armhf cross, `-DUNIFICO_GLES=ON` | new; also `Unifico-raspios-armhf.deb` |
| freebsd/openbsd/netbsd -x64, -arm64 | `<bsd>-amd64`, `<bsd>-arm64` | vmactions VMs, `tools/ci_bsd.sh` | |
| **illumos-x64** | **illumos-amd64** | vmactions/openindiana-vm 202604 | from branch `claude/illumos`, merged |
| **haiku-x64** | **haiku-x64** | vmactions/haiku-vm r1beta6, system SDL3 | new |
| **serenity-x64** | **serenity-x64** | SerenityOS toolchain + system from the game's pinned commit, SDL2 + rlsw | new; tests built, not run |

Steam Deck is `linux-x64`: there is no separate build, but the launcher
behaves for it (below).

## Per platform

**Raspberry Pi OS (arm64, armhf).** Same technique as the game's `OD_GLES`:
`-DUNIFICO_GLES=ON` builds raylib for OpenGL ES 2.0 (a Pi 4/5 offers GL 3.1,
not 3.3), and makes the platform tag `raspios-arm64` / `raspios-armhf`, so the
launcher installs the game's GLES build rather than the desktop-GL
`linux-arm64` / `linux-armv7` one a Pi cannot draw. Shipped as a zip and as a
.deb (`tools/make_deb.sh`), checked by `tools/deb_check.sh raspios` in Debian
bookworm with archive.raspberrypi.com added, as the game's
`tools/raspios_check.sh` does.

**ChromeOS.** The Linux (Crostini) container is Debian 12, so the launcher is
the `linux-x64` / `linux-arm64` build as a .deb (`Unifico-chromeos-<arch>.deb`),
checked by `tools/deb_check.sh chromeos` in debian:bookworm, as the game's
`tools/chromeos_check.sh` does. It installs the game's `linux-*` zip into the
container. The .deb puts `MANAGED` beside the launcher, so it leaves updating
to apt rather than replacing itself in `/usr`. The game's Android APK is the
other ChromeOS route; the launcher lists it as a manual download.

**Steam Deck.** `linux-x64`, with the game's two window rules
(`Game::init` in Open Doctrines): the window is never larger than the screen
(shrunk to 90% of a 1280x800 panel, centred: `uwindow::fitToScreen`), and
under gamescope (Game Mode: `GAMESCOPE_WAYLAND_DISPLAY`, or
`XDG_CURRENT_DESKTOP=gamescope`) it opens fullscreen without saving that as
the person's setting. Unifico has no gamepad navigation; in Game Mode Steam
Input's default desktop layout (trackpad as mouse) drives it. Gamepad
navigation is not part of this change.

**illumos / OpenIndiana (amd64).** The `claude/illumos` branch, merged
unchanged: `__EXTENSIONS__`, libsocket/libnsl, `-z ignore`, IPS packages as
the game installs them.

**Haiku (x86_64).** As the game: GLFW has no Haiku backend, so raylib on the
system SDL3 (`libsdl3_devel`), libnetwork linked, executable path from
`get_next_image_info`, URLs through `/bin/open`. 32-bit Haiku is the gcc2
hybrid; Arm/RISC-V Haiku are not releases.

**SerenityOS (x86_64).** As the game: SDL2 from SerenityOS's ports (SDL3 has
no SerenityOS backend), raylib's software renderer rlsw
(`UNIFICO_SOFTWARE_RENDER`, on by default there) because LibGL has no shaders,
built with SerenityOS's own toolchain from the game's pinned commit
(`b5d4da53...`), plus the curl port for HTTPS. Move the commit with the game's.

## Game builds the launcher does not run on

| Game asset | Why no launcher | What Unifico does |
|---|---|---|
| `OpenDoctrines.apk` (Android), `-web` | a phone or a browser has no use for a launcher | listed as manual / ignored |
| `OpenDoctrines-vita.vpk` | installed by VitaShell; console | listed as "PS Vita", manual |
| `OpenDoctrines-wii-ppc.zip` | Homebrew Channel; console, offline | listed as "Wii", manual |
| Nintendo 3DS | console | listed as "Nintendo 3DS", manual (by name prefix `3ds`) |
| `OpenDoctrines-dos-x86.zip` | DOS: no network, single-tasking | listed as "DOS", manual |
| `OpenDoctrines-freertos.zip` | an RTOS edition, own release tags | listed as "FreeRTOS", manual |
| RISC OS, Plan 9 | no raylib backend, no launcher port | listed by prefix (`riscos`, `plan9`), manual |
| TempleOS (`open-doctrines-templeos-*.zip`) | Unifico *runs* TempleOS in QEMU (Tools tab) rather than running on it | its own `templeos-v*` releases; manual in the game list |
| `*.deb` of any desktop build (ChromeOS, Raspberry Pi OS) | for a package manager | manual; the launcher installs the zip |

A new, unknown asset name is `Other`: never installed, never an error.
`tests/unifico_test.cpp` checks every name the game's download page offers.
