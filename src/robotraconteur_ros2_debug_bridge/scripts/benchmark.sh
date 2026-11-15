#!/usr/bin/env bash
# Usage: scripts/benchmark.sh [benchmark.py args...]
# B12 benchmark (docs/benchmark.md) on the release build, inside the
# container (--network=none), under rmw_intraprocess_cpp and then
# rmw_fastrtps_cpp. Run scripts/build.sh release first.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

run_in_container release bash -c '
  source "$0/scripts/in-container/env.sh"
  set +u; source /ws/install/setup.bash; set -u
  "$0/scripts/in-container/check-no-network.sh"
  echo "robotraconteur: $(tr "\n" " " < /opt/robotraconteur/VERSIONS)"
  echo "cpus: $(nproc), $(grep -m1 "model name" /proc/cpuinfo | cut -d: -f2)"
  pkg=/ws/build/robotraconteur_ros2_debug_bridge
  for rmw in rmw_intraprocess_cpp rmw_fastrtps_cpp; do
    RMW_IMPLEMENTATION=$rmw python3 "$0/robotraconteur_ros2_debug_bridge/test/benchmark.py" \
      --exe "$pkg/benchmark_host" "$@"
  done' "${SRC_MOUNT}" "$@"
