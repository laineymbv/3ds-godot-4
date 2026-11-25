# Godot for the Nintendo 3DS

A fork of [Godot Engine](https://godotengine.org) 4.7.2 that runs on the
Nintendo 3DS and New Nintendo 3DS, and renders stereoscopic 3D on the top
screen. Write a game in GDScript and scenes, export it, and run it on the
console from the Homebrew Launcher or install it as a CIA.

- **Documentation:** https://laineymbv.github.io/3ds-godot-4/ (the same pages
  are in [`docs/`](docs/index.html) and build from [`docs-site/`](docs-site/README.md))
- **Platform guide:** [`platform/3ds/README.md`](platform/3ds/README.md)
- **Original Godot README:** [`README-godot.md`](README-godot.md)

## What works

- **Dual screen.** The top screen is the main window, and any `Window` you add
  can sit on the bottom screen and take touch input.
- **Stereo 3D.** Each `Camera3D` renders one image per eye, scaled by the 3D
  slider, and 2D items can sit in front of or behind the screen.
- **Renderer.** The PICA200's fixed-function fragment stage is mapped to
  Godot's built-in materials: lights, shadows, skies, fog, mipmaps, MultiMesh,
  CPU particles, skeletons, and 2D lights.
- **Custom shaders.** A subset of spatial shaders is translated at load time.
  Anything outside it falls back to unshaded drawing, with a warning.
- **Audio, input, files and networking.** Sound through ndsp, the full joypad
  mapping, touch, motion sensors, `user://` saves on the SD card, and IPv4
  sockets over Wi-Fi.
- **Editor.** A **Nintendo 3DS** export platform (`.3dsx` and `.cia`), a
  "Run on 3DS" button, and a **Stereo Depth** row in the Inspector.
- **Original 3DS.** A build profile that drops unused classes, 16-bit textures,
  and a memory cap for testing against the original's 64 MB.

## Status

- Tested in the Azahar emulator and on a New 3DS, where the showcase and
  platformer run at 60 fps.
- The original 3DS is supported but has only been tested in the emulator's
  original-console mode, which doesn't model its speed.
- Some features aren't implemented: GPU particles, reflection probes,
  post-processing, omni-light shadows, and 2D shadows. The guide lists them.

## Building

You need Docker (for the devkitARM toolchain) and Python 3 with `scons`.

```sh
# 3DS export templates (in Docker)
platform/3ds/build.sh target=template_release
platform/3ds/build.sh target=template_debug

# Editor with the 3DS export platform (on your PC)
python3 -m venv .venv && .venv/bin/pip install scons
.venv/bin/scons platform=linuxbsd target=editor -j$(nproc)

# Make the templates available to the editor
platform/3ds/install_templates.sh
```

Then, in the editor: **Project > Export > Add > Nintendo 3DS**, and export to
a `.3dsx` or `.cia` file. The full steps, options and troubleshooting are in
[the platform guide](platform/3ds/README.md).

## A first project

Set these project settings so the top and bottom screens work:

```
window/size/viewport_width=400
window/size/viewport_height=240
window/subwindows/embed_subwindows=false
```

A short example of a bottom-screen window is in the
[dual screen section](platform/3ds/README.md#screens-and-windows-dual-screen).

## License

This fork is covered by the same license as Godot Engine: the MIT License.
See [`LICENSE.txt`](LICENSE.txt) and [`COPYRIGHT.txt`](COPYRIGHT.txt). The
3DS platform code is by the fork's author on top of the Godot contributors'
work; see [`AUTHORS.md`](AUTHORS.md) for everyone else.
