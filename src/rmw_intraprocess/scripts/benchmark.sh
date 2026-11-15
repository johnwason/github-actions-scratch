#!/usr/bin/env bash
# Usage: scripts/benchmark.sh [variant, default release]
# M10: runs the rclcpp publish/subscribe benchmark (rmw_intraprocess_cpp
# bench_pubsub: latency and throughput, 64 B .. 1 MB) against this RMW and
# the reference rmw_fastrtps_cpp, in one container with --network=none.
# Numbers depend on the machine; compare the two RMWs from the same run.
# Run scripts/build.sh <variant> first.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

variant="${1:-release}"; shift || true
check_variant "$variant"
run_in_container "$variant" bash -c '
  source "$0/scripts/in-container/env.sh"
  "${SRC_DIR}/scripts/in-container/check-no-network.sh"
  set +u; source /ws/install/setup.bash; set -u
  for rmw in rmw_intraprocess_cpp rmw_fastrtps_cpp; do
    RMW_IMPLEMENTATION=$rmw timeout 900 /ws/build/rmw_intraprocess_cpp/bench_pubsub \
      2>&1 | grep -vE "^\[(INFO|WARN)\]"
  done' "${SRC_MOUNT}"
