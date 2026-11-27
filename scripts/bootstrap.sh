#!/usr/bin/env bash
# Build the podman image and fetch/build every pinned dependency into .deps/.
# Idempotent: finished steps are skipped (stamp files under .deps/stamps/).
set -euo pipefail
source "$(dirname "$0")/lib/container.sh"

rrlua_ensure_image
echo "host architecture: $(uname -m)"
rrlua_run -- bash scripts/container/bootstrap-steps.sh
