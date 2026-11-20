#!/usr/bin/env bash
# Build and run every test on Lua 5.4 and LuaJIT inside the project container, with no
# network access. Exits non-zero on any failure. Run scripts/bootstrap.sh first.
set -euo pipefail
source "$(dirname "$0")/lib/container.sh"

rrlua_ensure_image
rrlua_run --network none -- bash scripts/container/verify-steps.sh "$@"
