#!/usr/bin/env bash
# Build both repos' packages for a variant: scripts/build.sh [release|asan|tsan] [colcon args...]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
variant="${1:-release}"; shift || true
run_in_container "$variant" "${SRC_MOUNT}/scripts/in-container/build.sh" "$@"
