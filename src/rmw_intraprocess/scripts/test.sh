#!/usr/bin/env bash
# Usage: scripts/test.sh [release|asan|tsan] [extra colcon test args...]
# Runs (inside the container, --network=none): the no-network smoke test,
# colcon test for the given variant's build, and the exported/imported symbol check.
# Run scripts/build.sh <variant> first.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

variant="${1:-release}"; shift || true
check_variant "$variant"
run_in_container "$variant" bash "${SRC_MOUNT}/scripts/in-container/test.sh" "$@"
