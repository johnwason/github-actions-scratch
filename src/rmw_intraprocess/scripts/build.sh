#!/usr/bin/env bash
# Usage: scripts/build.sh [release|asan|tsan] [extra colcon build args...]
# Builds the workspace inside the container with --network=none.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

variant="${1:-release}"; shift || true
check_variant "$variant"
run_in_container "$variant" bash "${SRC_MOUNT}/scripts/in-container/build.sh" "$@"
