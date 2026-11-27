#!/usr/bin/env bash
# ASan and TSan builds of RR core and the binding, then the tests and the teardown stress
# test under each. Slow; not part of verify.sh (CLAUDE.md, M0.5). Run scripts/bootstrap.sh
# first. Pass "asan" or "tsan" to run only one.
set -euo pipefail
source "$(dirname "$0")/lib/container.sh"

rrlua_ensure_image
rrlua_run --network none -- bash scripts/container/sanitizer-steps.sh "$@"
