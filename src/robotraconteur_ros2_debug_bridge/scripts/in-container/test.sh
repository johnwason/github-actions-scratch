#!/usr/bin/env bash
# Inside the container: no-network smoke test, then this repo's tests.
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

[[ -f /ws/install/setup.bash ]] || { echo "no install for variant $VARIANT; run scripts/build.sh $VARIANT" >&2; exit 1; }

"${SRC_DIR}/scripts/in-container/check-no-network.sh"
echo "robotraconteur: $(cat /opt/robotraconteur/VERSIONS | tr '\n' ' ')"

set +u
# shellcheck disable=SC1091
source /ws/install/setup.bash
set -u

cd /ws
pkgs=$(bridge_packages)
for pkg in $pkgs; do
  rm -rf "/ws/build/${pkg}/test_results"
done
rc=0
# shellcheck disable=SC2086
colcon --log-base /ws/log test \
  --base-paths "${SRC_DIR}" \
  --build-base /ws/build --install-base /ws/install \
  --packages-select $pkgs \
  --event-handlers console_direct+ \
  --return-code-on-test-failure \
  "$@" || rc=$?
for pkg in $pkgs; do
  colcon test-result --test-result-base "/ws/build/${pkg}" --verbose || rc=1
done

if [[ $rc -ne 0 ]]; then echo "test ($VARIANT): FAILED" >&2; exit $rc; fi
echo "test ($VARIANT): OK"
