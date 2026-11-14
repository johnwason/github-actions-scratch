#!/usr/bin/env bash
# Inside the container: build an upstream package's tests (rcl, rclcpp, ...) from
# /opt/upstream_src and run them with this RMW (M7).
#
# Usage: conformance.sh <suite> [ctest -R regex] [ctest -E regex]
#   suite: rcl | rclcpp | rcl_action | rcl_lifecycle | rclcpp_action |
#          rclcpp_lifecycle | rclcpp_components | system_tests:<package> (test_rclcpp, test_communication, ...)
#
# The upstream package is built into its own build/install bases under
# /ws/build/_conformance, which are never sourced by the other scripts, so the
# test build of rcl/rclcpp does not shadow the installed one.
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
suite="${1:?suite}"
include="${2:-.*}"
exclude_extra="${3:-}"
# RMW under test (CONFORMANCE_RMW=rmw_fastrtps_cpp runs the same selection on the
# reference RMW, to tell upstream defects from ours).
rmw="${CONFORMANCE_RMW:-rmw_intraprocess_cpp}"

# Exclusions approved by the user (2026-11-13); each one is documented in
# docs/unsupported.md. Whole ctest targets go in excluded_tests (regex),
# single gtest cases in excluded_cases (':'-separated gtest names).
excluded_tests=''
excluded_cases=''
case "$suite" in
  rcl)
    base=/opt/upstream_src/rcl; pkg=rcl
    # xmllint: needs the network for the package.xml schema.
    # test_services: server and client in separate processes (cross-process).
    excluded_tests="^xmllint\$|^test_services__${rmw}\$"
    # Deadline/liveliness events (unsupported, PLAN.md Q3).
    excluded_cases='TestEventFixture.test_event_is_valid:TestEventFixture.test_pubsub_deadline_missed'
    excluded_cases+=':TestEventFixture.test_pubsub_no_deadline_missed'
    excluded_cases+=':TestEventFixture.test_pubsub_liveliness_kill_pub'
    ;;
  rclcpp)
    base=/opt/upstream_src/rclcpp; pkg=rclcpp
    # xmllint: needs the network. cppcheck: findings in rclcpp's own code.
    excluded_tests='^xmllint$|^cppcheck$'
    # Deadline events (unsupported, PLAN.md Q3).
    excluded_cases='TestWaitSet.add_guard_condition_to_two_different_wait_set'
    excluded_cases+=':TestWaitSet.add_remove_wait'
    ;;
  rcl_action|rcl_lifecycle)
    # M8: action and lifecycle layers of rcl.
    base=/opt/upstream_src/rcl; pkg="$suite"
    excluded_tests='^xmllint$'
    ;;
  rclcpp_action|rclcpp_lifecycle)
    # M8: action and lifecycle layers of rclcpp.
    base=/opt/upstream_src/rclcpp; pkg="$suite"
    excluded_tests='^xmllint$'
    ;;
  rclcpp_components)
    # M8: ComponentManager load/unload/list over services. cppcheck: findings
    # in Google Benchmark's header (benchmark.h missingReturn), not the RMW.
    base=/opt/upstream_src/rclcpp; pkg="$suite"
    excluded_tests='^xmllint$|^cppcheck$'
    ;;
  system_tests:*)
    base=/opt/upstream_src/system_tests; pkg="${suite#system_tests:}"
    # xmllint: needs the network (all packages).
    excluded_tests='^xmllint$'
    case "$pkg" in
      test_rclcpp)
        # Two or more executables (cross-process).
        excluded_tests+='|^test_(client_scope_cpp|client_scope_consistency_cpp|services_cpp'
        excluded_tests+="|parameter_server_cpp|node_name|n_nodes)__${rmw}\$"
        ;;
      test_communication)
        # Publisher/subscriber, requester/replier and action client/server in
        # separate processes, for each rclcpp/rclpy pairing (cross-process).
        excluded_tests+='|^test_(publisher_subscriber|requester_replier|action_client_server)__'
        ;;
    esac
    ;;
  *) echo "unknown suite: $suite" >&2; exit 2 ;;
esac
[[ -d "$base" ]] || { echo "upstream sources for $suite are not in the image: $base" >&2; exit 2; }
[[ -f /ws/install/setup.bash ]] || { echo "no install for variant $VARIANT; run scripts/build.sh $VARIANT" >&2; exit 1; }
"${SRC_DIR}/scripts/in-container/check-no-network.sh"
set +u
# shellcheck disable=SC1091
source /ws/install/setup.bash
set -u

CBASE=/ws/build/_conformance
mkdir -p "$CBASE"
cd "$CBASE"
colcon --log-base "$CBASE/log" build \
  --base-paths "$base" \
  --packages-select "$pkg" \
  --build-base "$CBASE/build" \
  --install-base "$CBASE/install" \
  --event-handlers console_cohesion+ \
  --parallel-workers 1 \
  --cmake-args \
    -DBUILD_TESTING=ON \
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}" \
    "-DCMAKE_C_FLAGS=${SAN_FLAGS}" \
    "-DCMAKE_CXX_FLAGS=${SAN_FLAGS}" \
    "-DCMAKE_EXE_LINKER_FLAGS=${SAN_FLAGS}" \
    "-DCMAKE_SHARED_LINKER_FLAGS=${SAN_FLAGS}"

# Tests not tied to an RMW use RMW_IMPLEMENTATION; per-RMW instances of other
# RMWs are excluded.
export RMW_IMPLEMENTATION="${rmw}"
exclude="$(printf "%s\n" rmw_intraprocess_cpp rmw_fastrtps_cpp rmw_fastrtps_dynamic_cpp rmw_cyclonedds_cpp rmw_connextdds rmw_zenoh_cpp | grep -vx "${rmw}" | sed "s/^/__/" | paste -sd"|")"
[[ -n "$excluded_tests" ]] && exclude="${exclude}|${excluded_tests}"
[[ -n "$exclude_extra" ]] && exclude="${exclude}|${exclude_extra}"
if [[ -n "$excluded_cases" ]]; then
  # gtest reads GTEST_FILTER from the environment of every test binary.
  export GTEST_FILTER="-${excluded_cases}"
fi
echo "conformance ($suite/$pkg): include '${include}', exclude '${exclude}'"
echo "conformance ($suite/$pkg): excluded gtest cases '${excluded_cases}'"
rm -f "$CBASE/build/$pkg/test_results/$pkg"/*.xml
rc=0
colcon --log-base "$CBASE/log" test \
  --base-paths "$base" \
  --packages-select "$pkg" \
  --build-base "$CBASE/build" \
  --install-base "$CBASE/install" \
  --event-handlers console_direct+ \
  --return-code-on-test-failure \
  --ctest-args -R "$include" -E "$exclude" --output-on-failure || rc=$?
colcon test-result --test-result-base "$CBASE/build/$pkg" || rc=1
[[ $rc -eq 0 ]] && echo "conformance $suite ($VARIANT): OK" || { echo "conformance $suite ($VARIANT): FAILED" >&2; exit $rc; }
