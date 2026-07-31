# VCD Player

A Windows desktop player for Video CDs (VCD), built with Qt6 and [libmpv](https://mpv.io/).

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

- Windows, CMake >= 3.20, a C++17 compiler (MSVC or MinGW matching your Qt kit)
- [Qt6](https://www.qt.io/) (`Widgets` + `LinguistTools` components)
- [mpv source tree](https://github.com/mpv-player/mpv) with a built `libmpv`,
  see below

### Getting libmpv

This repo does not vendor mpv's source. Clone it into `vendor/mpv-src` and
build `libmpv` with [meson](https://mesonbuild.com/), following mpv's own
[Windows compilation guide](https://github.com/mpv-player/mpv/blob/master/DOCS/compile-windows.md):

```sh
git clone https://github.com/mpv-player/mpv vendor/mpv-src
cd vendor/mpv-src
meson setup build
meson compile -C build
```

The project expects `vendor/mpv-src/include` (headers) and
`vendor/mpv-src/build/libmpv.dll.a` + `libmpv-2.dll` (import lib and runtime
DLL) to exist after this step; `libmpv-2.dll` is copied next to the built
executable automatically.

> **Licensing note:** mpv defaults to a GPLv2+ build (`meson setup
> -Dgpl=true`, the default). Because VCD Player links against it, the
> combined binary is a GPLv2+/GPLv3 work — this is why VCD Player itself is
> licensed GPLv3 (see [LICENSE](LICENSE)). If you need a differently
> licensed build, reconfigure mpv with `-Dgpl=false` (LGPLv2.1+) instead.

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
