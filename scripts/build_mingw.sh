#!/usr/bin/env bash
# Cross-build for Windows x86_64 with MinGW-w64. Runs inside the mingw image:
#   scripts/podman.sh mingw-build [all|core|go]
# Stage 1: upstream RobotRaconteurCore (static) cross-built, installed to build-mingw/install.
# Stage 2: Go bindings native library (SWIG wrapper) cross-built; cgo_flags_windows.go generated.
# RobotRaconteurGenGo.exe is also built, but cannot run here; scripts/cross_test_build.sh
# generates code with the Linux generator instead.
set -euo pipefail
GO_DIR=/work/robotraconteur_go
RR_SRC=${RR_SRC:-/work/robotraconteur}
BUILD=${BUILD:-$GO_DIR/build-mingw}
JOBS=${JOBS:-2}
BUILD_TYPE=${BUILD_TYPE:-Release}
TOOLCHAIN=${TOOLCHAIN:-/opt/mingw64/toolchain.cmake}
# RR_TESTING=ON also builds the upstream C++ test programs used by test/interop
# and test/crosscheck (linked with -static: no MinGW runtime DLLs needed). Their thunk code is generated with the Linux
# RobotRaconteurGen from "scripts/podman.sh build" (HOST_RRGEN).
HOST_RRGEN=${HOST_RRGEN:-$GO_DIR/build/install/bin/RobotRaconteurGen}
RR_TESTING_ARG="-DBUILD_TESTING=OFF"
if [ "${RR_TESTING:-OFF}" = ON ]; then
  # PRE_TEST: gtest test discovery would run the .exe at build time
  RR_TESTING_ARG="-DBUILD_TESTING=ON -DRobotRaconteurGen_EXECUTABLE=$HOST_RRGEN
    -DCMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE=PRE_TEST"
fi
CPP_TESTS="robotraconteur_test_server robotraconteur_test_service robotraconteur_test_service2 robotraconteur_test_service3"

mkdir -p "$BUILD"; [ -f "$BUILD/go.mod" ] || printf "module rrgo_build_ignore\n" > "$BUILD/go.mod"

MODE=${1:-all}
TARGET=${2:-}

if [ "$MODE" != "go" ]; then
cmake -S "$RR_SRC" -B "$BUILD/rr" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
  -DCMAKE_INSTALL_PREFIX="$BUILD/install" \
  -DCMAKE_PREFIX_PATH=/opt/mingw64 \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_EXE_LINKER_FLAGS=-static \
  -DBUILD_GEN=OFF $RR_TESTING_ARG -DBUILD_DOCUMENTATION=OFF
cmake --build "$BUILD/rr" -j "$JOBS" --target RobotRaconteurCore
cmake --install "$BUILD/rr" --component Unspecified >/dev/null
if [ "${RR_TESTING:-OFF}" = ON ]; then
  # only the programs the Go tests run (not the GTest unit tests)
  cmake --build "$BUILD/rr" -j 1 --target $CPP_TESTS
  mkdir -p "$BUILD/tests/cpp"
  for t in $CPP_TESTS; do cp "$(find "$BUILD/rr/test" -name "$t.exe" -path "*bin*" | head -1)" "$BUILD/tests/cpp/"; done
fi
fi

if [ "$MODE" = "core" ]; then exit 0; fi

cmake -S "$GO_DIR" -B "$BUILD/go" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
  -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
  -DCMAKE_PREFIX_PATH="$BUILD/install;/opt/mingw64" \
  -DRR_SOURCE_DIR="$RR_SRC" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build "$BUILD/go" -j "$JOBS" ${TARGET:+--target "$TARGET"}
