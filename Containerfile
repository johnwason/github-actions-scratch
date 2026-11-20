# Build and test image for robotraconteur_lua. All third-party code is built and run in
# this image (see CLAUDE.md, "Boundaries for unattended work"). Pinned sources are fetched
# by scripts/bootstrap.sh into .deps/, not baked into the image.
FROM docker.io/library/ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Packages named in CLAUDE.md, plus build tooling: git (pinned clones), make (Lua and
# LuaJIT builds), python3-venv (the Python test peer venv), file/binutils (size checks),
# and libgtest-dev (RR core's test tree, which holds the C++ test server, requires it).
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        cmake ninja-build g++ swig curl ca-certificates \
        libboost-all-dev libssl-dev libdbus-1-dev libbluetooth-dev libusb-1.0-0-dev \
        libgtest-dev \
        git make python3 python3-venv binutils file \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
