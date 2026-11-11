# Changelog

Notable changes to VCD Player, release by release. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Added

- CI: CodeQL security analysis (every push/PR plus a weekly rescan),
  shellcheck for the `ci/` packaging scripts, and an optional automated
  PR review workflow (enabled by an `ANTHROPIC_API_KEY` secret).

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
