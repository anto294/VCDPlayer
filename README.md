# VCD Player

[![Build](https://github.com/anto294/VCDPlayer/actions/workflows/build.yml/badge.svg)](https://github.com/anto294/VCDPlayer/actions/workflows/build.yml)
[![Lint](https://github.com/anto294/VCDPlayer/actions/workflows/lint.yml/badge.svg)](https://github.com/anto294/VCDPlayer/actions/workflows/lint.yml)
[![Test](https://github.com/anto294/VCDPlayer/actions/workflows/test.yml/badge.svg)](https://github.com/anto294/VCDPlayer/actions/workflows/test.yml)
[![CodeQL](https://github.com/anto294/VCDPlayer/actions/workflows/codeql.yml/badge.svg)](https://github.com/anto294/VCDPlayer/actions/workflows/codeql.yml)

A cross-platform desktop player for Video CDs (VCD), built with Qt6 and
[libmpv](https://mpv.io/). Windows and Linux are built, packaged and tested
in CI (see [Prebuilt builds](#prebuilt-builds)); macOS is also built in CI
as a sanity check (Qt6 + mpv via Homebrew), but isn't packaged into a
release artifact and isn't otherwise tested by hand.

It reads a disc directly from its `.cue`/`.bin` image: parses the ISO9660
filesystem and PBC (PlayBack Control) metadata, resolves every video track
(`MPEGAV/AVSEQnn.DAT`) and segment item (`SEGMENT/ITEMnnnn.DAT`), and plays
them back through mpv. PBC menus are supported (numeric keypad selection),
alongside a plain sequential-playback fallback for discs without PBC.

## Features

- Native `.cue`/`.bin` VCD image loading (no mounting/extraction required)
- Full PBC (PlayBack Control) menu navigation, or plain track-by-track
  playback for discs without it
- **Go to chapter** list (from the disc's `ENTRIES.VCD`, with real start
  times read from the MPEG stream) and **Disc information**
- Resume where you left off, and a **Recent discs** menu
- Karaoke channel selection: stereo, left only or right only
- Keeps the screen on while a video plays; media keys, plus MPRIS on Linux
  (the desktop's media widget, lock screen, KDE Connect, `playerctl`)
- Collapsible remote-control panel: d-pad, numeric keypad, transport
  controls, volume/mute, fullscreen
- Zoom, screenshot, always-on-top, and progress bar/time label toggles
  (menu bar and right-click context menu)
- Two mpv render backends — Software (safe default, works everywhere) and
  Hardware/OpenGL (GPU-accelerated) — picked automatically or set manually
  under **Advanced > Rendering**
- Experimental GPU upscaling presets (Anime4K for cartoons, FSRCNNX for live
  action, RAVU, AMD FSR, AMD CAS sharpening) and CPU upscaling for VCD's low
  native resolution (352x288), under the **Experimental** menu
- `.cue` file association (Linux packages and the Windows installer), plus
  drag & drop, "Open with", or command line (`VCDPlayer path/to/disc.cue`)
- Italian and English UI (Qt Linguist `.ts`/`.qm`)

## Building

### Prerequisites

- CMake >= 3.20, a C++17 compiler
- [Qt6](https://www.qt.io/) (`Widgets`, `OpenGL`, `LinguistTools` components)
- libmpv (see below — a system/MSYS2 package on Linux/macOS/Windows, or a
  local source build as a fallback)

### Getting libmpv

**Linux / macOS**: install the mpv development package from your package
manager; CMake finds it automatically via `pkg-config`.

```sh
# Debian/Ubuntu
sudo apt install libmpv-dev
# Fedora
sudo dnf install mpv-libs-devel
# Arch
sudo pacman -S mpv
# macOS (untested, see note above)
brew install mpv
```

**Windows**: the tested setup (same as CI) is [MSYS2](https://www.msys2.org/),
which packages mpv for both UCRT64 (x64) and CLANGARM64 (arm64) with a
`pkg-config` file, so CMake finds it the same way it does on Linux/macOS:

```sh
# from an MSYS2 UCRT64 or CLANGARM64 shell
pacman -S mingw-w64-ucrt-x86_64-mpv mingw-w64-ucrt-x86_64-pkgconf   # UCRT64 (x64)
pacman -S mingw-w64-clang-aarch64-mpv mingw-w64-clang-aarch64-pkgconf  # CLANGARM64 (arm64)
```

If you're building with plain MSVC instead (no MSYS2/pkg-config available),
CMake falls back to a vendored source build. Clone mpv into `vendor/mpv-src`
and build `libmpv` with [meson](https://mesonbuild.com/), following mpv's own
[Windows compilation guide](https://github.com/mpv-player/mpv/blob/master/DOCS/compile-windows.md):

```sh
git clone https://github.com/mpv-player/mpv vendor/mpv-src
cd vendor/mpv-src
meson setup build
meson compile -C build
```

CMake uses this vendored build (`vendor/mpv-src/include` +
`vendor/mpv-src/build/libmpv.dll.a`) whenever `pkg-config` can't find a
system libmpv — so it's also a fallback on Linux/macOS if you need a specific
mpv build instead of the distro's. `libmpv-2.dll` is copied next to the built
executable automatically in that case.

> **Licensing note:** mpv defaults to a GPLv2+ build (`meson setup
> -Dgpl=true`, the default; most distro packages ship this too). Because VCD
> Player links against it, the combined binary is a GPLv2+/GPLv3 work — this
> is why VCD Player itself is licensed GPLv3 (see [LICENSE](LICENSE)). If you
> need a differently licensed build, reconfigure mpv with `-Dgpl=false`
> (LGPLv2.1+) instead.

### Building VCD Player

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=<path-to-your-Qt6-kit>
cmake --build build
```

## Prebuilt builds

Download them from [Releases](https://github.com/anto294/VCDPlayer/releases)
(every push also builds them: see the **Actions** tab). All come for x64 /
x86_64 and arm64:

| Package | Platform | Notes |
|---|---|---|
| `VCDPlayer-windows-*-setup.exe` | Windows 10/11 | Installer: Start menu entry, `.cue` association, uninstaller. Per-user (no admin) or for all users. |
| `VCDPlayer-windows-*-portable.zip` | Windows 10/11 | Unzip anywhere and run `VCDPlayer.exe`. |
| `VCDPlayer-linux-*.flatpak` | Any Linux with Flatpak | `flatpak install --user VCDPlayer-linux-x86_64.flatpak` (pulls the KDE runtime from Flathub). |
| `VCDPlayer-linux-*.deb` | Ubuntu 24.04+, Debian 13+ | `sudo apt install ./VCDPlayer-linux-x86_64.deb` |
| `VCDPlayer-linux-*.AppImage` | Any recent Linux | `chmod +x` and run; no install. |

The Linux packages (except the AppImage, unless integrated by a tool such as
Gear Lever) register VCD Player for `.cue` files. The Flatpak can read your
home folder and removable media (a `.cue`'s `.bin` sits next to it) and
writes only screenshots, to `Pictures/VCDPlayer`.

## Test samples

The `samples/` folder (git-ignored) is meant for local testing with your own
VCD `.bin`/`.cue` dumps. **Do not commit VCD image dumps** — they are
typically copyrighted commercial discs.

## Contributing

Code style is enforced by [.clang-format](.clang-format) and
[.clang-tidy](.clang-tidy); the **Lint** workflow checks both on every PR
(plus shellcheck on the `ci/` scripts and actionlint on the workflows).
**CodeQL** runs a security analysis of every PR, and when the repository has
an `ANTHROPIC_API_KEY` secret the **Review** workflow also posts an automated
code review. The **Test** workflow builds the disc-parsing core and its unit
tests (`tests/`) under AddressSanitizer/UBSan. To run them locally — no Qt or
libmpv needed:

```sh
cmake -S . -B build-tests -DVCDPLAYER_BUILD_APP=OFF \
  "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all"
cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
```

To run the style checks locally:

```sh
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i
clang-tidy -p build src/**/*.cpp   # needs a build/ configured with
                                    # -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

## Troubleshooting

mpv only logs warnings and errors by default. To see its full log on
stderr while diagnosing a playback problem, start VCD Player with
`VCDPLAYER_MPV_LOG=v` (or `debug`) in the environment.

## License

GPLv3 or later — see [LICENSE](LICENSE). This choice follows from linking
against mpv's default GPLv2+ build; see the licensing note above.

Bundles third-party mpv upscaling shaders (Anime4K, FSRCNNX, RAVU, AMD
FidelityFX FSR/CAS; MIT and LGPL-3.0) — sources, versions and licenses in
[shaders/README.md](shaders/README.md).
