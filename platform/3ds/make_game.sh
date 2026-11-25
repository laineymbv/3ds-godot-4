#!/bin/sh
# Bundle an exported Godot .pck into a runnable 3DS game.
#
#   platform/3ds/make_game.sh [options] <game.pck> <out.3dsx|out.cia>
#
# Options:
#   --elf PATH      export template (default: bin/godot.3ds.template_release...elf)
#   --title TEXT    title shown in the launcher (default: output file name)
#   --author TEXT   publisher line (default: "Made with Godot Engine")
#   --icon PNG      48x48 icon (default: platform/3ds/icon.png)
#   --banner PNG    256x128 HOME menu banner, CIA only (default: cia/banner.png)
#   --id HEX        CIA unique id, 0x00300-0xF7FFF (default: derived from title)
#
# The output type follows the extension. A .cia needs makerom and bannertool;
# set MAKEROM / BANNERTOOL if they aren't on PATH.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
P3=$ROOT/platform/3ds
ELF=$ROOT/bin/godot.3ds.template_release.arm32.elf
TITLE= AUTHOR="Made with Godot Engine" ICON=$P3/icon.png BANNER=$P3/cia/banner.png ID=
while [ $# -gt 2 ]; do
	case "$1" in
		--elf) ELF=$2; shift 2 ;;
		--title) TITLE=$2; shift 2 ;;
		--author) AUTHOR=$2; shift 2 ;;
		--icon) ICON=$2; shift 2 ;;
		--banner) BANNER=$2; shift 2 ;;
		--id) ID=$2; shift 2 ;;
		*) echo "unknown option $1" >&2; exit 1 ;;
	esac
done
[ $# -eq 2 ] || { sed -n 2,16p "$0"; exit 1; }
PCK=$(realpath "$1"); OUT=$(realpath -m "$2")
ELF=$(realpath "$ELF"); ICON=$(realpath "$ICON"); BANNER=$(realpath "$BANNER")
[ -n "$TITLE" ] || TITLE=$(basename "$OUT" | sed 's/\.[^.]*$//')

WORK=$(mktemp -d); trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/romfs"
cp "$PCK" "$WORK/romfs/game.pck"
cp "$ELF" "$WORK/game.elf"; cp "$ICON" "$WORK/icon.png"

case "$OUT" in
*.3dsx)
	docker run --rm -u "$(id -u):$(id -g)" -v "$WORK":/w -w /w godot3ds-build sh -c \
		'smdhtool --create "$0" "$1" "$1" icon.png game.smdh && 3dsxtool game.elf game.3dsx --smdh=game.smdh --romfs=romfs' \
		"$TITLE" "$AUTHOR"
	cp "$WORK/game.3dsx" "$OUT"
	;;
*.cia)
	MAKEROM=${MAKEROM:-$(command -v makerom || echo "$HOME/Projects/roguelike-3ds/cia/tools/makerom")}
	BANNERTOOL=${BANNERTOOL:-$(command -v bannertool || echo "$HOME/Projects/roguelike-3ds/cia/tools/bannertool-1.2.3-linux/bannertool")}
	[ -x "$MAKEROM" ] && [ -x "$BANNERTOOL" ] || { echo "makerom/bannertool not found (set MAKEROM, BANNERTOOL)" >&2; exit 1; }
	if [ -z "$ID" ]; then
		# Stable per title, inside the homebrew-safe range.
		ID=$(printf '0x%05X' $(( 0x10000 + $(printf '%s' "$TITLE" | cksum | cut -d' ' -f1) % 0xE0000 )))
	fi
	PRODUCT=$(printf 'CTR-P-%s' "$(printf '%s' "$TITLE" | tr -cd 'A-Za-z0-9' | tr a-z A-Z | cut -c1-4)")
	sed -e "s|@TITLE@|$(printf '%s' "$TITLE" | cut -c1-63)|" -e "s|@PRODUCT@|$PRODUCT|" -e "s|@UNIQUE_ID@|$ID|" \
		"$P3/cia/template.rsf" > "$WORK/app.rsf"
	cp "$BANNER" "$WORK/banner.png"; cp "$P3/cia/banner.wav" "$WORK/banner.wav"
	(cd "$WORK" &&
		"$BANNERTOOL" makesmdh -s "$TITLE" -l "$TITLE" -p "$AUTHOR" -i icon.png -o icon.smdh >/dev/null &&
		"$BANNERTOOL" makebanner -i banner.png -a banner.wav -o banner.bnr >/dev/null &&
		"$MAKEROM" -f cia -o game.cia -elf game.elf -rsf app.rsf -icon icon.smdh -banner banner.bnr -exefslogo -target t)
	cp "$WORK/game.cia" "$OUT"
	echo "unique id $ID" >&2
	;;
*) echo "output must end in .3dsx or .cia" >&2; exit 1 ;;
esac
echo "$OUT"
