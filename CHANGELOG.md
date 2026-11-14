# Changelog

Notable changes to VCD Player, release by release. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/).

## [v1.0.0] - 2026-11-14

### Added

- CI: CodeQL security analysis (every push/PR plus a weekly rescan),
  shellcheck for the `ci/` packaging scripts, and an optional automated
  PR review workflow (enabled by an `ANTHROPIC_API_KEY` secret).
- Disc images split into one `.bin` per track (multi-`FILE` `.cue`).
- Multi-digit menu numbers (menus with 10+ choices) and support for menus
  numbered from a base other than 1.
- Working Stop button; Play after Stop or at the end of the disc restarts
  it. Space toggles play/pause; clicking the seek bar jumps there.
- View toggles, volume and window size/position are remembered; the
  progress bar and time are shown by default.
- The UI starts in Italian on Italian systems.
- Unit tests for the disc-reading core, run in CI under ASan/UBSan.
- GPU upscaling presets under **Experimental > GPU upscaling**: Anime4K
  (cartoons, now with its artifact-removing Restore pass), FSRCNNX (live
  action), RAVU, AMD FSR and AMD CAS sharpening.

### Changed

- The window can be made much smaller (the remote panel scrolls when it
  doesn't fit), so it fits small or scaled laptop screens.
- Disc loading errors are shown in the UI language, with technical details
  underneath.

### Fixed

- PBC never advanced on its own: films didn't return to the menu, menu
  timeouts and play lists never moved on.
- Every video track started 2 seconds late and ran 2 seconds into the next
  one (and chapter points were off by as much) on images whose `.cue` has a
  `PREGAP`.
- Menus and play lists now follow the VCD timing rules: an item's repeat
  count, the wait before a timeout jumps, every item of a play list (only
  the first was played), and "play then continue" nodes that stopped
  playback instead of returning to the menu.
- Hardware (OpenGL) rendering showed the video upside down.
- Keyboard shortcuts stopped working in fullscreen.
- The menu bar could be unreadable (dark text on dark background) with the
  Breeze style when the KDE colour scheme couldn't be read.
- The picture stayed on screen after Stop.
- Corrupt or crafted disc images could crash the player or make it read
  past its buffers; they are now rejected with an error.
- `.cue` files saved with a UTF-8 BOM, naming the `.bin` with different
  case, or with a Windows path, failed to open; unsupported sector formats
  (e.g. `MODE2/2336`) are now reported instead of playing garbage.

- Linux/macOS: crash at startup (`mpv_create() failed`) on systems with a
  non-English locale that uses a decimal comma (Italian, German, French,
  ...).
- Linux AppImage: now runs natively on Wayland, with OpenGL, instead of
  falling back to XWayland.
- CI: macOS build moved off GitHub's retired `macos-14` runner; Windows x64
  build moved to MSYS2 UCRT64, since MSYS2 no longer ships mpv for MINGW64.

## [v0.1.0-alpha] - 2026-08-26

### Added

- About dialog (**Info** menu) and a dynamic window title showing the
  currently open disc/file.
- Multi-resolution app icon (16 to 512px), embedded as a Qt resource and
  used verbatim for the Linux AppImage's icon theme too.
- Hardware (OpenGL) render backend for mpv, alongside the existing
  Software one — GPU-accelerated decoding/scaling with automatic
  detection of known software rasterizers (llvmpipe, SwiftShader, ...) so
  "Automatic" never picks a backend known to misrender on them. Selectable
  under **Advanced > Rendering**.
- Experimental upscaling for VCD's low native resolution (352x288), under
  the **Experimental** menu:
  - AI upscaling via a bundled Anime4K GPU shader (MIT-licensed, GPU/OpenGL
    backend only).
  - CPU upscaling (sharpening pass on top of mpv's software scaler).
- Lint CI workflow: `clang-format`, `clang-tidy`, and `actionlint` checks
  on every PR, plus `.clang-format`/`.clang-tidy` configs (existing source
  reformatted to match).
- macOS CI job (build-only sanity check, not packaged/released).
- Issue templates, a PR template, and CODEOWNERS.
- Dependabot config to keep GitHub Actions versions current.

### Changed

- Remote-control panel simplified: zoom and screenshot moved out of it
  (they stay in the menu bar / right-click context menu, with their
  keyboard shortcuts) to keep the panel focused on disc navigation.
- CI/CD pipeline hardened: pinned action versions, least-privilege
  `permissions`, concurrency cancellation for superseded runs, per-job
  timeouts, `ccache` on Windows/Linux/macOS, cached `linuxdeploy` downloads,
  fail-loudly if packaging produces no artifact, auto-generated release
  notes.

### Fixed

- Windows arm64 packaging hanging indefinitely: `ldd` was actually
  executing the built GUI executable to resolve its imports, blocking on
  its event loop. Dependency resolution now reads the PE import table
  statically (`objdump -p`) instead.
- Linux AppImage packaging rejecting the app icon (wrong resolution) —
  now ships a proper multi-resolution hicolor icon set instead of a single
  resized image.
- Various earlier CI-only issues: missing `Qt6LinguistTools` CMake package
  on Ubuntu, lost executable bit on `ci/*.sh` after checkout, CI not
  triggering on pushes to `master`.

## [v0.0.1] - 2026-07-31

Initial public release: cross-platform (Windows x64/arm64, Linux
x86_64/arm64) VCD player with full PBC menu support, built with Qt6 and
libmpv.
