# VCD Player

A cross-platform (Windows, Linux, macOS) desktop player for Video CDs (VCD),
built with Qt6 and [libmpv](https://mpv.io/).

It reads a disc directly from its `.cue`/`.bin` image: parses the ISO9660
filesystem and PBC (PlayBack Control) metadata, resolves every video track
(`MPEGAV/AVSEQnn.DAT`) and segment item (`SEGMENT/ITEMnnnn.DAT`), and plays
them back through mpv. PBC menus are supported (numeric keypad selection),
alongside a plain sequential-playback fallback for discs without PBC.

## Features

- Native `.cue`/`.bin` VCD image loading (no mounting/extraction required)
- Full PBC (PlayBack Control) menu navigation
- Collapsible remote-control panel: d-pad, numeric keypad, transport
  controls, volume/mute, zoom, screenshot, fullscreen
- Open via drag & drop, "Open with", or command line (`VCDPlayer.exe path\to\disc.cue`)
- Italian and English UI (Qt Linguist `.ts`/`.qm`)

## Building

### Prerequisites

- CMake >= 3.20, a C++17 compiler
- [Qt6](https://www.qt.io/) (`Widgets` + `LinguistTools` components)
- libmpv (see below — a system package on Linux/macOS, a local source build
  on Windows)

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
# macOS
brew install mpv
```

**Windows**: there's no equivalent system package, so this repo builds mpv
from source instead. Clone it into `vendor/mpv-src` and build `libmpv` with
[meson](https://mesonbuild.com/), following mpv's own [Windows compilation
guide](https://github.com/mpv-player/mpv/blob/master/DOCS/compile-windows.md):

```sh
git clone https://github.com/mpv-player/mpv vendor/mpv-src
cd vendor/mpv-src
meson setup build
meson compile -C build
```

CMake falls back to this vendored build (`vendor/mpv-src/include` +
`vendor/mpv-src/build/libmpv.dll.a`) whenever `pkg-config` can't find a
system libmpv — so it also works as a Linux/macOS fallback if you need a
specific mpv build instead of the distro's. `libmpv-2.dll` is copied next to
the built executable automatically in that case.

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

## Test samples

The `samples/` folder (git-ignored) is meant for local testing with your own
VCD `.bin`/`.cue` dumps. **Do not commit VCD image dumps** — they are
typically copyrighted commercial discs.

## License

GPLv3 or later — see [LICENSE](LICENSE). This choice follows from linking
against mpv's default GPLv2+ build; see the licensing note above.
