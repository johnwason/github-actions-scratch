#!/usr/bin/env bash
# Runs inside the dev container (see scripts/podman.sh).
# Stage 1: upstream RobotRaconteurCore + RobotRaconteurGen, out-of-tree, installed to build/install.
# Stage 2: Go bindings (SWIG module, native static lib, generator).
set -euo pipefail
GO_DIR=/work/robotraconteur_go
RR_SRC=${RR_SRC:-/work/robotraconteur}
BUILD=${BUILD:-$GO_DIR/build}
# 8 GB RAM hosts: RR core and the SWIG wrapper are memory hungry to compile
JOBS=${JOBS:-2}
BUILD_TYPE=${BUILD_TYPE:-RelWithDebInfo}
# BUILD_TESTING is only passed when RR_TESTING is set (otherwise the cached value is kept)
RR_TESTING_ARG=""
if [ -n "${RR_TESTING:-}" ]; then RR_TESTING_ARG="-DBUILD_TESTING=$RR_TESTING"; fi

# Keep build outputs out of the Go module (./... must not see generated files)
mkdir -p "$BUILD"; [ -f "$BUILD/go.mod" ] || printf "module rrgo_build_ignore\n" > "$BUILD/go.mod"

MODE=${1:-all}   # all | core | go (go: skip the upstream stage)
TARGET=${2:-}

if [ "$MODE" != "go" ]; then
cmake -S "$RR_SRC" -B "$BUILD/rr" -G Ninja \
  -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
  -DCMAKE_INSTALL_PREFIX="$BUILD/install" \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DBUILD_GEN=ON $RR_TESTING_ARG -DBUILD_DOCUMENTATION=OFF
cmake --build "$BUILD/rr" -j "$JOBS"
cmake --install "$BUILD/rr" >/dev/null
fi

if [ "$MODE" = "core" ]; then exit 0; fi

cmake -S "$GO_DIR" -B "$BUILD/go" -G Ninja \
  -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
  -DCMAKE_PREFIX_PATH="$BUILD/install" \
  -DRR_SOURCE_DIR="$RR_SRC" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build "$BUILD/go" -j "$JOBS" ${TARGET:+--target "$TARGET"}
