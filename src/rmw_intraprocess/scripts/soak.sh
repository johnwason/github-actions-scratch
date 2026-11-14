#!/usr/bin/env bash
# Usage: scripts/soak.sh [variant, default release] [minutes, default 30]
# M8c soak mode: runs the torture driver (rmw_intraprocess_torture,
# test_torture: ShortMix and ShutdownDuringMix under every executor, intra-
# process comms off/on) in a loop until the time is up, with a new seed per
# iteration. Stops at the first failure and prints its seed (rerun one
# iteration with TORTURE_SEED=<seed>). Run scripts/build.sh <variant> first.
# Inside the container, --network=none.
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

variant="${1:-release}"; shift || true
check_variant "$variant"
minutes="${1:-30}"
run_in_container "$variant" env "SOAK_MINUTES=${minutes}" \
  bash "${SRC_MOUNT}/scripts/in-container/soak.sh"
