#!/usr/bin/env bash
# Usage: scripts/run-test-binary.sh <variant> <binary under /ws/build/rmw_intraprocess_cpp> [args...]
# Runs one built test binary directly (e.g. with --gtest_filter) inside the
# container, --network=none, with the variant's install sourced. Env vars to
# pass through: RMW_IMPLEMENTATION, RUN_TIMEOUT (seconds, default 120).
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

variant="${1:?variant}"; shift
binary="${1:?binary}"; shift
check_variant "$variant"
run_in_container "$variant" env \
  "RMW_IMPLEMENTATION=${RMW_IMPLEMENTATION:-rmw_intraprocess_cpp}" \
  "RUN_TIMEOUT=${RUN_TIMEOUT:-120}" \
  bash -c 'source "$0/scripts/in-container/env.sh"; set +u; source /ws/install/setup.bash; set -u;
    export LD_LIBRARY_PATH="/ws/build/rmw_intraprocess_cpp:${LD_LIBRARY_PATH}";
    exec timeout --signal=TERM --kill-after=5 "${RUN_TIMEOUT}" "/ws/build/rmw_intraprocess_cpp/$1" "${@:2}"' \
  "${SRC_MOUNT}" "$binary" "$@"
