# Nintendo 3DS platform guide

This is Godot 4.7 for the Nintendo 3DS: an export template built with
devkitARM, libctru and citro3d, plus an editor that can export for it. Your
game is GDScript and scenes, the same as on any other platform. What's
different is the hardware, so this guide covers the 3DS specifically:

- two screens, one of which is a second window with touch input
- stereoscopic 3D on the top screen, and 2D depth
- the console's input (buttons, circle pad, touch, motion)
- the renderer, and what the PICA200 graphics chip can and can't do
- audio, files, networking, threads, and the memory budget
- building, exporting (`.3dsx` and `.cia`), and debugging

Start with [Quick start](#quick-start). The rest is reference, in roughly the
order you'll need it.

## Contents

- [Quick start](#quick-start)
- [Screens and windows (dual screen)](#screens-and-windows-dual-screen)
- [Stereoscopic 3D](#stereoscopic-3d)
- [2D depth](#2d-depth)
- [Input](#input)
- [The `Nintendo3DS` singleton](#the-nintendo3ds-singleton)
- [Renderer](#renderer)
- [Custom spatial shaders](#custom-spatial-shaders)
- [Textures](#textures)
- [Audio](#audio)
- [Files, saves and paths](#files-saves-and-paths)
- [Networking](#networking)
- [Threads](#threads)
- [Memory and the original 3DS](#memory-and-the-original-3ds)
- [Building the template](#building-the-template)
- [Exporting from the editor](#exporting-from-the-editor)
- [Running, logs and debugging](#running-logs-and-debugging)
- [Limitations](#limitations)
- [Class reference](#class-reference)
- [Porting notes](#porting-notes)

## Quick start

1. Build the editor and the templates (see [Building the template](#building-the-template)).
2. In the editor: **Project > Export > Add > Nintendo 3DS**. Export to a file
   ending in `.3dsx` (runs from the Homebrew Launcher) or `.cia` (installs with FBI).
3. Copy the file to the console: a `.3dsx` goes to `sdmc:/3ds/`, and you start
   it from the Homebrew Launcher. A `.cia` is installed with FBI.

Set the two project settings that every 3DS game needs, described in
[Screens and windows](#screens-and-windows-dual-screen):

```
window/size/viewport_width=400
window/size/viewport_height=240
window/subwindows/embed_subwindows=false
```

## Screens and windows (dual screen)

The 3DS has two screens, and Godot treats them as two windows:

- The **top screen** (400 x 240) is the main window. Its viewport is your
  game's 3D and 2D scene, and it renders one image per eye in stereo.
- The **bottom screen** (320 x 240) is any other `Window` you add. It shows
  whatever you put in it, and it receives the touch screen.

Three things make it work:

1. **Set the main viewport to 400 x 240** (`window/size/viewport_width` and
   `viewport_height`). The top screen's size is fixed, so the game should fill
   it.
2. **Turn off embedded sub-windows** (`window/subwindows/embed_subwindows=false`).
   Without this, a `Window` is drawn inside the main window, not on the
   other screen.
3. **Move the window to the bottom screen** by setting `current_screen = 1` on it.
   Screen 0 is the top screen (the default), and screen 1 is the bottom one.

### Worked example: a bottom-screen menu

The scene tree:

```
Main (Node3D or Node2D, your game)
├── Camera3D, Lights, World...
└── Bottom (Window, size 320x240, borderless)
    ├── Background (ColorRect, full size)
    ├── Title (Label)
    └── Buttons (VBoxContainer with Button children)
```

In `main.gd`, on the main node:

```gdscript
extends Node3D

@onready var bottom: Window = $Bottom

func _ready() -> void:
	bottom.current_screen = 1          # put the window on the bottom screen
	bottom.borderless = true           # no title bar; it's a screen, not a window
	($Bottom/Buttons/Start as Button).pressed.connect(_on_start)

func _on_start() -> void:
	print("Start pressed")
```

Things to know about the bottom window:

- It's a normal `Window`, so anything you can put in a window works in it:
  Controls, Labels, Buttons, sprites, a second `SubViewport`.
- Its contents are **2D**. It doesn't see your 3D world unless you render the
  world into a `SubViewport` and show that in a `TextureRect`.
- **Touch** goes to the bottom window as `InputEventScreenTouch` and
  `InputEventScreenDrag`, and also as emulated mouse events, so Buttons,
  sliders and text fields respond to the stylus with no extra code.
- If you don't create a bottom window, touches go to the top window.
- **Hiding it** is `bottom.visible = false`. The top screen then fills its own
  screen as usual.

The platformer uses this pattern for its HUD (`scenes/hud.tscn` is a
borderless `Window`, and `level.gd` sets `current_screen = 1`). The launcher
(a separate project) uses it for its game list.

### Showing things on both screens

Anything on the top screen is drawn by the main window. To show a copy on the
bottom screen too, render it into a `SubViewport` and display that
`ViewportTexture` in the bottom window's `TextureRect`. It costs a second
render, so keep it small.

### Checking what's on screen

`Nintendo3DS.save_screens(dir)` writes `top_left.png`, `top_right.png` (the two
eyes) and `bottom.png` into `dir`. Use it with `OS.get_user_data_dir()` and
look at the files to check a layout without a camera or a real console.

## Stereoscopic 3D

The 3D slider on the console's right side controls depth. The top screen
renders one image for each eye whenever the slider is up, and one image when
it's down.

- **Cameras.** Each `Camera3D` is offset sideways per eye, and its frustum is
  sheared so that objects at the convergence distance sit on the screen
  plane. Objects nearer than that come out of the screen, and farther ones
  go into it.
- **Project settings:**
  - `rendering/3ds/stereo/max_separation` (default `0.12`): the eye separation
    in world units at full slider.
  - `rendering/3ds/stereo/convergence` (default `3.0`): the distance from the
    camera with zero parallax.
- **Per scene.** `Nintendo3DS.set_stereo_separation(x)` and
  `Nintendo3DS.set_stereo_convergence(x)` override the project settings for
  the scene you're in. Pass a negative value to go back to the settings.
- **Off the slider.** At `0.0` the scene draws once, with no depth, and costs
  less. Test your game with the slider down too.

2D drawing is flat: 2D canvas items sit on the screen plane. To move them,
see the next section.

## 2D depth

Any `CanvasItem` (a Control, a Sprite2D, a Label, or a whole HUD) can sit in
front of or behind the screen plane. Depth is measured in **pixels of
disparity at full slider**: positive pops out, negative sinks in, and `0.0` is
the screen plane. Each eye sees the item shifted sideways by half the depth,
times the slider, rounded to whole pixels so text and sprites stay sharp. Keep
depths small (about `-6` to `+6`); big shifts are hard to look at.

```gdscript
var n3ds = Engine.get_singleton("Nintendo3DS")
n3ds.set_depth($Hud, 3.0)           # the HUD and everything under it
n3ds.set_depth($Sky, -5.0, false)   # only this item
print(n3ds.get_depth($Hud))         # 3.0
```

How it works: `set_depth` gives the item a `ShaderMaterial` with a
`stereo_depth` parameter. Children without a material of their own use that
material (`use_parent_material`), so a whole subtree shares the depth. A child
with its own material keeps it and sits at depth `0`, unless you call
`set_depth` on it too. A clipped item's clip rectangle moves with it.

**Warning:** `set_depth` replaces a material the item already has (it prints a
warning when it does). If the item has its own material, add a
`stereo_depth` uniform to that material yourself instead.

### In the editor

Select any `CanvasItem`. The Inspector has a **Stereo Depth** row (a slider in
pixels). It sets the same value as `set_depth`, it's undoable, and it's saved
with the scene. The row is read-only when the item inherits its parent's
material (set the depth on the parent instead), or has a material of its own.

### By hand

Assign a `ShaderMaterial` with this shader to any canvas item, and set the
`stereo_depth` value (you can animate it or tween it):

```glsl
shader_type canvas_item;
uniform float stereo_depth = 0.0;
void fragment() {}
```

## Input

The 3DS is a joypad (device 0), mapped by position like every other Godot
platform:

| 3DS | Godot |
| --- | --- |
| B (bottom) | `JOY_BUTTON_A` |
| A (right) | `JOY_BUTTON_B` |
| Y (left) | `JOY_BUTTON_X` |
| X (top) | `JOY_BUTTON_Y` |
| L / R | shoulders (`JOY_BUTTON_LEFT_SHOULDER` / `RIGHT_SHOULDER`) |
| ZL / ZR (New 3DS) | trigger axes |
| Circle pad / C-stick | left / right stick |
| START / SELECT | start / back |

So `Input.is_action_pressed("jump")` works with actions mapped to joypad
button 0 (B), and a game written for a gamepad works on the 3DS unchanged.

- **Touch:** on the bottom window (see above). Read it with `_gui_input` or
  `InputEventScreenTouch` on that window.
- **Motion:** `Input.get_accelerometer()` and `Input.get_gyroscope()` give the
  console's motion sensors.
- **Keyboard:** the system keyboard opens for text fields through
  `DisplayServer.virtual_keyboard_show`.

## The `Nintendo3DS` singleton

Available from scripts on the 3DS. The stock Godot editor doesn't know the
class, so reach it through `Engine`, which keeps the script parsing there too:

```gdscript
var n3ds = Engine.get_singleton("Nintendo3DS") if Engine.has_singleton("Nintendo3DS") else null
```

| Method | Returns / does |
| --- | --- |
| `get_3d_slider()` | 3D slider position, `0.0` to `1.0` |
| `is_new_3ds()` | `true` on a New 3DS or 2DS XL |
| `get_battery_level()` | `0` (empty) to `5` (full) |
| `is_charging()` | `true` while charging |
| `set_stereo_separation(x)` / `set_stereo_convergence(x)` | per-scene stereo overrides; negative restores the settings |
| `set_depth(item, depth, recursive = true)` | 2D depth, see above |
| `get_depth(item)` | the depth set on an item |
| `save_screens(dir)` | writes `top_left.png`, `top_right.png`, `bottom.png` |
| `get_audio_stats()` | `{active, buffers_played, last_peak}` from the audio driver |

Feature flags work with `OS.has_feature`: `"3ds"` and `"new_3ds"`.

Use `is_new_3ds()` to scale a game down on the original console: fewer
particles, shadows off, lower resolution textures.

## Renderer

The PICA200 has programmable vertex shaders but a fixed-function fragment
stage. The renderer maps Godot's built-in materials onto that fixed stage, so
most of what a game uses works, and a few features don't exist.

### 3D

- **Materials.** `StandardMaterial3D` and `ORMMaterial3D`: albedo color and
  texture, UV1 scale and offset, vertex color, unshaded, emission,
  roughness, metallic and specular (Blinn-Phong highlights), blend modes,
  cull modes, depth draw and test, alpha scissor, and texture filter and
  repeat.
- **Lights.** Up to 8 per scene: directional, omni and spot, with Godot's
  distance falloff, plus the environment's ambient light.
- **Shadows.** From the first `DirectionalLight3D` with shadows on (or else the
  first shadowed `SpotLight3D`). The shadow map is orthographic around the
  camera, out to `rendering/3ds/shadows/max_distance` (default 20), at
  `rendering/3ds/shadows/size` (default 512). Depth is 8 bits, so
  `rendering/3ds/shadows/bias` (default `0.012`) trades acne against
  peter-panning. Only the one light is shadowed.
- **Environment.** The background can be a clear color, a color, or a sky.
  Procedural, panorama and physical skies are approximated, with a sun disc
  from the directional light in the procedural and physical ones. The
  ambient light can take the sky's color. **Fog** works in exponential and
  depth modes.
- **Meshes.** `MultiMesh` (with per-instance color), `CPUParticles3D`,
  skeletons and blend shapes. Skinning runs on the CPU, because the PICA200
  has room for only about 28 bone matrices.

### 2D

- Rects, nine-patches, polygons, primitives, text, clipping, `CanvasModulate`,
  `CanvasItemMaterial` blend modes, filter and repeat settings,
  `MeshInstance2D`, and 2D `MultiMesh`.
- **2D lights.** `PointLight2D` (with a texture) and `DirectionalLight2D`,
  with add and subtract blend modes, light masks and z ranges, and energy up
  to 4. Each lit item draws one extra additive pass per light.
- **Not in 2D:** 2D shadows, normal maps, and 2D skeletons.

### Not available

- **GPU particles** (`GPUParticles2D` and `3D`) need compute shaders. Use
  `CPUParticles2D` or `CPUParticles3D` instead.
- **Reflection probes**, and most post-processing.
- **Clouds** in the procedural sky.
- **Omni-light shadows** (they'd need six shadow passes per light).
- **Custom `canvas_item` shaders** draw with the default 2D pipeline. See the
  next section for spatial shaders.

## Custom spatial shaders

The PICA200 has no fragment shaders, so a `spatial` `.gdshader` is translated
when it loads. The translation supports a small subset:

- `ALBEDO`, `ALPHA` and `EMISSION` set to a product (`*`) of constants,
  uniforms (`float`, `vec3`, `vec4`, from the material or the code's
  defaults), `COLOR` (vertex color, for `ALBEDO`), and at most one
  `texture(sampler, uv)` call, with swizzles like `.rgb` or `.a`.
- Texture coordinates of the form `UV`, `UV * c`, `UV + c`, or
  `UV + TIME * c` (scrolling).
- `ROUGHNESS`, `METALLIC` and `SPECULAR`, set to a constant or a uniform.
- Render modes as in `StandardMaterial3D` (unshaded, blend, cull, depth), and
  the sampler's `filter_*` and `repeat_*` hints.

Example:

```glsl
shader_type spatial;
uniform vec4 tint : source_color = vec4(1.0, 0.5, 0.2, 1.0);
uniform sampler2D tex : source_color, filter_nearest;
void fragment() {
	ALBEDO = tint.rgb * texture(tex, UV * 2.0 + TIME * vec2(0.1, 0.0)).rgb;
}
```

Anything outside the subset (other math, function calls, `if`, local
variables, a `vertex()` or `light()` function) prints a warning that names the
shader and the reason. The material then draws unshaded, using its
`albedo`/`color` and `texture_albedo`/`texture` parameters, so the object is
still visible.

## Textures

- **Formats.** Lossless imports (WebP), Lossy, and VRAM Compressed all load.
  VRAM Compressed textures are exported as S3TC and decompressed on the 3DS
  when they load (with `bcdec`). Lossless and Lossy load faster.
- **Size.** Textures larger than 1024 x 1024 are downscaled on load. Keep
  them at 512 or below where you can; the 3DS has little VRAM.
- **Mipmaps.** Textures imported with mipmaps (the 3D default) get a full
  chain, built on the CPU.
- **16-bit storage** on the original console. See
  [Memory](#memory-and-the-original-3ds).

## Audio

The audio driver (ndsp) plays at 32 kHz stereo, with about 96 ms buffered in
six 512-frame blocks, mixed on its own thread so a slow frame doesn't
underrun it. Everything Godot plays through the audio server goes through
this driver.

- **Firmware.** On real hardware the DSP needs its firmware dump at
  `sdmc:/3ds/dspfirm.cdc`, made with the DSP1 homebrew. Without it the game
  runs silently and writes an error to the log. The Azahar emulator accepts a
  placeholder file for testing.
- **Checking audio.** `get_audio_stats()` returns whether the driver is active,
  how many buffers it has played, and the loudest sample of the last buffer.
- **Microphone:** not supported.

## Files, saves and paths

| Godot path | On the console |
| --- | --- |
| `res://` | inside the `.3dsx`'s RomFS (read-only) |
| `user://` | `sdmc:/3ds/godot/app_userdata/<project name>/` |
| `sdmc:/...` | the SD card, direct (for example `sdmc:/3ds/godot/launcher/`) |

Use `user://` for saves and settings. The project name comes from the project
settings. Saves persist across runs and across updates of the same game, as
long as the name doesn't change.

Godot's output (`print`, errors) goes to `sdmc:/3ds/godot/stdout.txt`, which is
the first place to look when something goes wrong.

Command-line arguments come from two places: the Homebrew Launcher, and
`sdmc:/3ds/godot/args.txt` (one argument per line). Debug templates use this
for remote debugging. The game can read its own arguments with
`OS.get_cmdline_user_args()`.

## Networking

- **Sockets:** IPv4 TCP and UDP over Wi-Fi, through `HTTPRequest`,
  `StreamPeerTCP`, and `PacketPeerUDP`. The `soc:u` service takes 1 MB of
  memory.
- **IPv4 only.** Don't rely on IPv6, and have servers bind IPv4 addresses.
- **No TLS in the template.** Plain HTTP and TCP only, so anything sensitive
  travels unencrypted. Don't send passwords or secrets from a 3DS game.
- **Optional modules.** ENet and WebSocket work only if their modules are
  enabled in the template (they aren't by default; the template is kept
  small).
- **HTTP requests have one total timeout.** `HTTPRequest.timeout` covers the
  whole request, so a large download over a slow link can time out. Fetch big
  files in ranges (`Range:` headers) and resume, as the launcher does.

## Threads

Godot's worker threads run on the 3DS, at a priority just below the main
thread's. The 3DS kernel doesn't time-slice equal priorities, so a worker
gets time when the main thread waits for vsync. Heavy background work
(loading, procedural generation) should run on a worker and report back to the
main thread. Long jobs on the main thread will drop frames.

- Each worker thread gets a 512 KiB stack (the libctru default is far too small
  for Godot's code). The main thread has 1 MiB.
- Audio mixes on its own thread, on the second core of the New 3DS.

## Memory and the original 3DS

The original 3DS gives a game about **64 MB**, and the New 3DS gives **124 MB**.
The code and read-only data of the template take about 20 MB, so the original
has about 44 MB free for a game. Its CPU runs at 268 MHz, a third of the New
3DS's 804 MHz. The GPU is the same on both.

What helps on the original:

- **A smaller template.** `platform/3ds/tools/make_profile.py` scans a project
  (scenes, scripts, imported models) and writes a build profile that leaves
  out the node classes the project never uses. The platformer's template went
  from 20.5 to 17.8 MB this way.

  ```sh
  python3 platform/3ds/tools/make_profile.py ~/game --godot bin/godot.linuxbsd.editor.x86_64 -o bin/profiles/game.json
  platform/3ds/build.sh target=template_release build_profile=bin/profiles/game.json
  ```

  The profile must sit inside the Godot folder, because the build runs in
  Docker. A class the scan can't see (one built from a string, say) goes in
  `--keep`. The game prints `Cannot get class` if one is missing. Several
  games can share a template with `--also-project`.
- **16-bit textures.** `rendering/3ds/textures/format` is `Auto` by default:
  textures are stored as RGB565, RGBA5551 or RGBA4444 on the original console,
  and as full RGBA8 on the New 3DS. `16-bit` and `Full` force one. This halves
  texture memory, with dithering. `Image` readback returns RGBA8 either way.
- **Rehearsing the budget.** Build with `app_memory_limit_mb=40` to cap the
  heap and linear heap at 40 MB, even on a New 3DS or in an emulator. The
  platformer runs normally at 40 MB and at 28 MB, and fails at 22 MB. Emulator
  CPU timing isn't accurate, so test speed on hardware.
- **Scale down** with `Nintendo3DS.is_new_3ds()`.

The CIA template (`platform/3ds/cia/template.rsf`) asks for 64 MB, which the
original console grants, and 124 MB at 804 MHz on the New 3DS.

## Building the template

Builds run in Docker (devkitARM and SCons). The first run builds the image.

```sh
platform/3ds/build.sh target=template_release   # or template_debug
```

Output: `bin/godot.3ds.<target>.arm32.elf` and `.3dsx`. The defaults are full
LTO and `-Oz` (the release template is about 20.5 MB). Add `lto=none` while you
iterate; the build is much faster.

Options (pass them to `build.sh`):

| Option | Meaning |
| --- | --- |
| `lto=none` | skip link-time optimization (faster builds, bigger binary) |
| `linear_heap_mb=N` | fix the GPU-visible heap at N MiB (default: 30% of free memory, 8 to 32 MiB) |
| `app_memory_limit_mb=N` | cap heap plus linear heap at N MiB, to rehearse the original console |
| `build_profile=PATH` | leave out unused node classes (see [Memory](#memory-and-the-original-3ds)) |

To build an editor with the 3DS platform, from this repository:

```sh
python3 -m venv .venv && .venv/bin/pip install scons
.venv/bin/scons platform=linuxbsd target=editor -j$(nproc)
```

Then install the templates so the editor can export with them:

```sh
platform/3ds/install_templates.sh
```

`install_templates.sh` copies `bin/godot.3ds.template_*` to
`~/.local/share/godot/export_templates/<version>/3ds_{debug,release}.{3dsx,elf}`
(the `.elf` is used for CIAs). Pass a folder to install somewhere else, for
example `bin/editor_data/export_templates` for a self-contained editor.

## Exporting from the editor

Project > Export > Add > **Nintendo 3DS**, then export to a file ending in
`.3dsx` or `.cia`.

- **`.3dsx`** is written directly: the template, a title and icon (an SMDH
  block), and a RomFS holding the project's `.pck`. No other tools are needed.
- **`.cia`** also needs **makerom** and **bannertool**. Set their paths in
  Editor Settings > Export > 3DS. The CIA preset also takes a banner image and
  sound, a unique id and a product code (the defaults are the icon on a dark
  plate, silence, and an id derived from the title).

Preset options:

| Option | Meaning |
| --- | --- |
| `application/title` | the name shown in the launcher and on HOME |
| `application/description` | a short description (SMDH) |
| `application/author` | the publisher line (SMDH) |
| `application/icon` | the icon, 48 x 48 (defaults to the project icon) |
| `cia/banner` | the HOME-menu banner, 256 x 128 (CIA only) |
| `cia/banner_audio` | the banner's sound (CIA only) |

Editor Settings under `export/3ds` also hold the console address, for **Run on
3DS** (see below).

Exporting headless works too:

```sh
godot --headless --path <project> --export-release "3DS" game.3dsx
```

### From the command line, without the editor

1. Export the project as a pack: `godot --headless --path <project> --export-pack <preset> game.pck`.
2. Bundle it with the template: `platform/3ds/make_game.sh --title "My Game" game.pck game.3dsx`,
   or `... game.pck game.cia` (add `--banner`, `--author`, `--id`). With no
   arguments it prints all its options.

## Running, logs and debugging

- **Run it.** Put the `.3dsx` on the SD card under `sdmc:/3ds/` and start it
  from the Homebrew Launcher, or install the `.cia` with FBI.
- **Logs.** `sdmc:/3ds/godot/stdout.txt`. Read it with the SD card in a
  computer, or over FTP.
- **Remote debugging.** Debug templates honor `--remote-debug tcp://<pc>:6007`
  and other Godot arguments, from the launcher or from `args.txt`.
- **Run on 3DS from the editor.** Set Editor Settings > Export > 3DS >
  **3dslink** (the devkitPro tool) and optionally the **Console Address**. A
  "Run on 3DS" button appears for the 3DS preset. Press Y in the Homebrew
  Launcher, click the button, and the game starts with the editor's debugger,
  output and remote scene tree connected.
- **Crash addresses.** A Luma crash screen shows the program counter (PC) and
  link register (LR). Resolve them against the release ELF with
  `arm-none-eabi-addr2line -e godot.3ds.template_release.arm32.elf <address>`.
  Use the debug template for useful symbols.
- **Screens.** `Nintendo3DS.save_screens(dir)` to check a layout without a
  console camera.

## Limitations

- **Hardware:** no microphone input. No IPv6. No TLS. No GDExtension (no
  dynamic loading).
- **Graphics:** no GPU particles, no reflection probes, no post-processing, no
  2D shadows or normal maps, no omni-light shadows. See the
  [renderer](#renderer) section.
- **Shaders:** only the subset in [Custom spatial shaders](#custom-spatial-shaders).
- **Textures:** 1024 x 1024 maximum.
- **Audio:** needs `dspfirm.cdc` on real hardware (see [Audio](#audio)).
- **Networking:** IPv4 only; plain text on the wire.
- **Testing:** the Azahar emulator is a good check, but it doesn't model the
  original console's speed, and it hides some crashes that real hardware
  reports. Test on a console before release.

## Class reference

The editor's Help has entries for the classes this platform adds:
`Nintendo3DS` and `EditorExportPlatform3DS`. The pages are built from the XML
in `doc_classes/`, and the same pages are in the documentation website in
`docs-site/` (see its README).

## Porting notes

For people working on the template itself.

- `int32_t` is `long` on arm-none-eabi. `detect.py` redefines the 32-bit
  fixed-width types as `int`, so Godot's `Variant` overloads stay distinct.
- Godot adds `-s` at link time, which breaks `3dsx.specs` (it needs
  `--emit-relocs`). The SCsub removes it.
- The PICA200 samples texture row `v = 0` at the end of the texture data, and
  render targets are drawn the same way when the projection is y-flipped. So
  every texture stores Godot's top row last (see `rendering/citro3d_common.h`).
- citro3d queues clears to the GPU at once, but draws only at frame end. A
  clear in the middle of a frame has to split the frame first
  (`C3D_FrameSplit`).
- citro3d can't create or delete render targets inside a frame. Resizes and
  frees are deferred to the next frame.
- Never pass a null texture to `C3D_TexBind`. citro3d dereferences it, and the
  console data-aborts, while Azahar tolerates it.
- The Stereo Depth inspector row and the `Nintendo3DS` class are compiled into
  the editor too, through `platform/3ds/export/` and `platform/3ds/api/`.
