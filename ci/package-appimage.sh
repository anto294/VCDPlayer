#!/usr/bin/env bash
# Packages a built VCDPlayer binary (Linux) into an AppImage using
# linuxdeploy + its Qt plugin, which resolve and bundle every shared
# library/Qt plugin the binary actually needs.
set -euo pipefail

exe="$1"       # path to the built executable, e.g. build/VCDPlayer
arch="$2"      # x86_64 or aarch64 (linuxdeploy/appimagetool release arch suffix)
out_name="$3"  # AppImage base name, e.g. VCDPlayer-linux-x86_64

repo_root="$(pwd)"

# Nested AppImages (linuxdeploy, appimagetool) can't rely on FUSE being
# available in a CI container; this tells the AppImage runtime to
# extract-and-run itself instead of trying to mount.
export APPIMAGE_EXTRACT_AND_RUN=1

workdir="$(mktemp -d)"
appdir="$workdir/AppDir"
mkdir -p "$appdir/usr/bin" dist

cp "$exe" "$appdir/usr/bin/"

# Bundled GLSL user shaders (see shaders/README.md): CMake copies them next
# to the built exe, so they're already sitting right beside it here too.
shaders_dir="$(dirname "$exe")/shaders"
[ -d "$shaders_dir" ] && cp -r "$shaders_dir" "$appdir/usr/bin/"

cat > "$workdir/vcdplayer.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=VCD Player
Exec=VCDPlayer
Icon=vcdplayer
Categories=AudioVideo;Player;
EOF

# Pre-rendered sizes (see img/icon/, generated from img/icon-nobg.png) go
# straight into the AppDir's hicolor icon theme, so the AppImage carries a
# real multi-resolution icon set instead of a single resized copy;
# desktop environments then pick whichever size fits (menu, taskbar,
# alt-tab, ...) instead of scaling one image up or down.
for size in 16 32 48 64 128 256 512; do
  theme_dir="$appdir/usr/share/icons/hicolor/${size}x${size}/apps"
  mkdir -p "$theme_dir"
  cp "$repo_root/img/icon/${size}.png" "$theme_dir/vcdplayer.png"
done

# CI sets LINUXDEPLOY_CACHE_DIR to a path it persists with actions/cache, so
# a warm cache skips these downloads entirely (each tool is 100+ MB); a local
# run without the env var falls back to a throwaway dir under $workdir.
tools_dir="${LINUXDEPLOY_CACHE_DIR:-$workdir/tools}"
mkdir -p "$tools_dir"
[ -x "$tools_dir/linuxdeploy" ] || curl -fL -o "$tools_dir/linuxdeploy" \
  "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${arch}.AppImage"
[ -x "$tools_dir/linuxdeploy-plugin-qt" ] || curl -fL -o "$tools_dir/linuxdeploy-plugin-qt" \
  "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${arch}.AppImage"
chmod +x "$tools_dir"/linuxdeploy*

# linuxdeploy-plugin-qt only bundles the xcb platform plugin by default, so
# on a Wayland session Qt printed 'Could not find the Qt platform plugin
# "wayland"' and fell back to XWayland. Bundle the Wayland plugins too
# (the Qt plugin then also pulls in their shell-integration/decoration
# helpers). Discovered rather than hardcoded: Qt <= 6.6 ships
# libqwayland-egl.so + libqwayland-generic.so, newer Qt a single
# libqwayland.so. Skipped if the build machine has no qt6-wayland at all.
qmake_bin="$(command -v qmake6 || command -v qmake || true)"
qt_plugins_dir=""
if [ -n "$qmake_bin" ]; then
  qt_plugins_dir="$("$qmake_bin" -query QT_INSTALL_PLUGINS)"
  wayland_plugins="$(find "$qt_plugins_dir/platforms" -maxdepth 1 -name 'libqwayland*.so' -printf '%f;' 2>/dev/null || true)"
  if [ -n "$wayland_plugins" ]; then
    export EXTRA_PLATFORM_PLUGINS="${wayland_plugins%;}"
    echo "Bundling Wayland platform plugins: $EXTRA_PLATFORM_PLUGINS"
  fi
fi

cd "$workdir"
PATH="$tools_dir:$PATH" "$tools_dir/linuxdeploy" \
  --appdir "$appdir" \
  --executable "$appdir/usr/bin/$(basename "$exe")" \
  --desktop-file "$workdir/vcdplayer.desktop" \
  --icon-file "$repo_root/img/icon/256.png" \
  --plugin qt

# linuxdeploy-plugin-qt deploys the Wayland shell-integration and
# decoration plugins but not wayland-graphics-integration-client, which
# holds the wayland-egl buffer integration: without it Qt logs 'Failed to
# load client buffer integration: "wayland-egl"' and has no OpenGL on
# Wayland (breaking the Hardware/OpenGL render backend there). Copy it in
# by hand, then let linuxdeploy resolve its dependencies and fix its rpath
# in the same run that produces the AppImage.
deps_only=()
graphics_dir="$qt_plugins_dir/wayland-graphics-integration-client"
if [ -n "${EXTRA_PLATFORM_PLUGINS:-}" ] && [ -d "$graphics_dir" ]; then
  mkdir -p "$appdir/usr/plugins"
  cp -r "$graphics_dir" "$appdir/usr/plugins/"
  deps_only=(--deploy-deps-only "$appdir/usr/plugins/wayland-graphics-integration-client")
  echo "Bundling Wayland graphics integration: $(find "$graphics_dir" -name '*.so' -printf '%f ')"
fi

PATH="$tools_dir:$PATH" "$tools_dir/linuxdeploy" \
  --appdir "$appdir" \
  "${deps_only[@]}" \
  --output appimage
cd - >/dev/null

mv "$workdir"/VCD*.AppImage "dist/${out_name}.AppImage"
echo "Wrote dist/${out_name}.AppImage"
