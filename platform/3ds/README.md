# Nintendo 3DS platform

Godot 4.7 export template for the Nintendo 3DS (homebrew: devkitARM, libctru,
citro3d). The engine runs GDScript projects from a `.pck` stored in the
`.3dsx` RomFS, renders 3D with stereoscopic output on the top screen, and uses
the bottom screen as a second window with touch input.

## Building

Builds run in Docker (devkitARM + SCons):

```sh
platform/3ds/build.sh target=template_release   # or template_debug
```

Output: `bin/godot.3ds.<target>.arm32.elf` and `.3dsx`. Builds use
full LTO and `-Oz` by default (release template about 20.5 MB). Add `lto=none` for faster iteration.

Options (pass to `build.sh`):

- `linear_heap_mb=N` fixes the GPU-visible heap (textures, meshes, audio).
  By default it is 30% of the memory free at start, 8-32 MiB, so the same
  build fits a 64 MB Homebrew Launcher slot and a 124 MB New 3DS CIA.

## Making a game

### From the editor (recommended)

Build an editor from this fork once (it adds the **Nintendo 3DS** export
platform and knows the `Nintendo3DS` class):

```sh
python3 -m venv .venv && .venv/bin/pip install scons
.venv/bin/scons platform=linuxbsd target=editor -j$(nproc)
platform/3ds/build.sh target=template_release
platform/3ds/build.sh target=template_debug
platform/3ds/install_templates.sh
```

Then in the editor: Project > Export > Add > Nintendo 3DS, and export to a
`.3dsx` or `.cia` file name.

- `.3dsx` is written directly (template code + SMDH title/icon + a RomFS with
  the project's `.pck`); no external tools.
- `.cia` needs makerom and bannertool: set their paths in Editor Settings >
  Export > 3DS. Banner image/sound, unique id and product code are preset
  options (defaults: the icon on a dark plate, silence, derived from the
  title).
- Preset options: title, description, author, icon (defaults to the project
  icon), custom templates.
- Lossless (WebP), Lossy and VRAM Compressed texture imports all load.
- Textures imported as VRAM Compressed export as S3TC and are decompressed on
  the 3DS at load (`bcdec`); Lossless/Lossy import is faster to load.

`install_templates.sh` copies `bin/godot.3ds.template_*` to
`~/.local/share/godot/export_templates/<version>/3ds_{debug,release}.{3dsx,elf}`
(the `.elf` is used for CIAs). Pass a directory to install elsewhere, e.g.
`bin/editor_data/export_templates` for a self-contained editor (`bin/._sc_`).

### From the command line

1. Export the project as a pack (`.pck`) with any desktop preset, or headless:
   `godot --headless --path <project> --export-pack <preset> game.pck`.
2. Bundle it with the template:
   `platform/3ds/make_game.sh [--title T] [--icon icon.png] game.pck game.3dsx`
   or `... game.pck game.cia` (also `--banner`, `--author`, `--id`; needs
   makerom and bannertool). Run it without arguments for all options.

With the fork's editor, `--export-release "<3DS preset>" game.3dsx` works
headless too.

### Running

Copy a `.3dsx` to `sdmc:/3ds/` and start it from the Homebrew Launcher, or
install a `.cia` with FBI.

A `.3dsx` gets whatever memory the launcher has (usually 64 MB). A `.cia`
runs in 64 MB on the original 3DS and in 124 MB at 804 MHz on the New 3DS.
The main thread gets a 1 MB stack.

Godot's output goes to `sdmc:/3ds/godot/stdout.txt`; `user://` is
`sdmc:/3ds/godot/app_userdata/<project name>/`.

## Remote debugging and one-click run

Debug templates honor `--remote-debug tcp://<pc>:6007` and other Godot
arguments, taken from the launcher (3dslink) and from
`sdmc:/3ds/godot/args.txt` (one argument per line).

In the fork's editor, set Editor Settings > Export > 3DS > 3dslink (the
devkitPro tool) and optionally Console Address. A "Run on 3DS" button then
appears for the 3DS preset: press Y in the Homebrew Launcher, click the
button, and the game starts with the editor's debugger, output and remote
scene tree connected.

