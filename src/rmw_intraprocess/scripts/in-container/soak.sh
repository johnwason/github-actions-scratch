#!/usr/bin/env bash
# Inside the container: see scripts/soak.sh.
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
[[ -f /ws/install/setup.bash ]] || { echo "no install for variant $VARIANT; run scripts/build.sh $VARIANT" >&2; exit 1; }
"${SRC_DIR}/scripts/in-container/check-no-network.sh"
set +u
# shellcheck disable=SC1091
source /ws/install/setup.bash
set -u
export RMW_IMPLEMENTATION=rmw_intraprocess_cpp
binary=/ws/build/rmw_intraprocess_torture/test_torture__rmw_intraprocess_cpp
[[ -x "$binary" ]] || { echo "torture driver not built: $binary" >&2; exit 1; }

end=$(( $(date +%s) + ${SOAK_MINUTES:-30} * 60 ))
iteration=0
log=/tmp/soak_iteration.log
while (( $(date +%s) < end )); do
  iteration=$(( iteration + 1 ))
  seed=$(( 1000 + iteration ))
  if ! TORTURE_SEED="$seed" timeout --kill-after=10 1800 "$binary" > "$log" 2>&1; then
    grep -vE '^\[(INFO|WARN|ERROR)\]' "$log" | tail -60
    echo "soak ($VARIANT): FAILED in iteration ${iteration} (TORTURE_SEED=${seed})" >&2
    exit 1
  fi
  passed=$(grep -oE '^\[  PASSED  \] [0-9]+ tests' "$log" | grep -oE '[0-9]+')
  echo "soak ($VARIANT): iteration ${iteration} (TORTURE_SEED=${seed}) passed ${passed} tests"
done
echo "soak ($VARIANT): OK, ${iteration} iterations in ${SOAK_MINUTES:-30} minutes"
