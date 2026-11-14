# Bundled shaders

Real-time GPU upscaling shaders for mpv ("user shaders"/hooks), used by the
**Experimental > GPU upscaling** presets (`UpscalerPresets()` in
`src/ui/MainWindow.cpp`), which point mpv's `glsl-shaders` option at them.
They only take effect with the Hardware (OpenGL) render backend; mpv's
software render backend doesn't run the GPU shader pipeline at all.

Every file is fetched verbatim from its upstream source at the version
below — none is modified.

| Preset | File(s) | Upstream | Version | License |
|---|---|---|---|---|
| Anime4K — cartoons | `Anime4K_Clamp_Highlights.glsl`, `Anime4K_Restore_CNN_M.glsl`, `Anime4K_Upscale_CNN_x2_M.glsl` | [bloc97/Anime4K](https://github.com/bloc97/Anime4K) `glsl/` | tag `v4.0.1` | MIT, see `Anime4K-LICENSE.txt` |
| FSRCNNX — live action | `FSRCNNX_x2_8-0-4-1.glsl` | [igv/FSRCNN-TensorFlow](https://github.com/igv/FSRCNN-TensorFlow) | release `1.1` | LGPL-3.0-or-later (file header), see `LGPL-3.0.txt` |
| RAVU — general purpose | `ravu-lite-ar-r3.hook` | [bjin/mpv-prescalers](https://github.com/bjin/mpv-prescalers) | commit `b3f0a59` | LGPL-3.0-or-later (file header), see `LGPL-3.0.txt` |
| AMD FSR — fast | `FSR.glsl` (FidelityFX Super Resolution 1.0.2 for mpv) | [agyild's gist](https://gist.github.com/agyild/82219c545228d70c5604f865ce0b0ce5) | revision `2623d74` | MIT (AMD), in the file header |
| AMD CAS — sharpening only | `CAS-scaled.glsl` (FidelityFX Contrast Adaptive Sharpening 1.0.2 for mpv) | [agyild's gist](https://gist.github.com/agyild/bbb4e58298b2f86aa24da3032a0d2ee6) | revision `10e4ca1` | MIT (AMD), in the file header |

All of these are compatible with VCD Player's GPLv3-or-later license.

The Anime4K chain is the "Mode A" restore-then-upscale pipeline minus its
auto-downscale steps: the *Restore* pass is what removes the MPEG-1 blocking
and ringing typical of Video CDs, before the x2 upscale.

## Tried and left out

libmpv's render API (which the app uses to draw into its own widget) runs
mpv's classic `gpu` renderer, not `gpu-next`, so shaders written only for
`gpu-next`/libplacebo don't work here:

- **ArtCNN** ([Artoriuz/ArtCNN](https://github.com/Artoriuz/ArtCNN), MIT):
  makes the classic renderer abort with an assertion.
- **NVIDIA Image Scaling** ([agyild's gist](https://gist.github.com/agyild/7e8951915b2bf24526a9343d951db214), MIT):
  uses `//!PARAM`, which only `gpu-next` parses, so mpv rejects the file.

## Packaging

This directory is copied next to the built executable (see the
`add_custom_command` in `CMakeLists.txt`) and must ship alongside it — see
`ci/package-windows.sh` / `ci/package-appimage.sh`.
