#!/usr/bin/env bash
# Portability + no-networking source check (the checker lives in
# ../rmw_intraprocess and runs with its self-test), then the check that public
# headers expose no Robot Raconteur or Boost types.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"
run_in_container - bash -c '
  set -euo pipefail
  python3 -I "$1/scripts/portability/check_portability.py" --self-test --root "$2"
  "$2/scripts/in-container/check-public-headers.sh"
' _ "${RMW_SRC_MOUNT}" "${SRC_MOUNT}"
