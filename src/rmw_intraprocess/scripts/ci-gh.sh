#!/usr/bin/env bash
# Run `gh` against the cross-platform CI scratch repo with the CI token
# (CLAUDE.md S6). Intended for reading run status and logs, e.g.:
#   scripts/ci-gh.sh run list -b ci/rmw_intraprocess
#   scripts/ci-gh.sh run view <id> --log-failed
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CI_TOKEN_FILE="${CI_TOKEN_FILE:-${REPO_ROOT}/../github_ros2_rmw_token.txt}"
GH_TOKEN="$(tr -d '[:space:]' < "${CI_TOKEN_FILE}")"
export GH_TOKEN
export GH_REPO="johnwason/github-actions-scratch"
exec gh "$@"