## Screens and windows

- The main window is the top screen (400x240). Set the project's viewport size
  to 400x240.
- The bottom screen (320x240) is any other `Window`. Set
  `display/window/subwindows/embed_subwindows=false` so windows are real
  display windows, and `current_screen = 1` on the bottom one.
- Touches go to the bottom-screen window as `InputEventScreenTouch/Drag`
  (and emulated mouse events). With no bottom window they go to the top one.

## Stereoscopic 3D

The top screen's viewport renders one image per eye whenever the 3D slider is
up; with the slider down only one image is drawn. Each `Camera3D` is offset
sideways and its frustum sheared so objects at the convergence distance sit
on the screen plane; nearer objects come out of the screen.

- `rendering/3ds/stereo/max_separation` (default 0.12): eye separation in world
  units at full slider.
- `rendering/3ds/stereo/convergence` (default 3.0): distance from the camera
  with zero parallax.

2D canvas items draw at zero parallax (on the screen plane).

## Input

Joypad device 0, mapped by position like every other Godot platform:

| 3DS | Godot |
| --- | --- |
| B (bottom) | `JOY_BUTTON_A` |
| A (right) | `JOY_BUTTON_B` |
| Y (left) | `JOY_BUTTON_X` |
| X (top) | `JOY_BUTTON_Y` |
| L / R | shoulders |
| ZL / ZR (New 3DS) | trigger axes |
| Circle pad / C-stick | left / right stick |
| START / SELECT | start / back |

The accelerometer and gyroscope feed `Input.get_accelerometer()` and
`Input.get_gyroscope()`. The system keyboard opens for text fields
(`DisplayServer.virtual_keyboard_show`).

## `Nintendo3DS` singleton

Available from scripts on the 3DS. The stock Godot editor doesn't know the
class, so reach it through `Engine` to keep scripts parsing there:

```gdscript
var n3ds = Engine.get_singleton("Nintendo3DS") if Engine.has_singleton("Nintendo3DS") else null
```

Methods (an editor built from this fork has the class too, returning neutral
values):

- `get_3d_slider()`, `is_new_3ds()`, `get_battery_level()` (0-5), `is_charging()`
- `set_stereo_separation(float)`, `set_stereo_convergence(float)`
  (negative restores the project settings)
- `save_screens(dir)`: writes `top_left.png`, `top_right.png` and
  `bottom.png` with what the screens show.
- `get_audio_stats()`: `{active, buffers_played, last_peak}` of the DSP
  audio driver.

`OS.has_feature("3ds")` and `OS.has_feature("new_3ds")` are available too.

## Renderer (citro3d)

The PICA200 has vertex shaders but a fixed-function fragment stage, so the
renderer maps Godot's built-in materials onto it:

- `StandardMaterial3D` / `ORMMaterial3D`: albedo color and texture, UV1
  scale/offset, vertex color, unshaded, emission, roughness/metallic/specular
  (Blinn-Phong highlights), blend modes, cull modes, depth draw/test, alpha
  scissor, texture filter/repeat hints.
