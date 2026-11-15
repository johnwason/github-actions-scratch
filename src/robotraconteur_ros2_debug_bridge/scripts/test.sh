#!/usr/bin/env bash
# No-network smoke test plus this repo's tests: scripts/test.sh [release|asan|tsan] [colcon args...]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
variant="${1:-release}"; shift || true
run_in_container "$variant" "${SRC_MOUNT}/scripts/in-container/test.sh" "$@"
