#!/usr/bin/env bash
# Inside the container: smoke test, colcon test, symbol check for $RMW_INTRAPROCESS_VARIANT.
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

[[ -f /ws/install/setup.bash ]] || { echo "no install for variant $VARIANT; run scripts/build.sh $VARIANT" >&2; exit 1; }

"${SRC_DIR}/scripts/in-container/check-no-network.sh"

set +u
# shellcheck disable=SC1091
source /ws/install/setup.bash
set -u

cd /ws
# Results of earlier runs (or of tests since removed) must not count.
for pkg in rmw_intraprocess_cpp rmw_intraprocess_torture; do
  rm -rf "/ws/build/${pkg}/test_results"
done
rc=0
colcon --log-base /ws/log test \
  --base-paths "${SRC_DIR}" \
  --build-base /ws/build \
  --install-base /ws/install \
  --event-handlers console_direct+ \
  --return-code-on-test-failure \
  "$@" || rc=$?
# This repo's packages only (/ws/build also holds the upstream test builds).
for pkg in rmw_intraprocess_cpp rmw_intraprocess_torture; do
  colcon test-result --test-result-base "/ws/build/${pkg}" --verbose || rc=1
done

"${SRC_DIR}/scripts/in-container/check-symbols.sh" || rc=1

if [[ $rc -ne 0 ]]; then echo "test ($VARIANT): FAILED" >&2; exit $rc; fi
echo "test ($VARIANT): OK"
