#!/usr/bin/env bash
# Runs the portability checker (with its self-test) inside the container.
# Usage: scripts/check-portability.sh [paths relative to the repo...]
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

run_in_container - python3 -I "${SRC_MOUNT}/scripts/portability/check_portability.py" \
  --self-test --root "${SRC_MOUNT}" "$@"
