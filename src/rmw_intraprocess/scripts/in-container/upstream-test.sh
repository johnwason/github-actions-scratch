#!/usr/bin/env bash
# Inside the container: build and run upstream test_rmw_implementation for this RMW.
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
regex="$1"
UPSTREAM=/opt/upstream_src/rmw_implementation/test_rmw_implementation

[[ -f /ws/install/setup.bash ]] || { echo "no install for variant $VARIANT; run scripts/build.sh $VARIANT" >&2; exit 1; }
"${SRC_DIR}/scripts/in-container/check-no-network.sh"
set +u
# shellcheck disable=SC1091
source /ws/install/setup.bash
set -u

cd /ws
colcon --log-base /ws/log build \
  --base-paths "$UPSTREAM" \
  --packages-select test_rmw_implementation \
  --build-base /ws/build \
  --install-base /ws/install \
  --event-handlers console_cohesion+ \
  --cmake-args \
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}" \
    "-DCMAKE_C_FLAGS=${SAN_FLAGS}" \
    "-DCMAKE_CXX_FLAGS=${SAN_FLAGS}" \
    "-DCMAKE_EXE_LINKER_FLAGS=${SAN_FLAGS}" \
    "-DCMAKE_SHARED_LINKER_FLAGS=${SAN_FLAGS}"

# Only the instances of the upstream tests for the selected RMW.
rmw="${UPSTREAM_RMW:-rmw_intraprocess_cpp}"
full_regex="(${regex})__${rmw}\$"
echo "upstream tests matching: ${full_regex}"
# Upstream test code defects only (never rmw_intraprocess_cpp leaks): see the file.
export LSAN_OPTIONS="suppressions=${SRC_DIR}/scripts/in-container/lsan-upstream.supp:print_suppressions=1"
# Drop results of earlier runs so the summary covers only this selection.
rm -f /ws/build/test_rmw_implementation/test_results/test_rmw_implementation/*.xml
rc=0
colcon --log-base /ws/log test \
  --base-paths "$UPSTREAM" \
  --packages-select test_rmw_implementation \
  --build-base /ws/build \
  --install-base /ws/install \
  --event-handlers console_direct+ \
  --return-code-on-test-failure \
  --ctest-args -R "$full_regex" || rc=$?
colcon test-result --test-result-base /ws/build/test_rmw_implementation --verbose || rc=1
[[ $rc -eq 0 ]] && echo "upstream tests ($VARIANT): OK" || { echo "upstream tests ($VARIANT): FAILED" >&2; exit $rc; }
