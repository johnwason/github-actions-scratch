# Sourced inside the container by the other in-container scripts.
set -eo pipefail
# ROS setup scripts reference unset variables; enable nounset only afterwards.
# shellcheck disable=SC1091
source /opt/ros/jazzy/setup.bash
set -u

SRC_DIR="/ws/src/rmw_intraprocess"
VARIANT="${RMW_INTRAPROCESS_VARIANT:?RMW_INTRAPROCESS_VARIANT not set}"

case "$VARIANT" in
  release)
    SAN_FLAGS=""
    BUILD_TYPE="Release"
    ;;
  asan)
    SAN_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
    BUILD_TYPE="RelWithDebInfo"
    ;;
  tsan)
    SAN_FLAGS="-fsanitize=thread -fno-omit-frame-pointer"
    BUILD_TYPE="RelWithDebInfo"
    ;;
  *) echo "unknown variant $VARIANT" >&2; exit 2 ;;
esac

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:strict_string_checks=1:detect_stack_use_after_return=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
export TSAN_OPTIONS="halt_on_error=1:second_deadlock_stack=1:suppressions=${SRC_DIR}/scripts/in-container/tsan.supp"
# ament_cppcheck skips by default on Jazzy's cppcheck 2.13 ("performance issues").
# Run it anyway; revisit if it becomes too slow as the code grows.
export AMENT_CPPCHECK_ALLOW_SLOW_VERSIONS=1
