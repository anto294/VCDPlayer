#!/usr/bin/env bash
# Bundles a built VCDPlayer.exe (MSYS2 mingw/clang build) into a
# self-contained, portable folder: every non-system DLL it depends on
# (transitively) plus the Qt platform plugin, since neither ships in a
# default Windows install. Must run inside the MSYS2 shell that built the
# exe (relies on $MSYSTEM_PREFIX and its `objdump`).
set -euo pipefail

exe="$1"       # path to the built executable, e.g. build/VCDPlayer.exe
out_name="$2"  # archive/folder base name, e.g. VCDPlayer-windows-x64-portable

prefix="${MSYSTEM_PREFIX:?run this inside an MSYS2 shell (MSYSTEM_PREFIX unset)}"
dist_dir="dist/${out_name}"

rm -rf dist
mkdir -p "$dist_dir"
cp "$exe" "$dist_dir/"

# Bundled GLSL user shaders (see shaders/README.md): CMake copies them next
# to the built exe, so they're already sitting right beside it here too.
shaders_dir="$(dirname "$exe")/shaders"
[ -d "$shaders_dir" ] && cp -r "$shaders_dir" "$dist_dir/"

# Deliberately not `ldd`: MSYS2's ldd resolves a native (non-MSYS) PE
# executable's imports by actually running it, which for a Qt6 GUI app
# means it launches the app and blocks on its event loop instead of
# returning. `objdump -p` reads the PE import table statically instead.
direct_deps() {
  objdump -p "$1" 2>/dev/null | awk '/DLL Name:/ { print $3 }'
}

declare -A seen
queue=("$dist_dir/$(basename "$exe")")
while [ "${#queue[@]}" -gt 0 ]; do
  bin="${queue[0]}"
  queue=("${queue[@]:1}")
  for name in $(direct_deps "$bin"); do
    src="$prefix/bin/$name"
    [ -f "$src" ] || continue  # a Windows/system DLL, not ours to bundle
    if [ -z "${seen[$name]+x}" ]; then
      seen[$name]=1
      cp -n "$src" "$dist_dir/"
      queue+=("$dist_dir/$name")
    fi
  done
done

# Without its platform plugin Qt can't create a window at all ("no Qt
# platform plugin could be initialized"); it isn't picked up by ldd because
# Qt loads it at runtime, not at link time.
platform_plugin="$(find "$prefix" -iname 'qwindows.dll' | head -n1)"
[ -n "$platform_plugin" ] || { echo "qwindows.dll not found under $prefix" >&2; exit 1; }
mkdir -p "$dist_dir/platforms"
cp "$platform_plugin" "$dist_dir/platforms/"

# Native-looking widgets instead of the generic fallback style; skip
# silently if this MSYS2 Qt6 package build doesn't ship it.
style_plugin="$(find "$prefix" -iname 'qwindowsvistastyle.dll' | head -n1)"
if [ -n "$style_plugin" ]; then
  mkdir -p "$dist_dir/styles"
  cp "$style_plugin" "$dist_dir/styles/"
fi

(cd dist && zip -r "${out_name}.zip" "$(basename "$dist_dir")")
echo "Wrote dist/${out_name}.zip"
