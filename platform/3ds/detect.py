import os
import sys
from typing import TYPE_CHECKING

from methods import print_error
from platform_methods import validate_arch

if TYPE_CHECKING:
    from SCons.Script.SConscript import SConsEnvironment

# Nintendo 3DS platform (homebrew, devkitARM + libctru + citro3d).
# Build inside platform/3ds/docker (see README.md in this folder).

# Modules compiled into the 3DS template. Everything else is off: the old 3DS
# gives an application about 64 MB, so the runtime must stay small.
ENABLED_MODULES = [
    "gdscript",
    "freetype",
    "text_server_fb",
    "godot_physics_2d",
    "godot_physics_3d",
    "regex",
    "jpg",
    "ogg",
    "vorbis",
    "mp3",
    "bcdec",
]


def get_name():
    return "Nintendo 3DS"


def get_devkitpro():
    return os.environ.get("DEVKITPRO", "/opt/devkitpro")


def can_build():
    return os.path.exists(os.path.join(get_devkitpro(), "devkitARM", "bin", "arm-none-eabi-g++"))


def get_tools(env: "SConsEnvironment"):
    return ["cc", "c++", "ar", "link", "textfile"]


def get_opts():
    from SCons.Variables import BoolVariable

    return [
        ("linear_heap_mb", "GPU-visible (linear) heap in MiB; 0 = 30% of free memory at start, 8-32 MiB", 0),
    ]


def get_doc_classes():
    return []


def get_doc_path():
    return "doc_classes"


def get_program_suffix():
    return "3ds"


def get_flags():
    flags = {
        "arch": "arm32",
        "target": "template_release",
        "optimize": "size_extra",
        "threads": False,
        "vulkan": False,
        "opengl3": False,
        "d3d12": False,
        "metal": False,
        "accesskit": False,
        "sdl": False,
        "minizip": False,
        # The built-in default font is WOFF2, which needs Brotli.
        "brotli": True,
        "disable_xr": True,
        "disable_navigation_2d": True,
        "disable_navigation_3d": True,
        "disable_advanced_gui": True,
        # Keep deprecated APIs so scripts that run in the editor run here too.
        "deprecated": True,
        "modules_enabled_by_default": False,
        "builtin_pcre2_with_jit": False,
        # devkitPro's FreeType has no Brotli, so it can't read WOFF2 fonts
        # (including Godot's default font); use the bundled one.
        "builtin_freetype": True,
        "builtin_zlib": False,
        "builtin_libpng": False,
        "lto": "full",
        # The game .pck is loaded from RomFS with --main-pack.
        "disable_path_overrides": False,
    }
    for m in ENABLED_MODULES:
        flags["module_%s_enabled" % m] = True
    return flags


def configure(env: "SConsEnvironment"):
    validate_arch(env["arch"], get_name(), ["arm32"])

    if env.editor_build:
        print_error("The 3DS platform only builds export templates (target=template_debug/template_release).")
        sys.exit(255)

    dkp = get_devkitpro()
    bindir = os.path.join(dkp, "devkitARM", "bin")
    prefix = os.path.join(bindir, "arm-none-eabi-")
    env["CC"] = prefix + "gcc"
    env["CXX"] = prefix + "g++"
    env["AR"] = prefix + "gcc-ar"
    env["RANLIB"] = prefix + "gcc-ranlib"
    env["AS"] = prefix + "as"
    env["LINK"] = prefix + "g++"
    env["OBJCOPY"] = prefix + "objcopy"
    env["STRIP"] = prefix + "strip"
    env["PROGSUFFIX"] = ".elf"
    env.PrependENVPath("PATH", bindir)
    env.PrependENVPath("PATH", os.path.join(dkp, "tools", "bin"))

    arch_flags = ["-march=armv6k", "-mtune=mpcore", "-mfloat-abi=hard", "-mtp=soft"]
    env.Append(CCFLAGS=arch_flags + ["-mword-relocations", "-ffunction-sections", "-fdata-sections"])
    env.Append(LINKFLAGS=arch_flags + ["-specs=3dsx.specs", "-Wl,--gc-sections"])

    env.Append(
        CPPDEFINES=[
            "__3DS__",
            "THREEDS_ENABLED",
            "UNIX_ENABLED",
            "UNIX_SOCKET_UNAVAILABLE",
            "NO_FCNTL",
            "PTHREAD_NO_RENAME",
            ("FIONREAD", "0x541B"),
        ]
    )
    # arm-none-eabi defines int32_t as long. Godot's type machinery assumes
    # int32_t is int (true on every other target), and both are 32-bit on
    # ARM, so redefine the fixed-width 32-bit types as int.
    env.Append(
        CCFLAGS=[
            "-Wno-builtin-macro-redefined",
            "-U__INT32_TYPE__",
            "-D__INT32_TYPE__=int",
            "-U__UINT32_TYPE__",
            "-D__UINT32_TYPE__=unsigned int",
            "-U__INT32_MAX__",
            "-D__INT32_MAX__=2147483647",
            "-U__UINT32_MAX__",
            "-D__UINT32_MAX__=4294967295U",
        ]
    )
    env.Append(CPPDEFINES=[("THREEDS_LINEAR_HEAP_MB", int(env["linear_heap_mb"]))])
    env.Append(LINKFLAGS=["-Wl,--wrap=__system_allocateHeaps"])

    env.Append(
        CPPPATH=[
            "#platform/3ds",
            "#platform/3ds/compat",
            os.path.join(dkp, "libctru", "include"),
            os.path.join(dkp, "portlibs", "3ds", "include"),
        ]
    )
    env.Append(
        LIBPATH=[
            os.path.join(dkp, "libctru", "lib"),
            os.path.join(dkp, "portlibs", "3ds", "lib"),
        ]
    )

    # Order matters for static linking: users before providers.
    env.Append(LIBS=["png", "z"])
    if env.debug_features:
        env.Append(LIBS=["citro3dd", "ctrud"])
    else:
        env.Append(LIBS=["citro3d", "ctru"])
    env.Append(LIBS=["m"])
