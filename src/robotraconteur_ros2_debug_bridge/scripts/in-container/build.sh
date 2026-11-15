#!/usr/bin/env bash
# Inside the container: build for $RR_BRIDGE_VARIANT. First rmw_intraprocess_cpp
# from the sibling repo (without its tests: the bridge's tests run against it),
# then this repo's packages with tests.
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

"${SRC_DIR}/scripts/in-container/check-no-network.sh"

cd /ws
common=(--log-base /ws/log build
  --base-paths "${SRC_DIR}" "${RMW_SRC_DIR}"
  --build-base /ws/build --install-base /ws/install
  --event-handlers console_cohesion+)
# shellcheck disable=SC2046
colcon "${common[@]}" --packages-select rmw_intraprocess_cpp \
  --cmake-args "${CMAKE_VARIANT_ARGS[@]}" -DBUILD_TESTING=OFF
# shellcheck disable=SC2046
colcon "${common[@]}" --packages-select $(bridge_packages) \
  --cmake-args "${CMAKE_VARIANT_ARGS[@]}" \
    "-DCMAKE_COMPILE_WARNING_AS_ERROR=ON" "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" \
  "$@"
echo "build ($VARIANT): OK"
