# Bundled shaders

`Anime4K_Upscale_CNN_x2_M.glsl` is the "Upscale (M)" pass from
[bloc97/Anime4K](https://github.com/bloc97/Anime4K) (MIT license, see
`Anime4K-LICENSE.txt`), fetched verbatim from the upstream repository —
not modified. It's a real-time GPU shader (an mpv "user shader"/hook, not
a heavyweight neural-network model), which is what makes it usable during
live playback instead of only on a single paused frame.

Used by the Experimental menu's "AI upscaling" toggle (`MainWindow.cpp`),
which points mpv's `glsl-shaders` option at this file. It only has any
effect when the Hardware (OpenGL) render backend is active — mpv's
software render backend doesn't run the GPU shader pipeline this hooks
into at all (see `MpvPlayer::initRenderContext()`).

This directory is copied next to the built executable (see the
`add_custom_command` in `CMakeLists.txt`) and must ship alongside it —
see `ci/package-windows.sh` / `ci/package-appimage.sh`.
