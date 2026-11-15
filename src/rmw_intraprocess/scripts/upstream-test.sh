#!/usr/bin/env bash
# Usage: scripts/upstream-test.sh <release|asan|tsan> [test name regex, default .*]
# Builds the pinned upstream test_rmw_implementation sources (from the image,
# /opt/upstream_src) against the installed rmw_intraprocess_cpp of that variant
# and runs the tests for rmw_intraprocess_cpp matching the regex.
# Run scripts/build.sh <variant> first. Inside the container, --network=none.
# UPSTREAM_RMW=<rmw> runs the same tests against another installed RMW (e.g.
# rmw_fastrtps_cpp) for comparison; it still runs with --network=none.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

variant="${1:-release}"; shift || true
check_variant "$variant"
regex="${1:-.*}"
run_in_container "$variant" env "UPSTREAM_RMW=${UPSTREAM_RMW:-rmw_intraprocess_cpp}" bash "${SRC_MOUNT}/scripts/in-container/upstream-test.sh" "$regex"
