#!/usr/bin/env bash
# M7 conformance gate: runs every upstream suite against rmw_intraprocess_cpp
# (in the container, --network=none). Approved exclusions are in
# scripts/in-container/conformance.sh and docs/unsupported.md.
#
# Usage: scripts/check-conformance.sh [variant...]   (default: release)
# Requires scripts/build.sh <variant> first. Slow on the first run of a variant
# (builds rcl, rclcpp and system_tests packages with tests).
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SUITES=(rcl rclcpp rcl_action rcl_lifecycle rclcpp_action rclcpp_lifecycle rclcpp_components
  system_tests:test_rclcpp system_tests:test_quality_of_service
  system_tests:test_communication)
variants=("$@")
[[ ${#variants[@]} -eq 0 ]] && variants=(release)

rc=0
for variant in "${variants[@]}"; do
  for suite in "${SUITES[@]}"; do
    echo "================ conformance ${suite} (${variant}) ================"
    if ! "${HERE}/conformance.sh" "$variant" "$suite"; then
      rc=1
    fi
  done
done
[[ $rc -eq 0 ]] && echo "check-conformance: OK" || echo "check-conformance: FAILED" >&2
exit $rc
