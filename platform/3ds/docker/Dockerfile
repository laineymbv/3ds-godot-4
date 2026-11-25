# Build image for the Godot 3DS platform: devkitARM + SCons.
FROM devkitpro/devkitarm:latest
RUN apt-get update && apt-get install -y --no-install-recommends python3-pip && \
    pip3 install --break-system-packages scons && rm -rf /var/lib/apt/lists/*
ENV PATH="/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:${PATH}"