- Hardware per-pixel lighting for up to 8 lights: directional, omni and spot
  (Godot's distance falloff), plus the environment's ambient light.
- `Environment` background: clear color, color, or sky (ProceduralSkyMaterial,
  PanoramaSkyMaterial, PhysicalSkyMaterial approximated; ambient "Sky"
  takes its color; directional lights draw a sun disc in procedural and
  physical skies), and fog (exponential and depth modes) on the PICA fog
  unit.
- Mipmaps for textures imported with mipmaps (the 3D default).
- Shadows from the first `DirectionalLight3D` with shadows enabled (else the
  first shadowed `SpotLight3D`, with a perspective map of its cone): an
  orthographic shadow map (`rendering/3ds/shadows/size`, default 512) around
  the camera out to `rendering/3ds/shadows/max_distance` (default 20), read
  by the PICA200 lighting unit (only that light is darkened, as in Godot).
  Depth is 8-bit, so `rendering/3ds/shadows/bias` (default 0.012) trades
  acne against peter-panning. Casters are the meshes the camera sees.
- `MultiMesh` (per-instance color), `CPUParticles3D` and `CPUParticles2D`.
- Skeletons and blend shapes, skinned on the CPU (the PICA200 has uniform
  room for only ~28 bone matrices).
- 2D: rects, nine-patches, polygons, primitives, text, clipping, canvas
  modulate, `CanvasItemMaterial` blend modes, filter/repeat settings,
  `MeshInstance2D` and 2D MultiMesh.
- 2D lights (`PointLight2D` with a texture, `DirectionalLight2D`), add and
  subtract blend modes, item light masks and z ranges, energy up to 4;
  each lit item gets an extra additive pass per light. No 2D shadows or
  normal maps.
- Hand-written spatial `.gdshader` shaders, when `fragment()` is only
  assignments the fixed-function stage can do (see below). Custom
  `canvas_item` shaders draw with the default 2D pipeline.

### Custom spatial shaders

The PICA200 has no fragment shaders, so a `.gdshader` is translated when it
loads. Supported in `fragment()`:

- `ALBEDO`, `ALPHA` and `EMISSION` set to a product (`*`) of constants,
  uniforms (`vec3`/`vec4`/`float`, from the material or the code's
  defaults), `COLOR` (vertex color, ALBEDO only) and at most one
  `texture(sampler, uv)` lookup, with swizzles like `.rgb` or `.a`.
- Texture coordinates: `UV`, `UV * const`, `UV + const` and
  `UV + TIME * const` (scrolling).
- `ROUGHNESS`, `METALLIC`, `SPECULAR` set to a constant or uniform.
- Render modes as in `StandardMaterial3D` (unshaded, blend, cull, depth),
  and the sampler's `filter_*` / `repeat_*` hints.

```glsl
shader_type spatial;
uniform vec4 tint : source_color = vec4(1.0, 0.5, 0.2, 1.0);
uniform sampler2D tex : source_color, filter_nearest;
void fragment() {
	ALBEDO = tint.rgb * texture(tex, UV * 2.0 + TIME * vec2(0.1, 0.0)).rgb;
}
```

Anything else (other math, function calls, `if`, local variables, a
`vertex()` or `light()` function) prints a warning naming the shader and the
reason, and the material draws unshaded with its `albedo`/`color` and
`texture_albedo`/`texture` parameters.

Not implemented yet: GPU particles (need compute shaders: use CPUParticles),
2D skeletons, omni light shadows, 2D light shadows and normal maps, clouds
in the procedural sky, reflection probes and other post-processing. Textures larger than 1024x1024 are downscaled.

## Other limitations

- Audio: the DSP driver mixes at 32 kHz stereo with about 96 ms buffered,
  on its own thread, so slow frames don't underrun it. On real hardware it needs the DSP firmware dump at
  `sdmc:/3ds/dspfirm.cdc` (made by the DSP1 homebrew); without it the game
  runs silently with an error in the log. No microphone input.
- Networking: IPv4 TCP/UDP over Wi-Fi (`HTTPRequest`, `StreamPeerTCP`,
  ENet/WebSocket if those modules are enabled). No IPv6; servers should bind
  IPv4 addresses. soc:u takes 1 MB of memory.
- Threads: Godot's threads run below the main thread's priority on core 0
  (the 3DS kernel doesn't time-slice equal priorities), so background work
  uses the time the main thread waits for vsync. Audio mixes on its own
  thread (core 2 on the New 3DS).
- No GDExtension (no dynamic loading on the 3DS).

## Porting notes

- arm-none-eabi defines `int32_t` as `long`; `detect.py` redefines the 32-bit
  fixed-width types as `int` so Godot's `Variant` overloads stay distinct.
- Godot adds `-s` at link time, which breaks `3dsx.specs` (`--emit-relocs`);
  the SCsub removes it.
- The PICA200 samples texture row v = 0 at the end of the texture data, and
  draws render targets the same way when the projection is y-flipped, so all
  textures store Godot's top row last (see `rendering/citro3d_common.h`).
- citro3d queues clears to the GPU immediately but draws only at frame end, so
  a clear mid-frame first splits the frame (`C3D_FrameSplit`).
- citro3d can't create or delete render targets inside a frame; resizes and
  frees are deferred to the next frame.
