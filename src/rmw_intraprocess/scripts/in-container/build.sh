#!/usr/bin/env bash
# Inside the container: colcon build for $RMW_INTRAPROCESS_VARIANT.
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"

"${SRC_DIR}/scripts/in-container/check-no-network.sh"

cd /ws
colcon --log-base /ws/log build \
  --base-paths "${SRC_DIR}" \
  --build-base /ws/build \
  --install-base /ws/install \
  --event-handlers console_cohesion+ \
  --cmake-args \
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}" \
    "-DCMAKE_C_FLAGS=${SAN_FLAGS}" \
    "-DCMAKE_CXX_FLAGS=${SAN_FLAGS}" \
    "-DCMAKE_EXE_LINKER_FLAGS=${SAN_FLAGS}" \
    "-DCMAKE_SHARED_LINKER_FLAGS=${SAN_FLAGS}" \
    "-DCMAKE_COMPILE_WARNING_AS_ERROR=ON" \
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" \
  "$@"
echo "build ($VARIANT): OK"
