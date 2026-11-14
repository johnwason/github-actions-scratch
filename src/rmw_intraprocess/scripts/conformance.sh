#!/usr/bin/env bash
# Build and run an upstream suite (rcl, rclcpp, system_tests) against
# rmw_intraprocess_cpp, in the container with --network=none (M7).
#
# Usage: scripts/conformance.sh [variant] <suite> [ctest -R regex] [ctest -E regex]
# Requires scripts/build.sh <variant> first.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

variant="${1:-release}"; shift || true
check_variant "$variant"
suite="${1:?usage: conformance.sh [variant] <rcl|rclcpp|system_tests:<pkg>> [include] [exclude]}"
shift
run_in_container "$variant" env "CONFORMANCE_RMW=${CONFORMANCE_RMW:-rmw_intraprocess_cpp}" \
  bash "${SRC_MOUNT}/scripts/in-container/conformance.sh" "$suite" "$@"
