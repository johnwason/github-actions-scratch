#!/usr/bin/env bash
# Usage: scripts/run-test-binary.sh <variant> <binary under /ws/build/robotraconteur_ros2_debug_bridge> [args...]
# Runs one built test binary directly (e.g. with --gtest_filter) inside the
# container, --network=none, with the variant's install sourced. Env vars to
# pass through: RMW_IMPLEMENTATION (default rmw_intraprocess_cpp), RUN_TIMEOUT
# (seconds, default 300).
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

variant="${1:?variant}"; shift
binary="${1:?binary}"; shift
check_variant "$variant"
run_in_container "$variant" env \
  "RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION:-rmw_intraprocess_cpp}" \
  "RUN_TIMEOUT=${RUN_TIMEOUT:-300}" \
  bash -c 'source "$0/scripts/in-container/env.sh"; set +u; source /ws/install/setup.bash; set -u;
    exec timeout --signal=TERM --kill-after=5 "${RUN_TIMEOUT}" \
      "/ws/build/robotraconteur_ros2_debug_bridge/$1" "${@:2}"' \
  "${SRC_MOUNT}" "$binary" "$@"
