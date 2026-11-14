#!/usr/bin/env bash
# Full gate for a milestone: portability check, then build + test for every variant.
# Usage: scripts/check-all.sh [variants...]   (default: release asan; tsan disabled, see docs/unsupported.md)
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

variants=("$@")
[[ ${#variants[@]} -eq 0 ]] && variants=("${DEFAULT_VARIANTS[@]}")

upstream_regex="$(grep -Ev '^\s*(#|$)' "${REPO_DIR}/scripts/upstream-tests.txt" | paste -sd'|')"
# Anchor each name so test_init does not also match test_init_options.
upstream_regex="^(${upstream_regex})"

"${REPO_DIR}/scripts/check-portability.sh"
for v in "${variants[@]}"; do
  echo "================ variant: $v ================"
  "${REPO_DIR}/scripts/build.sh" "$v"
  "${REPO_DIR}/scripts/test.sh" "$v"
  "${REPO_DIR}/scripts/upstream-test.sh" "$v" "$upstream_regex"
done
echo "check-all: OK (${variants[*]})"
