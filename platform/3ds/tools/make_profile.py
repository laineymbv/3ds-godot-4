#!/usr/bin/env python3
"""Make a Godot build profile that drops the node classes a project never uses.

The 3DS template is about 20 MB and the original 3DS gives an application
64 MB, so every class left out of the binary is memory for the game. Pass the
profile to the build:

    platform/3ds/build.sh target=template_release build_profile=/path/profile.json

Usage:
    make_profile.py <project dir> -o profile.json [--godot EDITOR] [--api JSON]
                    [--keep Class ...] [--also-project DIR ...]

The class list comes from `godot --headless --dump-extension-api` (--godot, or
--api for an existing extension_api.json). With --godot the editor also loads
every scene to read the node types inside it, imported ones included. A class counts as used when its name
appears anywhere in the project: scenes, resources, scripts, shaders, and the
imported binaries under .godot/imported. A used class keeps its ancestors too.
Only Node classes are dropped; resources and servers stay, because those are
often created from names the scan can't see. Anything the scan misses (a class
built from a string made at run time, say) goes in --keep.

Several projects can share one template: --also-project adds their classes.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

TEXT_EXT = {".gd", ".tscn", ".tres", ".godot", ".gdshader", ".gdshaderinc", ".cfg", ".json", ".import", ".csv"}
BINARY_EXT = {".scn", ".res"}

# Nodes the engine itself needs, whatever the project says.
ALWAYS = {
    "Node", "Window", "Viewport", "SubViewport", "CanvasItem", "CanvasLayer", "Node2D", "Node3D",
    "Control", "Timer", "AudioStreamPlayer", "HTTPRequest", "CanvasModulate", "WorldEnvironment",
}

# Node types a glTF/FBX/Blender import can produce (used when no editor is given
# to read the imported scenes, whose files are compressed).
IMPORT_NODES = {"MeshInstance3D", "Skeleton3D", "BoneAttachment3D", "AnimationPlayer", "Camera3D", "DirectionalLight3D",
                "OmniLight3D", "SpotLight3D", "MultiMeshInstance3D"}
SCENE_EXT = {".tscn", ".scn", ".glb", ".gltf", ".fbx", ".blend", ".obj"}

SCAN_SCRIPT = """extends SceneTree

func _init() -> void:
	var seen: Dictionary = {}
	_walk("res://", seen)
	for c: String in seen:
		print("NODE_CLASS ", c)
	quit()

func _walk(dir: String, seen: Dictionary) -> void:
	for f: String in DirAccess.get_files_at(dir):
		var ext: String = f.get_extension()
		if ext == "tscn" or ext == "scn" or ext == "glb" or ext == "gltf" or ext == "fbx":
			var ps: PackedScene = load(dir.path_join(f)) as PackedScene
			if ps == null:
				continue
			var st: SceneState = ps.get_state()
			for i: int in st.get_node_count():
				seen[str(st.get_node_type(i))] = true
	for d: String in DirAccess.get_directories_at(dir):
		if not d.begins_with("."):
			_walk(dir.path_join(d), seen)
"""

TOKEN = re.compile(rb"[A-Za-z_][A-Za-z0-9_]{2,}")


def scan(project, names):
    used = set()
    for root, dirs, files in os.walk(project):
        rel = os.path.relpath(root, project)
        if rel.startswith(".godot"):
            # Only the imported resources: they hold the types of imported scenes.
            if not rel.startswith(os.path.join(".godot", "imported")):
                dirs[:] = []
                continue
        for f in files:
            ext = os.path.splitext(f)[1].lower()
            if ext not in TEXT_EXT and ext not in BINARY_EXT:
                continue
            try:
                data = open(os.path.join(root, f), "rb").read()
            except OSError:
                continue
            for m in TOKEN.finditer(data):
                t = m.group().decode("ascii")
                if t in names:
                    used.add(t)
    return used


def scene_nodes(godot, project):
    """Node classes of every scene in the project, read by the editor itself."""
    tmp = tempfile.mkdtemp()
    path = os.path.join(tmp, "scan.gd")
    open(path, "w").write(SCAN_SCRIPT)
    out = subprocess.run([os.path.abspath(godot), "--headless", "--path", project, "--script", path],
                         capture_output=True, text=True).stdout
    return {l.split()[1] for l in out.splitlines() if l.startswith("NODE_CLASS ") and len(l.split()) > 1}


def has_imports(project):
    for root, dirs, files in os.walk(project):
        dirs[:] = [d for d in dirs if not d.startswith(".")]
        if any(os.path.splitext(f)[1].lower() in (".glb", ".gltf", ".fbx", ".blend", ".obj") for f in files):
            return True
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("project")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--godot", help="editor binary used to dump the class list")
    ap.add_argument("--api", help="existing extension_api.json")
    ap.add_argument("--keep", nargs="*", default=[], help="extra classes to keep")
    ap.add_argument("--also-project", nargs="*", default=[], help="more projects the template must run")
    args = ap.parse_args()

    api = args.api
    if not api:
        if not args.godot:
            sys.exit("need --godot or --api")
        tmp = tempfile.mkdtemp()
        subprocess.run([os.path.abspath(args.godot), "--headless", "--dump-extension-api"], cwd=tmp, check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        api = os.path.join(tmp, "extension_api.json")
    classes = {c["name"]: c.get("inherits", "") for c in json.load(open(api))["classes"]}

    def ancestors(c):
        while c:
            yield c
            c = classes.get(c, "")

    def is_node(c):
        return "Node" in ancestors(c)

    used = set(ALWAYS) | set(args.keep)
    for p in [args.project] + args.also_project:
        used |= scan(p, classes)
        if args.godot:
            used |= scene_nodes(args.godot, p)
        elif has_imports(p):
            print(f"note: {p} has imported scenes; assuming the usual glTF node types (pass --godot to read them)")
            used |= IMPORT_NODES
    unknown = [k for k in args.keep if k not in classes]
    if unknown:
        sys.exit("unknown classes in --keep: " + ", ".join(unknown))

    keep = set()
    for c in used:
        keep.update(ancestors(c))
    disabled = sorted(c for c in classes if is_node(c) and c not in keep)
    json.dump({"type": "build", "disabled_classes": disabled}, open(args.output, "w"), indent=1)
    print(f"{len(used & set(classes))} classes used, {len(disabled)} node classes dropped -> {args.output}")


if __name__ == "__main__":
    main()
