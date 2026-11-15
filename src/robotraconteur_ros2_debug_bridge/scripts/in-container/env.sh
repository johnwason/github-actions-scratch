# Sourced inside the container by the other in-container scripts.
set -eo pipefail
# ROS and RR setup scripts reference unset variables; enable nounset afterwards.
# shellcheck disable=SC1091
source /opt/ros/jazzy/setup.bash
# shellcheck disable=SC1091
source /opt/robotraconteur/setup.bash
# The tsan variant links the TSan-instrumented Robot Raconteur when the image
# has it (RR_TSAN_BUILD=1, the GitHub TSan job); it comes first on the prefix
# path. Python tests keep using the release bindings (a separate process).
if [[ "${RR_BRIDGE_VARIANT:-}" == tsan && -f /opt/robotraconteur-tsan/local_setup.bash ]]; then
  # shellcheck disable=SC1091
  source /opt/robotraconteur-tsan/local_setup.bash
fi
set -u

SRC_DIR="/ws/src/robotraconteur_ros2_debug_bridge"
RMW_SRC_DIR="/ws/src/rmw_intraprocess"
VARIANT="${RR_BRIDGE_VARIANT:?RR_BRIDGE_VARIANT not set}"

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
export AMENT_CPPCHECK_ALLOW_SLOW_VERSIONS=1

# This repo's packages (the ones built with tests and tested here).
bridge_packages() {
  colcon list --base-paths "${SRC_DIR}" --names-only
}

# CMake arguments for the variant (an array: the flags contain spaces).
CMAKE_VARIANT_ARGS=(
  "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
  "-DCMAKE_C_FLAGS=${SAN_FLAGS}"
  "-DCMAKE_CXX_FLAGS=${SAN_FLAGS}"
  "-DCMAKE_EXE_LINKER_FLAGS=${SAN_FLAGS}"
  "-DCMAKE_SHARED_LINKER_FLAGS=${SAN_FLAGS}"
)
