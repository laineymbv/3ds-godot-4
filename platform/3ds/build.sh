#!/bin/sh
# Build the 3DS export template in Docker. Extra args go to scons, e.g.
#   platform/3ds/build.sh target=template_debug
set -e
cd "$(dirname "$0")/../.."
docker image inspect godot3ds-build >/dev/null 2>&1 || docker build -t godot3ds-build platform/3ds/docker
exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/work -w /work godot3ds-build \
	scons platform=3ds -j"$(nproc)" "$@"
