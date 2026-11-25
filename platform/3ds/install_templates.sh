#!/bin/sh
# Copy the built 3DS templates where the Godot editor looks for them:
#   <templates dir>/<version>/3ds_{debug,release}.{3dsx,elf}
# Usage: platform/3ds/install_templates.sh [templates dir]
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VERSION=$(python3 -c "import sys; sys.path.insert(0, '$ROOT'); import version as v; print(f'{v.major}.{v.minor}' + (f'.{v.patch}' if v.patch else '') + f'.{v.status}')")
DEST=${1:-${XDG_DATA_HOME:-$HOME/.local/share}/godot/export_templates}/$VERSION
mkdir -p "$DEST"
for t in debug release; do
	for ext in 3dsx elf; do
		src=$ROOT/bin/godot.3ds.template_$t.arm32.$ext; [ -f "$src" ] || src=$ROOT/bin/godot.3ds.template_$t.arm32.nothreads.$ext
		[ -f "$src" ] && cp "$src" "$DEST/3ds_$t.$ext" && echo "installed $DEST/3ds_$t.$ext"
	done
done
